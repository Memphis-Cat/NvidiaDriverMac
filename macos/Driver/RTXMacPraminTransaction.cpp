#include "RTXMacPramin.hpp"

#include <DriverKit/IOMemoryMap.h>

#include <array>
#include <cstring>
#include <limits>

namespace {
constexpr std::uint64_t kPageBytes = 0x1000ull;

struct MappedMmioRange {
  IOMemoryMap* map{nullptr};
  const volatile std::uint8_t* bytes{nullptr};
};

void ReleaseMappedRange(MappedMmioRange* range) noexcept {
  if (!range) return;
  if (range->map) range->map->release();
  range->map = nullptr;
  range->bytes = nullptr;
}

kern_return_t MapBar0ReadRange(IOMemoryDescriptor* bar0,
                               std::uint64_t bar0Size,
                               std::uint64_t offset,
                               std::uint64_t bytes,
                               MappedMmioRange* out) noexcept {
  if (!bar0 || !out || bytes == 0u) return kIOReturnBadArgument;
  out->map = nullptr;
  out->bytes = nullptr;
  if (offset >= bar0Size || bytes > bar0Size - offset) return kIOReturnNoResources;
  if (offset > std::numeric_limits<std::uint64_t>::max() - (bytes - 1u)) {
    return kIOReturnBadArgument;
  }

  const std::uint64_t pageBase = offset & ~(kPageBytes - 1u);
  const std::uint64_t inPage = offset - pageBase;
  const std::uint64_t needed = inPage + bytes;
  if (needed > std::numeric_limits<std::uint64_t>::max() - (kPageBytes - 1u)) {
    return kIOReturnBadArgument;
  }
  const std::uint64_t mapLength = (needed + kPageBytes - 1u) & ~(kPageBytes - 1u);
  if (pageBase >= bar0Size || mapLength > bar0Size - pageBase) return kIOReturnNoResources;

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
  out->map = map;
  out->bytes = reinterpret_cast<const volatile std::uint8_t*>(base + inPage);
  return kIOReturnSuccess;
}

kern_return_t SelectWindow(IOMemoryDescriptor* bar0,
                           std::uint64_t bar0Size,
                           std::uint32_t selector) noexcept {
  using namespace rtxmac;
  using namespace rtxmac::nvidia;
  constexpr std::array rules{
      MmioWriteRule{.offset = kPraminWindowSelectOffset, .writableMask = 0xFFFFFFFFu},
  };
  if (CheckMmioWrite(rules, kPraminWindowSelectOffset, 0u, selector) != WriteDecision::Allowed) {
    return kIOReturnNotPermitted;
  }

  MappedMmioRange range{};
  kern_return_t kr = MapBar0ReadRange(bar0, bar0Size, kPraminWindowSelectOffset,
                                      sizeof(std::uint32_t), &range);
  if (kr != kIOReturnSuccess) return kr;
  auto* reg = const_cast<volatile std::uint32_t*>(
      reinterpret_cast<const volatile std::uint32_t*>(range.bytes));
  *reg = selector;
  const volatile std::uint32_t flush = *reg;
  (void)flush;
  ReleaseMappedRange(&range);
  return kIOReturnSuccess;
}

kern_return_t ReadAperture(IOMemoryDescriptor* bar0,
                           std::uint64_t bar0Size,
                           std::uint32_t offset,
                           std::uint8_t* destination,
                           std::uint64_t bytes) noexcept {
  using namespace rtxmac;
  using namespace rtxmac::nvidia;
  if (!destination || bytes == 0u || (bytes & 3u) != 0u) return kIOReturnBadArgument;
  constexpr std::array regions{
      MmioWriteRegionRule{
          .offset = kPraminApertureOffset,
          .length = kPraminApertureBytes,
          .alignment = 4u,
      },
  };
  if (CheckMmioRegionWrite(regions, offset, bytes) != RegionWriteDecision::Allowed) {
    return kIOReturnNotPermitted;
  }

  MappedMmioRange range{};
  kern_return_t kr = MapBar0ReadRange(bar0, bar0Size, offset, bytes, &range);
  if (kr != kIOReturnSuccess) return kr;

  for (std::uint64_t i = 0u; i < bytes; i += sizeof(std::uint32_t)) {
    const auto* src = reinterpret_cast<const volatile std::uint32_t*>(range.bytes + i);
    const std::uint32_t value = *src;
    std::memcpy(destination + i, &value, sizeof(value));
  }
  ReleaseMappedRange(&range);
  return kIOReturnSuccess;
}

bool ValidateChunk(const rtxmac::nvidia::PraminStageChunk& chunk,
                   std::uint64_t expectedVram,
                   std::uint64_t expectedSource,
                   std::uint64_t totalBytes) noexcept {
  using namespace rtxmac::nvidia;
  if (chunk.bytes == 0u || (chunk.bytes & 3u) != 0u ||
      chunk.vramOffset != expectedVram || chunk.sourceOffset != expectedSource ||
      chunk.sourceOffset > totalBytes || chunk.bytes > totalBytes - chunk.sourceOffset) {
    return false;
  }
  const std::uint64_t windowBase = chunk.vramOffset & ~kPraminWindowMask;
  const std::uint64_t inWindow = chunk.vramOffset & kPraminWindowMask;
  const std::uint64_t selector = windowBase >> 16u;
  const std::uint64_t aperture = static_cast<std::uint64_t>(kPraminApertureOffset) + inWindow;
  return chunk.bytes <= kPraminWindowBytes - inWindow &&
      selector <= std::numeric_limits<std::uint32_t>::max() &&
      aperture <= std::numeric_limits<std::uint32_t>::max() &&
      chunk.windowBase == windowBase &&
      chunk.windowSelector == static_cast<std::uint32_t>(selector) &&
      chunk.bar0ApertureOffset == static_cast<std::uint32_t>(aperture);
}
} // namespace

kern_return_t RTXMacReadPramin(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    void* destination,
    std::uint64_t destinationBytes,
    bool writesEnabled) noexcept {
  using namespace rtxmac::nvidia;
  if (!writesEnabled) return kIOReturnNotPermitted;
  if (!bar0 || !destination || !stage.valid || stage.vramSize == 0u ||
      stage.totalBytes == 0u || stage.chunks.empty() || destinationBytes != stage.totalBytes ||
      stage.vramOffset >= stage.vramSize || stage.totalBytes > stage.vramSize - stage.vramOffset) {
    return kIOReturnBadArgument;
  }

  auto* out = static_cast<std::uint8_t*>(destination);
  std::uint64_t expectedVram = stage.vramOffset;
  std::uint64_t expectedSource = 0u;
  for (const auto& chunk : stage.chunks) {
    if (!ValidateChunk(chunk, expectedVram, expectedSource, destinationBytes)) {
      return kIOReturnBadArgument;
    }
    kern_return_t kr = SelectWindow(bar0, bar0Size, chunk.windowSelector);
    if (kr != kIOReturnSuccess) return kr;
    kr = ReadAperture(bar0, bar0Size, chunk.bar0ApertureOffset,
                      out + chunk.sourceOffset, chunk.bytes);
    if (kr != kIOReturnSuccess) return kr;
    expectedVram += chunk.bytes;
    expectedSource += chunk.bytes;
  }
  return (expectedSource == stage.totalBytes &&
          expectedVram == stage.vramOffset + stage.totalBytes)
      ? kIOReturnSuccess : kIOReturnBadArgument;
}

kern_return_t RTXMacStagePraminTransactional(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    const void* source,
    std::uint64_t sourceBytes,
    void* backupStorage,
    std::uint64_t backupBytes,
    bool writesEnabled) noexcept {
  if (!writesEnabled) return kIOReturnNotPermitted;
  if (!bar0 || !source || !backupStorage || sourceBytes == 0u ||
      backupBytes != sourceBytes || sourceBytes != stage.totalBytes) {
    return kIOReturnBadArgument;
  }

  kern_return_t kr = RTXMacReadPramin(
      bar0, bar0Size, stage, backupStorage, backupBytes, true);
  if (kr != kIOReturnSuccess) return kr;

  kr = RTXMacStagePramin(bar0, bar0Size, stage, source, sourceBytes, true);
  if (kr == kIOReturnSuccess) {
    kr = RTXMacVerifyPramin(bar0, bar0Size, stage, source, sourceBytes, true);
  }
  if (kr == kIOReturnSuccess) return kIOReturnSuccess;

  const kern_return_t originalFailure = kr;
  kern_return_t rollback = RTXMacStagePramin(
      bar0, bar0Size, stage, backupStorage, backupBytes, true);
  if (rollback == kIOReturnSuccess) {
    rollback = RTXMacVerifyPramin(
        bar0, bar0Size, stage, backupStorage, backupBytes, true);
  }
  return rollback == kIOReturnSuccess ? originalFailure : kIOReturnError;
}
