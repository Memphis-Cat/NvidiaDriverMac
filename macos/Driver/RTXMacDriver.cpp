#include "RTXMacDriver.h"

#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/IOUserClient.h>
#include <PCIDriverKit/PCIDriverKit.h>

#include <cstdint>

struct RTXMacDriver_IVars {
  IOPCIDevice* pci{nullptr};
};

namespace {
constexpr std::uint64_t kPageSize = 0x1000ull;

struct DiagnosticRegister {
  const char* name;
  std::uint64_t offset;
};

// Read-only diagnostic snapshot. Every offset is cross-checked against
// NVIDIA's published Ampere/Turing-compatible register headers and the
// corresponding tinygrad generated register tables.
constexpr DiagnosticRegister kDiagnosticRegisters[] = {
  {"NV_PMC_BOOT_0",                         0x00000000ull},
  {"NV_PMC_BOOT_42",                        0x00000A00ull},
  {"NV_PFB_PRI_MMU_WPR2_ADDR_HI",           0x001FA828ull},
  {"NV_PGSP_FALCON_MAILBOX0",               0x00110040ull},
  {"NV_PGSP_FALCON_MAILBOX1",               0x00110044ull},
  {"GSP_NV_PRISCV_RISCV_CPUCTL",            0x00111388ull},
  {"NV_PGC6_AON_SECURE_SCRATCH_GROUP_42",   0x001183A4ull},
  {"NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_0", 0x00118234ull},
  {"NV_PGC6_BSI_SECURE_SCRATCH_14",         0x001180F8ull},
  {"NV_PSEC_FALCON_MAILBOX0",               0x00840040ull},
  {"NV_PSEC_FALCON_MAILBOX1",               0x00840044ull},
};

kern_return_t CopyBar0(IOService* owner,
                       IOPCIDevice* pci,
                       IOMemoryDescriptor** descriptor,
                       std::uint64_t* size) {
  if (!owner || !pci || !descriptor || !size) return kIOReturnBadArgument;

  std::uint8_t memoryIndex = 0, memoryType = 0;
  kern_return_t kr = pci->GetBARInfo(0, &memoryIndex, size, &memoryType);
  if (kr != kIOReturnSuccess) return kr;

  *descriptor = nullptr;
  kr = pci->_CopyDeviceMemoryWithIndex(memoryIndex, descriptor, owner);
  if (kr != kIOReturnSuccess || !*descriptor) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }
  return kIOReturnSuccess;
}

kern_return_t ReadBar0Register(IOMemoryDescriptor* bar0,
                               std::uint64_t bar0Size,
                               std::uint64_t offset,
                               std::uint32_t* value) {
  if (!bar0 || !value || (offset & 3ull) != 0ull ||
      offset > bar0Size || sizeof(std::uint32_t) > bar0Size - offset) {
    return kIOReturnBadArgument;
  }

  const std::uint64_t pageBase = offset & ~(kPageSize - 1ull);
  const std::uint64_t inPage = offset - pageBase;
  const std::uint64_t mapLength =
      pageBase + kPageSize <= bar0Size ? kPageSize : bar0Size - pageBase;
  if (inPage + sizeof(std::uint32_t) > mapLength) return kIOReturnNoResources;

  IOMemoryMap* map = nullptr;
  kern_return_t kr = bar0->CreateMapping(0, 0, pageBase, mapLength, 0, &map);
  if (kr != kIOReturnSuccess || !map) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }

  const auto base = static_cast<std::uintptr_t>(map->GetAddress());
  if (base == 0u) {
    map->release();
    return kIOReturnNoResources;
  }
  const auto* ptr = reinterpret_cast<const volatile std::uint32_t*>(base + inPage);
  *value = *ptr;
  map->release();
  return kIOReturnSuccess;
}

void LogDiagnosticSnapshot(IOService* owner, IOPCIDevice* pci) {
  IOMemoryDescriptor* bar0 = nullptr;
  std::uint64_t bar0Size = 0;
  const kern_return_t copyKr = CopyBar0(owner, pci, &bar0, &bar0Size);
  if (copyKr != kIOReturnSuccess) {
    os_log(OS_LOG_DEFAULT, "rtxmac: snapshot BAR0 unavailable kr=0x%x", copyKr);
    return;
  }

  os_log(OS_LOG_DEFAULT, "rtxmac: snapshot begin BAR0-size=0x%llx", bar0Size);
  for (const auto& reg : kDiagnosticRegisters) {
    std::uint32_t value = 0;
    const kern_return_t kr = ReadBar0Register(bar0, bar0Size, reg.offset, &value);
    if (kr == kIOReturnSuccess) {
      os_log(OS_LOG_DEFAULT, "rtxmac: snapshot %s @0x%llx = 0x%08x",
             reg.name, reg.offset, value);

      if (reg.offset == 0x00111388ull) {
        const std::uint32_t active = (value >> 7u) & 1u;
        const std::uint32_t halted = (value >> 4u) & 1u;
        os_log(OS_LOG_DEFAULT,
               "rtxmac: snapshot GSP-CPUCTL active=%u halted=%u",
               active, halted);
      } else if (reg.offset == 0x001183A4ull &&
                 value != 0u && value != 0xFFFFFFFFu) {
        os_log(OS_LOG_DEFAULT, "rtxmac: snapshot reported-vram=%u MiB", value);
      }
    } else {
      os_log(OS_LOG_DEFAULT,
             "rtxmac: snapshot %s @0x%llx read-failed kr=0x%x",
             reg.name, reg.offset, kr);
    }
  }
  os_log(OS_LOG_DEFAULT, "rtxmac: snapshot end");
  bar0->release();
}
} // namespace

bool RTXMacDriver::init() {
  if (!super::init()) return false;
  ivars = new RTXMacDriver_IVars();
  return ivars != nullptr;
}

void RTXMacDriver::free() {
  IOSafeDeleteNULL(ivars, RTXMacDriver_IVars, 1);
  super::free();
}

kern_return_t RTXMacDriver::Start_Impl(IOService* provider) {
  kern_return_t kr = Start(provider, SUPERDISPATCH);
  if (kr != kIOReturnSuccess) return kr;

  ivars->pci = OSDynamicCast(IOPCIDevice, provider);
  if (!ivars->pci) return kIOReturnNoDevice;

  kr = ivars->pci->Open(this, 0);
  if (kr != kIOReturnSuccess) {
    ivars->pci = nullptr;
    return kr;
  }

  std::uint16_t vendor = 0, device = 0, subsystemVendor = 0, subsystemDevice = 0;
  std::uint8_t revision = 0;
  ivars->pci->ConfigurationRead16(kIOPCIConfigurationOffsetVendorID, &vendor);
  ivars->pci->ConfigurationRead16(kIOPCIConfigurationOffsetDeviceID, &device);
  ivars->pci->ConfigurationRead8(kIOPCIConfigurationOffsetRevisionID, &revision);
  ivars->pci->ConfigurationRead16(0x2C, &subsystemVendor);
  ivars->pci->ConfigurationRead16(0x2E, &subsystemDevice);

  os_log(OS_LOG_DEFAULT,
         "rtxmac: PCI %04x:%04x subsystem %04x:%04x rev %02x",
         vendor, device, subsystemVendor, subsystemDevice, revision);

  for (std::uint8_t bar = 0; bar < 6; ++bar) {
    std::uint8_t memoryIndex = 0, memoryType = 0;
    std::uint64_t memorySize = 0;
    const kern_return_t barKr =
        ivars->pci->GetBARInfo(bar, &memoryIndex, &memorySize, &memoryType);
    if (barKr == kIOReturnSuccess) {
      os_log(OS_LOG_DEFAULT,
             "rtxmac: BAR%u index=%u size=0x%llx type=%u",
             bar, memoryIndex, memorySize, memoryType);
    }
  }

  // Prototype 1 remains intentionally read-only. Reusable DMA preparation,
  // PRAMIN transactions, reset recovery, Falcon execution, and single-step GSP
  // phase execution are compiled in separate cold modules but are never called
  // from Start_Impl. The user client added later in the prototype is also
  // validation-only: package upload does not cause any GPU write or reset.
  LogDiagnosticSnapshot(this, ivars->pci);

  RegisterService();
  return kIOReturnSuccess;
}

kern_return_t RTXMacDriver::NewUserClient_Impl(
    std::uint32_t type,
    IOUserClient** userClient) {
  if (type != 0u || !userClient) return kIOReturnBadArgument;
  *userClient = nullptr;

  IOService* service = nullptr;
  kern_return_t kr = Create(this, "UserClientProperties", &service);
  if (kr != kIOReturnSuccess) return kr;
  *userClient = OSDynamicCast(IOUserClient, service);
  if (!*userClient) {
    service->release();
    return kIOReturnUnsupported;
  }
  return kIOReturnSuccess;
}

kern_return_t RTXMacDriver::Stop_Impl(IOService* provider) {
  if (ivars && ivars->pci) {
    ivars->pci->Close(this, 0);
    ivars->pci = nullptr;
  }
  return Stop(provider, SUPERDISPATCH);
}

IOPCIDevice* RTXMacDriver::GetPCI() { return ivars ? ivars->pci : nullptr; }
