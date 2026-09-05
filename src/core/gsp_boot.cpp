#include "rtxmac/gsp_boot.hpp"

#include <limits>
#include <span>

namespace rtxmac::nvidia::gsp {
namespace {

constexpr std::uint64_t RoundUp(std::uint64_t value, std::uint64_t alignment) noexcept {
  if (alignment == 0u) return 0u;
  const std::uint64_t rem = value % alignment;
  return rem == 0u ? value : value + (alignment - rem);
}

void StoreLe32(std::span<std::uint8_t> out, std::size_t off, std::uint32_t value) noexcept {
  out[off + 0] = static_cast<std::uint8_t>(value >> 0u);
  out[off + 1] = static_cast<std::uint8_t>(value >> 8u);
  out[off + 2] = static_cast<std::uint8_t>(value >> 16u);
  out[off + 3] = static_cast<std::uint8_t>(value >> 24u);
}

void StoreLe64(std::span<std::uint8_t> out, std::size_t off, std::uint64_t value) noexcept {
  for (std::size_t i = 0; i < 8u; ++i) {
    out[off + i] = static_cast<std::uint8_t>(value >> (i * 8u));
  }
}

} // namespace

std::optional<QueueMemoryLayout> PlanQueueMemory(
    std::uint64_t queueBytes,
    std::uint64_t pageBytes,
    std::uint64_t pteBytes) noexcept {
  if (queueBytes == 0u || pageBytes == 0u || pteBytes == 0u) return std::nullopt;
  if ((queueBytes % pageBytes) != 0u) return std::nullopt;
  if (queueBytes > std::numeric_limits<std::uint64_t>::max() / 2u) return std::nullopt;

  const std::uint64_t queuesTotal = queueBytes * 2u;
  const std::uint64_t queuePteCount = queuesTotal / pageBytes;
  if (queuePteCount > std::numeric_limits<std::uint64_t>::max() / pteBytes) return std::nullopt;

  // This mirrors the current GSP queue construction: account for the PTEs
  // that map the two queues plus the page(s) occupied by those PTEs.
  const std::uint64_t queuePteBytes = queuePteCount * pteBytes;
  if (queuePteBytes > std::numeric_limits<std::uint64_t>::max() - (pageBytes - 1u)) return std::nullopt;
  const std::uint64_t pteStorageBytes = RoundUp(queuePteBytes, pageBytes);
  const std::uint64_t pteStoragePages = pteStorageBytes / pageBytes;
  const std::uint64_t pteCount = queuePteCount + pteStoragePages;
  if (pteCount > std::numeric_limits<std::uint64_t>::max() / pteBytes) return std::nullopt;

  const std::uint64_t rawPtBytes = pteCount * pteBytes;
  if (rawPtBytes > std::numeric_limits<std::uint64_t>::max() - (pageBytes - 1u)) return std::nullopt;
  const std::uint64_t ptBytes = RoundUp(rawPtBytes, pageBytes);
  if (ptBytes > std::numeric_limits<std::uint64_t>::max() - queuesTotal) return std::nullopt;

  return QueueMemoryLayout{
    .queueBytes = queueBytes,
    .pageBytes = pageBytes,
    .pteBytes = pteBytes,
    .queuePageCount = queuePteCount,
    .pageTableEntryCount = pteCount,
    .pageTableBytes = ptBytes,
    .commandQueueOffset = ptBytes,
    .statusQueueOffset = ptBytes + queueBytes,
    .totalBytes = ptBytes + queuesTotal,
  };
}

std::optional<std::vector<std::uint8_t>> BuildQueuePageTable(
    const QueueMemoryLayout& layout,
    std::span<const std::uint64_t> dmaPageAddresses) {
  if (layout.pageBytes == 0u || layout.pteBytes != sizeof(std::uint64_t)) return std::nullopt;
  if (layout.pageTableEntryCount != dmaPageAddresses.size()) return std::nullopt;
  if (layout.pageTableBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) return std::nullopt;
  if (layout.pageTableEntryCount > std::numeric_limits<std::uint64_t>::max() / layout.pteBytes) return std::nullopt;

  const std::uint64_t usedBytes = layout.pageTableEntryCount * layout.pteBytes;
  if (usedBytes > layout.pageTableBytes) return std::nullopt;

  std::vector<std::uint8_t> out(static_cast<std::size_t>(layout.pageTableBytes), 0u);
  auto bytes = std::span<std::uint8_t>(out);
  for (std::size_t i = 0; i < dmaPageAddresses.size(); ++i) {
    const std::uint64_t address = dmaPageAddresses[i];
    if ((address % layout.pageBytes) != 0u) return std::nullopt;
    StoreLe64(bytes, i * sizeof(std::uint64_t), address);
  }
  return out;
}

std::array<std::uint8_t, kGspArgumentsCachedBytes> BuildCachedArguments(
    const QueueMemoryLayout& layout,
    std::uint64_t sharedMemPhysAddr) noexcept {
  std::array<std::uint8_t, kGspArgumentsCachedBytes> out{};
  auto bytes = std::span<std::uint8_t>(out);

  // MESSAGE_QUEUE_INIT_ARGUMENTS @ offset 0, size 32.
  StoreLe64(bytes, 0u, sharedMemPhysAddr);
  StoreLe32(bytes, 8u, static_cast<std::uint32_t>(layout.pageTableEntryCount));
  StoreLe64(bytes, 16u, layout.commandQueueOffset);
  StoreLe64(bytes, 24u, layout.statusQueueOffset);

  // GSP_ARGUMENTS_CACHED::bDmemStack @ offset 48 (NvBool/u32).
  StoreLe32(bytes, 48u, 1u);
  return out;
}

} // namespace rtxmac::nvidia::gsp
