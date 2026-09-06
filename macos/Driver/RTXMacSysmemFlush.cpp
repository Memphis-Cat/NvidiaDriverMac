#include "RTXMacSysmemFlush.hpp"

#include <DriverKit/IOMemoryMap.h>

#include <cstdint>

namespace {
constexpr std::uint64_t kPageBytes = 0x1000ull;

kern_return_t MapRegister(IOMemoryDescriptor* bar0,
                          std::uint64_t bar0Size,
                          std::uint32_t offset,
                          IOMemoryMap** outMap,
                          volatile std::uint32_t** outRegister) noexcept {
  if (!bar0 || !outMap || !outRegister) return kIOReturnBadArgument;
  *outMap = nullptr;
  *outRegister = nullptr;
  if ((offset & 3u) != 0u ||
      static_cast<std::uint64_t>(offset) + sizeof(std::uint32_t) > bar0Size) {
    return kIOReturnNoResources;
  }

  const std::uint64_t pageBase = static_cast<std::uint64_t>(offset) & ~(kPageBytes - 1u);
  const std::uint64_t inPage = static_cast<std::uint64_t>(offset) - pageBase;
  const std::uint64_t mapLength =
      pageBase + kPageBytes <= bar0Size ? kPageBytes : bar0Size - pageBase;
  if (inPage + sizeof(std::uint32_t) > mapLength) return kIOReturnNoResources;

  IOMemoryMap* map = nullptr;
  const kern_return_t kr = bar0->CreateMapping(0, 0, pageBase, mapLength, 0, &map);
  if (kr != kIOReturnSuccess || !map) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }

  const auto base = static_cast<std::uintptr_t>(map->GetAddress());
  if (base == 0u) {
    map->release();
    return kIOReturnNoResources;
  }

  *outMap = map;
  *outRegister = reinterpret_cast<volatile std::uint32_t*>(base + inPage);
  return kIOReturnSuccess;
}

kern_return_t ReadRegister(IOMemoryDescriptor* bar0,
                           std::uint64_t bar0Size,
                           std::uint32_t offset,
                           std::uint32_t* value) noexcept {
  if (!value) return kIOReturnBadArgument;
  IOMemoryMap* map = nullptr;
  volatile std::uint32_t* reg = nullptr;
  const kern_return_t kr = MapRegister(bar0, bar0Size, offset, &map, &reg);
  if (kr != kIOReturnSuccess) return kr;
  *value = *reg;
  map->release();
  return kIOReturnSuccess;
}

kern_return_t WriteRegister(IOMemoryDescriptor* bar0,
                            std::uint64_t bar0Size,
                            std::uint32_t offset,
                            std::uint32_t value) noexcept {
  IOMemoryMap* map = nullptr;
  volatile std::uint32_t* reg = nullptr;
  const kern_return_t kr = MapRegister(bar0, bar0Size, offset, &map, &reg);
  if (kr != kIOReturnSuccess) return kr;
  *reg = value;
  const volatile std::uint32_t flush = *reg;
  (void)flush;
  map->release();
  return kIOReturnSuccess;
}

kern_return_t ReadState(IOMemoryDescriptor* bar0,
                        std::uint64_t bar0Size,
                        std::uint32_t* lo,
                        std::uint32_t* hi) noexcept {
  using namespace rtxmac::nvidia;
  if (!lo || !hi) return kIOReturnBadArgument;
  kern_return_t kr = ReadRegister(bar0, bar0Size, kSysmemFlushAddrLoOffset, lo);
  if (kr != kIOReturnSuccess) return kr;
  return ReadRegister(bar0, bar0Size, kSysmemFlushAddrHiOffset, hi);
}

kern_return_t WriteState(IOMemoryDescriptor* bar0,
                         std::uint64_t bar0Size,
                         std::uint32_t lo,
                         std::uint32_t hi) noexcept {
  using namespace rtxmac::nvidia;
  // Match GA100/GA102 reference ordering: high address field first, then low.
  kern_return_t kr = WriteRegister(bar0, bar0Size, kSysmemFlushAddrHiOffset, hi);
  if (kr != kIOReturnSuccess) return kr;
  return WriteRegister(bar0, bar0Size, kSysmemFlushAddrLoOffset, lo);
}

bool StateEquals(std::uint32_t lo,
                 std::uint32_t hi,
                 std::uint32_t expectedLo,
                 std::uint32_t expectedHi) noexcept {
  return lo == expectedLo && hi == expectedHi;
}

kern_return_t RestoreAndVerify(IOMemoryDescriptor* bar0,
                               std::uint64_t bar0Size,
                               std::uint32_t lo,
                               std::uint32_t hi) noexcept {
  kern_return_t kr = WriteState(bar0, bar0Size, lo, hi);
  if (kr != kIOReturnSuccess) return kr;
  std::uint32_t verifyLo = 0u, verifyHi = 0u;
  kr = ReadState(bar0, bar0Size, &verifyLo, &verifyHi);
  if (kr != kIOReturnSuccess) return kr;
  return StateEquals(verifyLo, verifyHi, lo, hi) ? kIOReturnSuccess : kIOReturnIOError;
}
} // namespace

kern_return_t RTXMacProgramSysmemFlushPage(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::SysmemFlushPagePlan& plan,
    bool writesEnabled) noexcept {
  using namespace rtxmac::nvidia;
  if (!writesEnabled) return kIOReturnNotPermitted;
  if (!bar0 || !ValidateSysmemFlushPagePlan(plan)) return kIOReturnBadArgument;

  std::uint32_t liveLo = 0u, liveHi = 0u;
  kern_return_t kr = ReadState(bar0, bar0Size, &liveLo, &liveHi);
  if (kr != kIOReturnSuccess) return kr;
  if (!StateEquals(liveLo, liveHi, plan.oldLo, plan.oldHi)) {
    return kIOReturnNotPermitted;
  }

  kr = WriteState(bar0, bar0Size, plan.newLo, plan.newHi);
  if (kr != kIOReturnSuccess) {
    (void)RestoreAndVerify(bar0, bar0Size, plan.oldLo, plan.oldHi);
    return kr;
  }

  kr = ReadState(bar0, bar0Size, &liveLo, &liveHi);
  if (kr == kIOReturnSuccess &&
      StateEquals(liveLo, liveHi, plan.newLo, plan.newHi)) {
    return kIOReturnSuccess;
  }

  const kern_return_t rollbackKr =
      RestoreAndVerify(bar0, bar0Size, plan.oldLo, plan.oldHi);
  if (rollbackKr != kIOReturnSuccess) return rollbackKr;
  return kr == kIOReturnSuccess ? kIOReturnIOError : kr;
}

kern_return_t RTXMacRollbackSysmemFlushPage(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::SysmemFlushPagePlan& plan,
    bool writesEnabled) noexcept {
  using namespace rtxmac::nvidia;
  if (!writesEnabled) return kIOReturnNotPermitted;
  if (!bar0 || !ValidateSysmemFlushPagePlan(plan)) return kIOReturnBadArgument;

  std::uint32_t liveLo = 0u, liveHi = 0u;
  kern_return_t kr = ReadState(bar0, bar0Size, &liveLo, &liveHi);
  if (kr != kIOReturnSuccess) return kr;
  if (!StateEquals(liveLo, liveHi, plan.newLo, plan.newHi)) {
    return kIOReturnNotPermitted;
  }
  return RestoreAndVerify(bar0, bar0Size, plan.oldLo, plan.oldHi);
}
