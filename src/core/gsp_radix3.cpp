#include "rtxmac/gsp_radix3.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace rtxmac::nvidia::gsp {
namespace {

std::optional<std::uint64_t> CeilDiv(std::uint64_t value, std::uint64_t divisor) noexcept {
  if (divisor == 0u) return std::nullopt;
  if (value == 0u) return 0u;
  return 1u + (value - 1u) / divisor;
}

void StoreLe64(std::vector<std::uint8_t>& out, std::size_t off, std::uint64_t value) noexcept {
  for (std::size_t i = 0; i < 8u; ++i) out[off + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}

} // namespace

std::optional<Radix3Layout> PlanRadix3(std::uint64_t imageBytes) noexcept {
  if (imageBytes == 0u) return std::nullopt;

  Radix3Layout out{};
  out.imageBytes = imageBytes;
  const auto imagePages = CeilDiv(imageBytes, kRadixPageBytes);
  if (!imagePages) return std::nullopt;
  out.pageCounts[3] = *imagePages;
  for (int i = 3; i > 0; --i) {
    const auto parent = CeilDiv(out.pageCounts[static_cast<std::size_t>(i)], kRadixEntriesPerPage);
    if (!parent) return std::nullopt;
    out.pageCounts[static_cast<std::size_t>(i - 1)] = *parent;
  }

  // LibOS is passed one root PA, so reject images so huge that they require
  // multiple root pages rather than silently producing an unusable tree.
  if (out.pageCounts[0] != 1u) return std::nullopt;

  std::uint64_t prefixPages = 0u;
  for (std::size_t i = 0; i < 4u; ++i) {
    if (prefixPages > std::numeric_limits<std::uint64_t>::max() / kRadixPageBytes) return std::nullopt;
    out.offsets[i] = prefixPages * kRadixPageBytes;
    if (i < 3u) {
      if (prefixPages > std::numeric_limits<std::uint64_t>::max() - out.pageCounts[i]) return std::nullopt;
      prefixPages += out.pageCounts[i];
    }
  }
  out.tableBytes = out.offsets[3];
  if (out.tableBytes > std::numeric_limits<std::uint64_t>::max() - imageBytes) return std::nullopt;
  out.allocationBytes = out.tableBytes + imageBytes;
  const auto allocationPages = CeilDiv(out.allocationBytes, kRadixPageBytes);
  if (!allocationPages) return std::nullopt;
  out.allocationPages = *allocationPages;
  return out;
}

std::optional<std::vector<std::uint8_t>> BuildRadix3Tables(
    const Radix3Layout& layout,
    std::span<const std::uint64_t> physicalPages) noexcept {
  if (layout.pageCounts[0] != 1u || layout.tableBytes != layout.offsets[3]) return std::nullopt;
  if (layout.tableBytes > std::numeric_limits<std::size_t>::max()) return std::nullopt;
  if (physicalPages.size() != layout.allocationPages) return std::nullopt;
  for (const auto pa : physicalPages) {
    if ((pa & (kRadixPageBytes - 1u)) != 0u) return std::nullopt;
  }

  std::vector<std::uint8_t> out(static_cast<std::size_t>(layout.tableBytes), 0u);
  std::uint64_t parentStartPage = 0u;
  for (std::size_t level = 0u; level < 3u; ++level) {
    const std::uint64_t childStartPage = parentStartPage + layout.pageCounts[level];
    const std::uint64_t childCount = layout.pageCounts[level + 1u];
    const std::uint64_t parentCapacity = layout.pageCounts[level] * kRadixEntriesPerPage;
    if (childCount > parentCapacity || childStartPage + childCount > physicalPages.size()) return std::nullopt;

    const std::uint64_t tableOffset = layout.offsets[level];
    for (std::uint64_t i = 0u; i < childCount; ++i) {
      const std::uint64_t byteOffset = tableOffset + i * 8u;
      if (byteOffset + 8u > layout.tableBytes) return std::nullopt;
      StoreLe64(out, static_cast<std::size_t>(byteOffset), physicalPages[static_cast<std::size_t>(childStartPage + i)]);
    }
    parentStartPage = childStartPage;
  }
  return out;
}

std::optional<std::vector<std::uint8_t>> BuildRadix3AllocationImage(
    const Radix3Layout& layout,
    std::span<const std::uint64_t> physicalPages,
    std::span<const std::uint8_t> firmwareImage) noexcept {
  if (firmwareImage.size() != layout.imageBytes) return std::nullopt;
  if (layout.allocationPages > std::numeric_limits<std::uint64_t>::max() / kRadixPageBytes) return std::nullopt;
  const std::uint64_t paddedBytes = layout.allocationPages * kRadixPageBytes;
  if (paddedBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) return std::nullopt;
  if (layout.offsets[3] > paddedBytes || layout.imageBytes > paddedBytes - layout.offsets[3]) return std::nullopt;

  const auto tables = BuildRadix3Tables(layout, physicalPages);
  if (!tables || tables->size() != layout.tableBytes) return std::nullopt;

  std::vector<std::uint8_t> out(static_cast<std::size_t>(paddedBytes), 0u);
  std::copy(tables->begin(), tables->end(), out.begin());
  std::copy(firmwareImage.begin(), firmwareImage.end(),
            out.begin() + static_cast<std::ptrdiff_t>(layout.offsets[3]));
  return out;
}

} // namespace rtxmac::nvidia::gsp
