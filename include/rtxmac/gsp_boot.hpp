#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rtxmac::nvidia::gsp {

inline constexpr std::uint64_t kDefaultQueueBytes = 0x40000ull;
inline constexpr std::uint64_t kGspPageBytes = 0x1000ull;
inline constexpr std::uint64_t kGspPteBytes = 8ull;
inline constexpr std::size_t kMessageQueueInitArgsBytes = 32u;
inline constexpr std::size_t kGspArgumentsCachedBytes = 72u;

struct QueueMemoryLayout {
  std::uint64_t queueBytes{};
  std::uint64_t pageBytes{};
  std::uint64_t pteBytes{};
  std::uint64_t queuePageCount{};
  std::uint64_t pageTableEntryCount{};
  std::uint64_t pageTableBytes{};
  std::uint64_t commandQueueOffset{};
  std::uint64_t statusQueueOffset{};
  std::uint64_t totalBytes{};
};

[[nodiscard]] std::optional<QueueMemoryLayout> PlanQueueMemory(
    std::uint64_t queueBytes = kDefaultQueueBytes,
    std::uint64_t pageBytes = kGspPageBytes,
    std::uint64_t pteBytes = kGspPteBytes) noexcept;

// Build the shared-memory PTE page used by the GSP message queues. The Ampere
// path stores one raw little-endian 64-bit DMA page address per entry; entry 0
// maps the page-table page itself, followed by the command/status queue pages.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> BuildQueuePageTable(
    const QueueMemoryLayout& layout,
    std::span<const std::uint64_t> dmaPageAddresses);

// Build the 72-byte GSP_ARGUMENTS_CACHED block currently used by the
// Ampere GSP path. Fields not required for initial queue bring-up remain zero.
[[nodiscard]] std::array<std::uint8_t, kGspArgumentsCachedBytes> BuildCachedArguments(
    const QueueMemoryLayout& layout,
    std::uint64_t sharedMemPhysAddr) noexcept;

} // namespace rtxmac::nvidia::gsp
