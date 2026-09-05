#include "RTXMacDriver.h"

#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <PCIDriverKit/PCIDriverKit.h>

struct RTXMacDriver_IVars {
  IOPCIDevice* pci{nullptr};
};

namespace {
constexpr uint64_t kPageSize = 0x1000ull;
constexpr uint32_t kMaxDmaSegments = 32u;
constexpr uint64_t kMaxDmaChunkBytes = static_cast<uint64_t>(kMaxDmaSegments) * kPageSize;
constexpr uint8_t kDmaAddressBits = 40u;

struct DiagnosticRegister {
  const char* name;
  uint64_t offset;
};

struct PreparedDmaChunk {
  IODMACommand* command{nullptr};
  IOAddressSegment segments[kMaxDmaSegments]{};
  uint32_t segmentCount{0};
  uint64_t flags{0};
  uint64_t offset{0};
  uint64_t length{0};
};

struct PreparedDmaBuffer {
  IOBufferMemoryDescriptor* memory{nullptr};
  PreparedDmaChunk* chunks{nullptr};
  uint32_t chunkCount{0};
  uint64_t length{0};
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

kern_return_t CopyBar0(IOService* owner, IOPCIDevice* pci,
                       IOMemoryDescriptor** descriptor, uint64_t* size) {
  if (!owner || !pci || !descriptor || !size) return kIOReturnBadArgument;

  uint8_t memoryIndex = 0, memoryType = 0;
  kern_return_t kr = pci->GetBARInfo(0, &memoryIndex, size, &memoryType);
  if (kr != kIOReturnSuccess) return kr;

  *descriptor = nullptr;
  kr = pci->_CopyDeviceMemoryWithIndex(memoryIndex, descriptor, owner);
  if (kr != kIOReturnSuccess || !*descriptor) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }
  return kIOReturnSuccess;
}

kern_return_t ReadBar0Register(IOMemoryDescriptor* bar0, uint64_t bar0Size,
                               uint64_t offset, uint32_t* value) {
  if (!bar0 || !value) return kIOReturnBadArgument;
  if ((offset & 3ull) != 0ull) return kIOReturnBadArgument;
  if (offset + sizeof(uint32_t) > bar0Size) return kIOReturnNoResources;

  const uint64_t pageBase = offset & ~(kPageSize - 1ull);
  const uint64_t inPage = offset - pageBase;
  const uint64_t mapLength = (pageBase + kPageSize <= bar0Size)
      ? kPageSize
      : (bar0Size - pageBase);
  if (inPage + sizeof(uint32_t) > mapLength) return kIOReturnNoResources;

  IOMemoryMap* map = nullptr;
  // DriverKit CreateMapping is (options, address, offset, length, alignment, map).
  // Map only the BAR page that contains the requested register.
  kern_return_t kr = bar0->CreateMapping(0, 0, pageBase, mapLength, 0, &map);
  if (kr != kIOReturnSuccess || !map) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }

  const auto base = static_cast<uintptr_t>(map->GetAddress());
  const volatile uint32_t* ptr = reinterpret_cast<const volatile uint32_t*>(base + inPage);
  *value = *ptr;
  map->release();
  return kIOReturnSuccess;
}

void LogDiagnosticSnapshot(IOService* owner, IOPCIDevice* pci) {
  IOMemoryDescriptor* bar0 = nullptr;
  uint64_t bar0Size = 0;
  const kern_return_t copyKr = CopyBar0(owner, pci, &bar0, &bar0Size);
  if (copyKr != kIOReturnSuccess) {
    os_log(OS_LOG_DEFAULT, "rtxmac: snapshot BAR0 unavailable kr=0x%x", copyKr);
    return;
  }

  os_log(OS_LOG_DEFAULT, "rtxmac: snapshot begin BAR0-size=0x%llx", bar0Size);
  for (const auto& reg : kDiagnosticRegisters) {
    uint32_t value = 0;
    const kern_return_t kr = ReadBar0Register(bar0, bar0Size, reg.offset, &value);
    if (kr == kIOReturnSuccess) {
      os_log(OS_LOG_DEFAULT, "rtxmac: snapshot %s @0x%llx = 0x%08x",
             reg.name, reg.offset, value);

      if (reg.offset == 0x00111388ull) {
        const uint32_t active = (value >> 7u) & 1u;
        const uint32_t halted = (value >> 4u) & 1u;
        os_log(OS_LOG_DEFAULT, "rtxmac: snapshot GSP-CPUCTL active=%u halted=%u", active, halted);
      } else if (reg.offset == 0x001183A4ull && value != 0u && value != 0xFFFFFFFFu) {
        os_log(OS_LOG_DEFAULT, "rtxmac: snapshot reported-vram=%u MiB", value);
      }
    } else {
      os_log(OS_LOG_DEFAULT, "rtxmac: snapshot %s @0x%llx read-failed kr=0x%x",
             reg.name, reg.offset, kr);
    }
  }
  os_log(OS_LOG_DEFAULT, "rtxmac: snapshot end");

  bar0->release();
}

void CompleteDmaChunk(PreparedDmaChunk* chunk) {
  if (!chunk) return;
  if (chunk->command) {
    chunk->command->CompleteDMA(kIODMACommandCompleteDMANoOptions);
    chunk->command->release();
  }
  *chunk = {};
}

kern_return_t PrepareDmaChunk(IOPCIDevice* pci, IOMemoryDescriptor* memory,
                              uint64_t offset, uint64_t length,
                              PreparedDmaChunk* out) {
  if (!pci || !memory || !out || length == 0 || length > kMaxDmaChunkBytes) {
    return kIOReturnBadArgument;
  }
  if (offset > (~uint64_t{0}) - length) return kIOReturnBadArgument;

  *out = {};
  IODMACommandSpecification spec = {};
  spec.options = 0;
  spec.maxAddressBits = kDmaAddressBits;

  IODMACommand* command = nullptr;
  kern_return_t kr = IODMACommand::Create(
      pci, kIODMACommandCreateNoOptions, &spec, &command);
  if (kr != kIOReturnSuccess || !command) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }

  uint64_t flags = kIOMemoryDirectionInOut;
  uint32_t segmentCount = kMaxDmaSegments;
  IOAddressSegment segments[kMaxDmaSegments]{};
  kr = command->PrepareForDMA(
      kIODMACommandPrepareForDMANoOptions,
      memory,
      offset,
      length,
      &flags,
      &segmentCount,
      segments);
  if (kr != kIOReturnSuccess) {
    command->release();
    return kr;
  }

  bool valid = segmentCount > 0 && segmentCount <= kMaxDmaSegments;
  uint64_t covered = 0;
  for (uint32_t i = 0; valid && i < segmentCount; ++i) {
    const uint64_t address = segments[i].address;
    const uint64_t bytes = segments[i].length;
    if (bytes == 0 || address > (~uint64_t{0}) - bytes) {
      valid = false;
      break;
    }
    if (covered > length || bytes > length - covered) {
      valid = false;
      break;
    }
    covered += bytes;
  }
  valid = valid && covered == length;

  if (!valid) {
    command->CompleteDMA(kIODMACommandCompleteDMANoOptions);
    command->release();
    return kIOReturnError;
  }

  out->command = command;
  out->segmentCount = segmentCount;
  out->flags = flags;
  out->offset = offset;
  out->length = length;
  for (uint32_t i = 0; i < segmentCount; ++i) out->segments[i] = segments[i];
  return kIOReturnSuccess;
}

void ReleasePreparedDmaBuffer(PreparedDmaBuffer* prepared) {
  if (!prepared) return;
  if (prepared->chunks) {
    for (uint32_t i = 0; i < prepared->chunkCount; ++i) {
      CompleteDmaChunk(&prepared->chunks[i]);
    }
    delete[] prepared->chunks;
  }
  if (prepared->memory) prepared->memory->release();
  *prepared = {};
}

[[maybe_unused]] kern_return_t AllocateAndPrepareDmaBuffer(
    IOPCIDevice* pci, uint64_t length, PreparedDmaBuffer* out) {
  if (!pci || !out || length == 0) return kIOReturnBadArgument;
  *out = {};

  if (length > (~uint64_t{0}) - (kMaxDmaChunkBytes - 1ull)) {
    return kIOReturnNoResources;
  }
  const uint64_t chunkCount64 =
      (length + kMaxDmaChunkBytes - 1ull) / kMaxDmaChunkBytes;
  if (chunkCount64 == 0 || chunkCount64 > 0xFFFFFFFFull) {
    return kIOReturnNoResources;
  }
  const uint32_t chunkCount = static_cast<uint32_t>(chunkCount64);

  IOBufferMemoryDescriptor* memory = nullptr;
  kern_return_t kr = IOBufferMemoryDescriptor::Create(
      kIOMemoryDirectionInOut, length, kPageSize, &memory);
  if (kr != kIOReturnSuccess || !memory) {
    return kr == kIOReturnSuccess ? kIOReturnNoMemory : kr;
  }

  PreparedDmaChunk* chunks = new PreparedDmaChunk[chunkCount]();
  if (!chunks) {
    memory->release();
    return kIOReturnNoMemory;
  }

  uint32_t preparedCount = 0;
  for (uint32_t i = 0; i < chunkCount; ++i) {
    const uint64_t offset = static_cast<uint64_t>(i) * kMaxDmaChunkBytes;
    const uint64_t remaining = length - offset;
    const uint64_t chunkLength =
        remaining < kMaxDmaChunkBytes ? remaining : kMaxDmaChunkBytes;

    kr = PrepareDmaChunk(pci, memory, offset, chunkLength, &chunks[i]);
    if (kr != kIOReturnSuccess) {
      for (uint32_t j = 0; j < preparedCount; ++j) CompleteDmaChunk(&chunks[j]);
      delete[] chunks;
      memory->release();
      return kr;
    }
    ++preparedCount;
  }

  out->memory = memory;
  out->chunks = chunks;
  out->chunkCount = chunkCount;
  out->length = length;
  return kIOReturnSuccess;
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

  // Prototype 1 is intentionally read-only. DMA allocation/preparation helpers
  // are compiled but are not invoked from Start. No config writes, MMIO writes,
  // resets, firmware loading, or GSP boot are performed.
  LogDiagnosticSnapshot(this, ivars->pci);

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
