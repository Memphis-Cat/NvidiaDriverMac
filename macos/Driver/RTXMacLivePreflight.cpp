#include "RTXMacLivePreflight.hpp"

#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOMemoryMap.h>

#include "rtxmac/nvidia_snapshot.hpp"

#include <cstdint>
#include <optional>

namespace {
constexpr std::uint64_t kPageBytes = 0x1000ull;
constexpr std::uint64_t kMmuLockPrivMaskOffset = 0x001FA7C8ull;
constexpr std::uint64_t kMmuLockAddrLoOffset = 0x001FA82Cull;
constexpr std::uint64_t kMmuLockAddrHiOffset = 0x001FA830ull;
constexpr std::uint64_t kMmuPageBase =
    kMmuLockPrivMaskOffset & ~(kPageBytes - 1ull);

std::uint32_t ReadMappedU32(std::uintptr_t base,
                            std::uint64_t offset) noexcept {
  const auto inPage = static_cast<std::uintptr_t>(offset - kMmuPageBase);
  const auto* ptr = reinterpret_cast<const volatile std::uint32_t*>(base + inPage);
  return *ptr;
}
} // namespace

RTXMacLiveBoundaryPreflight RTXMacCheckLiveReservedBoundary(
    IOService* owner,
    IOPCIDevice* pci,
    const rtxmac::nvidia::prototype::ReservedBoundaryProfile& profile) noexcept {
  RTXMacLiveBoundaryPreflight out{};
  if (!owner || !pci) {
    out.ioStatus = kIOReturnBadArgument;
    return out;
  }

  std::uint8_t memoryIndex = 0u;
  std::uint8_t memoryType = 0u;
  std::uint64_t bar0Size = 0u;
  kern_return_t kr = pci->GetBARInfo(
      0u, &memoryIndex, &bar0Size, &memoryType);
  if (kr != kIOReturnSuccess ||
      bar0Size < kMmuLockAddrHiOffset + sizeof(std::uint32_t)) {
    out.ioStatus = kr == kIOReturnSuccess ? kIOReturnNoResources : kr;
    return out;
  }

  IOMemoryDescriptor* bar0 = nullptr;
  kr = pci->_CopyDeviceMemoryWithIndex(memoryIndex, &bar0, owner);
  if (kr != kIOReturnSuccess || !bar0) {
    out.ioStatus = kr == kIOReturnSuccess ? kIOReturnNoResources : kr;
    return out;
  }

  IOMemoryMap* map = nullptr;
  kr = bar0->CreateMapping(
      0u, 0u, kMmuPageBase, kPageBytes, 0u, &map);
  if (kr != kIOReturnSuccess || !map) {
    bar0->release();
    out.ioStatus = kr == kIOReturnSuccess ? kIOReturnNoResources : kr;
    return out;
  }

  const auto base = static_cast<std::uintptr_t>(map->GetAddress());
  if (base == 0u) {
    map->release();
    bar0->release();
    out.ioStatus = kIOReturnNoResources;
    return out;
  }

  const std::uint32_t privilegeMask = ReadMappedU32(base, kMmuLockPrivMaskOffset);
  const std::uint32_t lowRegister = ReadMappedU32(base, kMmuLockAddrLoOffset);
  const std::uint32_t highRegister = ReadMappedU32(base, kMmuLockAddrHiOffset);

  map->release();
  bar0->release();

  const rtxmac::nvidia::MmuLockState mmuLock =
      rtxmac::nvidia::DecodeMmuLock(privilegeMask, lowRegister, highRegister);
  out.decision = rtxmac::nvidia::prototype::CheckReservedBoundary(
      profile, std::optional<rtxmac::nvidia::MmuLockState>(mmuLock));
  out.ioStatus = kIOReturnSuccess;
  out.captured = true;
  return out;
}
