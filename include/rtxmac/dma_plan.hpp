#pragma once

#include <cstdint>
#include <vector>

namespace rtxmac {

inline constexpr std::uint32_t kDriverKitMaxSegmentsPerPrepare = 32;
inline constexpr std::uint64_t kDefaultDmaPageSize = 4096;

struct DmaChunk {
  std::uint64_t offset{};
  std::uint64_t length{};
};

enum class DmaPlanStatus : std::uint8_t {
  Ok = 0,
  InvalidPageSize,
  SizeOverflow,
};

struct DmaPlan {
  DmaPlanStatus status{DmaPlanStatus::Ok};
  std::uint64_t totalBytes{};
  std::uint64_t pageSize{};
  std::uint32_t segmentLimit{};
  std::vector<DmaChunk> chunks;
};

// Conservative plan: each chunk contains no more than segmentLimit pages.
// Even if every page becomes its own DMA segment, the chunk can fit in one
// DriverKit PrepareForDMA result. Runtime coverage validation is still required.
[[nodiscard]] DmaPlan PlanConservativeDmaChunks(
    std::uint64_t totalBytes,
    std::uint64_t pageSize = kDefaultDmaPageSize,
    std::uint32_t segmentLimit = kDriverKitMaxSegmentsPerPrepare);

} // namespace rtxmac
