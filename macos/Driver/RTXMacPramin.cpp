#include "RTXMacPramin.hpp"

#include <DriverKit/IOMemoryMap.h>

#include <array>
#include <cstring>
#include <limits>

namespace {
constexpr std::uint64_t kPageBytes = 0x1000ull;

struct MappedMmioRange {
  IOMemoryMap* map{nullptr};
  volatile std::uint8_t* bytes{nullptr};
};

void ReleaseMappedRange(MappedMmioRange* range) noexcept {
  if (!range) return;
  if (range->map) range->map->release();
  range->map = nullptr;
  range->bytes = nullptr;
}

kern_return_t MapBar0Range(IOMemoryDescriptor* bar0,
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
  out->bytes = reinterpret_cast<volatile std::uint8_t*>(base + inPage);
  return kIOReturnSuccess;
}

kern_return_t WriteSelector(IOMemoryDescriptor* bar0,
                            std::uint64_t bar0Size,
                            std::uint32_t value) noexcept {
  using namespace rtxmac;
  using namespace rtxmac::nvidia;

  constexpr std::array rules{
      MmioWriteRule{.offset = kPraminWindowSelectOffset, .writableMask = 0xFFFFFFFFu},
  };
  if (CheckMmioWrite(rules, kPraminWindowSelectOffset, 0u, value) != WriteDecision::Allowed) {
    return kIOReturnNotPermitted;
  }

  MappedMmioRange range{};
  kern_return_t kr = MapBar0Range(bar0, bar0Size, kPraminWindowSelectOffset,
                                  sizeof(std::uint32_t), &range);
  if (kr != kIOReturnSuccess) return kr;

  auto* reg = reinterpret_cast<volatile std::uint32_t*>(range.bytes);
  *reg = value;
  const volatile std::uint32_t postedWriteFlush = *reg;
  (void)postedWriteFlush;
  ReleaseMappedRange(&range);
  return kIOReturnSuccess;
}

kern_return_t WriteAperture(IOMemoryDescriptor* bar0,
                            std::uint64_t bar0Size,
                            std::uint32_t bar0Offset,
                            const std::uint8_t* source,
                            std::uint64_t bytes) noexcept {
  using namespace rtxmac;
  using namespace rtxmac::nvidia;

  if (!source || bytes == 0u || (bytes & 3u) != 0u) return kIOReturnBadArgument;
  constexpr std::array regions{
      MmioWriteRegionRule{
          .offset = kPraminApertureOffset,
          .length = kPraminApertureBytes,
          .alignment = 4u,
      },
  };
  if (CheckMmioRegionWrite(regions, bar0Offset, bytes) != RegionWriteDecision::Allowed) {
    return kIOReturnNotPermitted;
  }

  MappedMmioRange range{};
  kern_return_t kr = MapBar0Range(bar0, bar0Size, bar0Offset, bytes, &range);
  if (kr != kIOReturnSuccess) return kr;

  for (std::uint64_t offset = 0; offset < bytes; offset += sizeof(std::uint32_t)) {
    std::uint32_t value = 0u;
    std::memcpy(&value, source + offset, sizeof(value));
    auto* dst = reinterpret_cast<volatile std::uint32_t*>(range.bytes + offset);
    *dst = value;
  }

  auto* last = reinterpret_cast<volatile std::uint32_t*>(range.bytes + bytes - sizeof(std::uint32_t));
  const volatile std::uint32_t postedWriteFlush = *last;
  (void)postedWriteFlush;
  ReleaseMappedRange(&range);
  return kIOReturnSuccess;
}

kern_return_t CompareAperture(IOMemoryDescriptor* bar0,
                              std::uint64_t bar0Size,
                              std::uint32_t bar0Offset,
                              const std::uint8_t* expected,
                              std::uint64_t bytes) noexcept {
  using namespace rtxmac;
  using namespace rtxmac::nvidia;

  if (!expected || bytes == 0u || (bytes & 3u) != 0u) return kIOReturnBadArgument;
  constexpr std::array regions{
      MmioWriteRegionRule{
          .offset = kPraminApertureOffset,
          .length = kPraminApertureBytes,
          .alignment = 4u,
      },
  };
  if (CheckMmioRegionWrite(regions, bar0Offset, bytes) != RegionWriteDecision::Allowed) {
    return kIOReturnNotPermitted;
  }

  MappedMmioRange range{};
  kern_return_t kr = MapBar0Range(bar0, bar0Size, bar0Offset, bytes, &range);
  if (kr != kIOReturnSuccess) return kr;

  for (std::uint64_t offset = 0; offset < bytes; offset += sizeof(std::uint32_t)) {
    std::uint32_t expectedValue = 0u;
    std::memcpy(&expectedValue, expected + offset, sizeof(expectedValue));
    const auto* src = reinterpret_cast<const volatile std::uint32_t*>(range.bytes + offset);
    if (*src != expectedValue) {
      ReleaseMappedRange(&range);
      return kIOReturnIOError;
    }
  }

  ReleaseMappedRange(&range);
  return kIOReturnSuccess;
}

kern_return_t ValidateStage(const rtxmac::nvidia::PraminStagePlan& stage,
                            std::uint64_t sourceBytes) noexcept {
  using namespace rtxmac::nvidia;
  if (!stage.valid || stage.vramSize == 0u || stage.totalBytes == 0u ||
      stage.chunks.empty() || sourceBytes != stage.totalBytes ||
      stage.vramOffset >= stage.vramSize ||
      stage.totalBytes > stage.vramSize - stage.vramOffset) {
    return kIOReturnBadArgument;
  }
  return kIOReturnSuccess;
}
} // namespace

kern_return_t RTXMacStagePramin(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    const void* source,
    std::uint64_t sourceBytes,
    bool writesEnabled) noexcept {
  using namespace rtxmac::nvidia;

  if (!writesEnabled) return kIOReturnNotPermitted;
  if (!bar0 || !source) return kIOReturnBadArgument;
  kern_return_t validation = ValidateStage(stage, sourceBytes);
  if (validation != kIOReturnSuccess) return validation;

  const auto* sourceBytesPtr = static_cast<const std::uint8_t*>(source);
  std::uint64_t expectedVram = stage.vramOffset;
  std::uint64_t expectedSource = 0u;

  for (const auto& chunk : stage.chunks) {
    if (chunk.bytes == 0u || (chunk.bytes & 3u) != 0u ||
        chunk.vramOffset != expectedVram || chunk.sourceOffset != expectedSource ||
        chunk.sourceOffset > sourceBytes || chunk.bytes > sourceBytes - chunk.sourceOffset) {
      return kIOReturnBadArgument;
    }

    const std::uint64_t expectedWindow = chunk.vramOffset & ~kPraminWindowMask;
    const std::uint64_t inWindow = chunk.vramOffset & kPraminWindowMask;
    const std::uint64_t room = kPraminWindowBytes - inWindow;
    const std::uint64_t selector64 = expectedWindow >> 16u;
    const std::uint64_t aperture64 =
        static_cast<std::uint64_t>(kPraminApertureOffset) + inWindow;

    if (chunk.bytes > room ||
        selector64 > std::numeric_limits<std::uint32_t>::max() ||
        aperture64 > std::numeric_limits<std::uint32_t>::max() ||
        chunk.windowBase != expectedWindow ||
        chunk.windowSelector != static_cast<std::uint32_t>(selector64) ||
        chunk.bar0ApertureOffset != static_cast<std::uint32_t>(aperture64)) {
      return kIOReturnBadArgument;
    }

    kern_return_t kr = WriteSelector(bar0, bar0Size, chunk.windowSelector);
    if (kr != kIOReturnSuccess) return kr;

    kr = WriteAperture(bar0, bar0Size, chunk.bar0ApertureOffset,
                       sourceBytesPtr + chunk.sourceOffset, chunk.bytes);
    if (kr != kIOReturnSuccess) return kr;

    if (expectedVram > std::numeric_limits<std::uint64_t>::max() - chunk.bytes ||
        expectedSource > std::numeric_limits<std::uint64_t>::max() - chunk.bytes) {
      return kIOReturnBadArgument;
    }
    expectedVram += chunk.bytes;
    expectedSource += chunk.bytes;
  }

  if (expectedSource != stage.totalBytes ||
      expectedVram != stage.vramOffset + stage.totalBytes) {
    return kIOReturnBadArgument;
  }
  return kIOReturnSuccess;
}

kern_return_t RTXMacVerifyPramin(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    const void* expected,
    std::uint64_t expectedBytes,
    bool writesEnabled) noexcept {
  using namespace rtxmac::nvidia;

  if (!writesEnabled) return kIOReturnNotPermitted;
  if (!bar0 || !expected) return kIOReturnBadArgument;
  kern_return_t validation = ValidateStage(stage, expectedBytes);
  if (validation != kIOReturnSuccess) return validation;

  const auto* expectedPtr = static_cast<const std::uint8_t*>(expected);
  std::uint64_t expectedVram = stage.vramOffset;
  std::uint64_t expectedSource = 0u;

  for (const auto& chunk : stage.chunks) {
    if (chunk.bytes == 0u || (chunk.bytes & 3u) != 0u ||
        chunk.vramOffset != expectedVram || chunk.sourceOffset != expectedSource ||
        chunk.sourceOffset > expectedBytes || chunk.bytes > expectedBytes - chunk.sourceOffset) {
      return kIOReturnBadArgument;
    }

    const std::uint64_t expectedWindow = chunk.vramOffset & ~kPraminWindowMask;
    const std::uint64_t inWindow = chunk.vramOffset & kPraminWindowMask;
    const std::uint64_t selector64 = expectedWindow >> 16u;
    const std::uint64_t aperture64 =
        static_cast<std::uint64_t>(kPraminApertureOffset) + inWindow;
    if (chunk.bytes > kPraminWindowBytes - inWindow ||
        selector64 > std::numeric_limits<std::uint32_t>::max() ||
        aperture64 > std::numeric_limits<std::uint32_t>::max() ||
        chunk.windowBase != expectedWindow ||
        chunk.windowSelector != static_cast<std::uint32_t>(selector64) ||
        chunk.bar0ApertureOffset != static_cast<std::uint32_t>(aperture64)) {
      return kIOReturnBadArgument;
    }

    kern_return_t kr = WriteSelector(bar0, bar0Size, chunk.windowSelector);
    if (kr != kIOReturnSuccess) return kr;
    kr = CompareAperture(bar0, bar0Size, chunk.bar0ApertureOffset,
                         expectedPtr + chunk.sourceOffset, chunk.bytes);
    if (kr != kIOReturnSuccess) return kr;

    expectedVram += chunk.bytes;
    expectedSource += chunk.bytes;
  }

  if (expectedSource != stage.totalBytes ||
      expectedVram != stage.vramOffset + stage.totalBytes) {
    return kIOReturnBadArgument;
  }
  return kIOReturnSuccess;
}
