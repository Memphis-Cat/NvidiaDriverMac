#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rtxmac::nvidia::gsp {

inline constexpr std::uint32_t kRpcHeaderVersion3 = 3u << 24u;
inline constexpr std::uint32_t kRpcSignatureValid = 0x43505256u;
inline constexpr std::uint32_t kRpcPending = 0xFFFFFFFFu;
inline constexpr std::size_t kQueueElementHeaderBytes = 48u;
inline constexpr std::size_t kRpcHeaderBytes = 32u;
inline constexpr std::size_t kProtocolOverheadBytes = kQueueElementHeaderBytes + kRpcHeaderBytes;

struct RpcFragment {
  std::size_t payloadOffset{};
  std::size_t payloadBytes{};
  bool continuation{};
};

struct RpcRecord {
  std::vector<std::uint8_t> bytes;
  std::uint32_t checksum{};
  std::uint32_t sequence{};
  std::uint32_t elementCount{};
  std::uint32_t function{};
  std::size_t payloadBytes{};
};

[[nodiscard]] std::uint32_t RpcChecksum(std::span<const std::uint8_t> bytes) noexcept;

// Plan payload fragmentation using the same maximum-elements concept used by
// NVIDIA GSP message queues. This function is transport-only: callers choose
// the continuation RPC function ID when they submit later fragments.
[[nodiscard]] std::vector<RpcFragment> PlanRpcFragments(
    std::size_t payloadBytes,
    std::size_t messageSize,
    std::uint32_t maxElementsPerRecord = 16u);

// Build one queue record. Returns nullopt if messageSize is zero, the record
// would exceed maxElements, or sizes cannot be represented safely.
[[nodiscard]] std::optional<RpcRecord> BuildRpcRecord(
    std::uint32_t function,
    std::span<const std::uint8_t> payload,
    std::uint32_t sequence,
    std::size_t messageSize,
    std::uint32_t maxElements = 16u);

// Structural/checksum validation for an already built record. No hardware or
// queue state is consulted.
[[nodiscard]] bool ValidateRpcRecord(
    std::span<const std::uint8_t> record,
    std::size_t messageSize,
    std::uint32_t maxElements = 16u) noexcept;

} // namespace rtxmac::nvidia::gsp
