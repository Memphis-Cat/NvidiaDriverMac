#include "RTXMacDriver.h"

#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <PCIDriverKit/PCIDriverKit.h>

struct RTXMacDriver_IVars {
  IOPCIDevice* pci{nullptr};
};

namespace {
constexpr uint64_t kPmcBoot0Offset = 0x00000000ull;
constexpr uint64_t kPmcBoot42Offset = 0x00000A00ull;
constexpr uint64_t kIdentityMapLength = 0x1000ull;

kern_return_t ReadIdentityRegisters(IOService* owner, IOPCIDevice* pci, uint32_t* boot0, uint32_t* boot42) {
  if (!owner || !pci || !boot0 || !boot42) return kIOReturnBadArgument;

  uint8_t memoryIndex = 0, memoryType = 0;
  uint64_t memorySize = 0;
  kern_return_t kr = pci->GetBARInfo(0, &memoryIndex, &memorySize, &memoryType);
  if (kr != kIOReturnSuccess) return kr;
  if (memorySize < kIdentityMapLength) return kIOReturnNoResources;

  IOMemoryDescriptor* bar0 = nullptr;
  kr = pci->_CopyDeviceMemoryWithIndex(memoryIndex, &bar0, owner);
  if (kr != kIOReturnSuccess || !bar0) return kr == kIOReturnSuccess ? kIOReturnError : kr;

  IOMemoryMap* map = nullptr;
  kr = bar0->CreateMapping(0, 0, 0, 0, kIdentityMapLength, &map);
  if (kr != kIOReturnSuccess || !map) {
    bar0->release();
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }

  const auto base = static_cast<uintptr_t>(map->GetAddress());
  const volatile uint32_t* boot0Ptr = reinterpret_cast<const volatile uint32_t*>(base + kPmcBoot0Offset);
  const volatile uint32_t* boot42Ptr = reinterpret_cast<const volatile uint32_t*>(base + kPmcBoot42Offset);
  *boot0 = *boot0Ptr;
  *boot42 = *boot42Ptr;

  map->release();
  bar0->release();
  return kIOReturnSuccess;
}
}

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

  uint16_t vendor = 0, device = 0, subsystemVendor = 0, subsystemDevice = 0;
  uint8_t revision = 0;
  ivars->pci->ConfigurationRead16(kIOPCIConfigurationOffsetVendorID, &vendor);
  ivars->pci->ConfigurationRead16(kIOPCIConfigurationOffsetDeviceID, &device);
  ivars->pci->ConfigurationRead8(kIOPCIConfigurationOffsetRevisionID, &revision);
  ivars->pci->ConfigurationRead16(0x2C, &subsystemVendor);
  ivars->pci->ConfigurationRead16(0x2E, &subsystemDevice);

  os_log(OS_LOG_DEFAULT,
         "rtxmac: PCI %04x:%04x subsystem %04x:%04x rev %02x",
         vendor, device, subsystemVendor, subsystemDevice, revision);

  for (uint8_t bar = 0; bar < 6; ++bar) {
    uint8_t memoryIndex = 0, memoryType = 0;
    uint64_t memorySize = 0;
    const kern_return_t barKr = ivars->pci->GetBARInfo(bar, &memoryIndex, &memorySize, &memoryType);
    if (barKr == kIOReturnSuccess) {
      os_log(OS_LOG_DEFAULT,
             "rtxmac: BAR%u index=%u size=0x%llx type=%u",
             bar, memoryIndex, memorySize, memoryType);
    }
  }

  uint32_t boot0 = 0, boot42 = 0;
  const kern_return_t idKr = ReadIdentityRegisters(this, ivars->pci, &boot0, &boot42);
  if (idKr == kIOReturnSuccess) {
    const uint32_t architecture = (boot42 >> 24u) & 0x1Fu;
    const uint32_t implementation = (boot42 >> 20u) & 0x0Fu;
    os_log(OS_LOG_DEFAULT,
           "rtxmac: read-only MMIO NV_PMC_BOOT_0=0x%08x NV_PMC_BOOT_42=0x%08x arch=0x%x impl=0x%x",
           boot0, boot42, architecture, implementation);
  } else {
    os_log(OS_LOG_DEFAULT, "rtxmac: read-only identity MMIO unavailable kr=0x%x", idKr);
  }

  RegisterService();
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
