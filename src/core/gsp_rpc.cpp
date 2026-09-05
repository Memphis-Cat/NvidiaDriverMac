#include "rtxmac/gsp_rpc.hpp"

#include <algorithm>
#include <limits>

namespace rtxmac::nvidia::gsp {
namespace {

constexpr std::size_t kChecksumOffset = 32u;
constexpr std::size_t kSequenceOffset = 36u;
constexpr std::size_t kElementCountOffset = 40u;
constexpr std::size_t kRpcOffset = kQueueElementHeaderBytes;
constexpr std::size_t kRpcLengthOffset = kRpcOffset + 8u;

void StoreLe32(std::span<std::uint8_t> out, std::size_t off, std::uint32_t value) {
  out[off + 0] = static_cast<std::uint8_t>(value >> 0u);
  out[off + 1] = static_cast<std::uint8_t>(value >> 8u);
  out[off + 2] = static_cast<std::uint8_t>(value >> 16u);
  out[off + 3] = static_cast<std::uint8_t>(value >> 24u);
}

std::uint32_t LoadLe32(std::span<const std::uint8_t> in, std::size_t off) noexcept {
  return static_cast<std::uint32_t>(in[off + 0]) |
      (static_cast<std::uint32_t>(in[off + 1]) << 8u) |
      (static_cast<std::uint32_t>(in[off + 2]) << 16u) |
      (static_cast<std::uint32_t>(in[off + 3]) << 24u);
}

constexpr std::size_t CeilDiv(std::size_t n, std::size_t d) noexcept {
  return n / d + ((n % d) != 0u ? 1u : 0u);
}

} // namespace

std::uint32_t RpcChecksum(std::span<const std::uint8_t> bytes) noexcept {
  std::uint64_t checksum = 0u;
  for (std::size_t off = 0; off < bytes.size(); off += 8u) {
    std::uint64_t word = 0u;
    const std::size_t count = std::min<std::size_t>(8u, bytes.size() - off);
    for (std::size_t i = 0; i < count; ++i) {
      word |= static_cast<std::uint64_t>(bytes[off + i]) << (i * 8u);
    }
    checksum ^= word;
  }
  return static_cast<std::uint32_t>(checksum) ^
      static_cast<std::uint32_t>(checksum >> 32u);
}

std::vector<RpcFragment> PlanRpcFragments(
    std::size_t payloadBytes,
    std::size_t messageSize,
    std::uint32_t maxElementsPerRecord) {
  std::vector<RpcFragment> out;
  if (messageSize == 0u || maxElementsPerRecord == 0u) return out;
  if (messageSize > std::numeric_limits<std::size_t>::max() / maxElementsPerRecord) return out;

  const std::size_t capacity = messageSize * maxElementsPerRecord;
  if (capacity <= kProtocolOverheadBytes) return out;
  const std::size_t maxPayload = capacity - kProtocolOverheadBytes;

  if (payloadBytes == 0u) {
    out.push_back({0u, 0u, false});
    return out;
  }

  std::size_t offset = 0u;
  while (offset < payloadBytes) {
    const std::size_t count = std::min(maxPayload, payloadBytes - offset);
    out.push_back({offset, count, offset != 0u});
    offset += count;
  }
  return out;
}

std::optional<RpcRecord> BuildRpcRecord(
    std::uint32_t function,
    std::span<const std::uint8_t> payload,
    std::uint32_t sequence,
    std::size_t messageSize,
    std::uint32_t maxElements) {
  if (messageSize == 0u || maxElements == 0u) return std::nullopt;
  if (payload.size() > std::numeric_limits<std::uint32_t>::max() - kRpcHeaderBytes) return std::nullopt;
  if (payload.size() > std::numeric_limits<std::size_t>::max() - kProtocolOverheadBytes) return std::nullopt;

  const std::size_t unpaddedBytes = kProtocolOverheadBytes + payload.size();
  const std::size_t elementCountSz = CeilDiv(unpaddedBytes, messageSize);
  if (elementCountSz == 0u || elementCountSz > maxElements ||
      elementCountSz > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }
  if (elementCountSz > std::numeric_limits<std::size_t>::max() / messageSize) return std::nullopt;

  RpcRecord out{};
  out.sequence = sequence;
  out.elementCount = static_cast<std::uint32_t>(elementCountSz);
  out.function = function;
  out.payloadBytes = payload.size();
  out.bytes.assign(elementCountSz * messageSize, 0u);

  auto bytes = std::span<std::uint8_t>(out.bytes);

  // GSP_MSG_QUEUE_ELEMENT: 16-byte auth tag, 16-byte AAD, checksum,
  // sequence, element count, padding. Auth/AAD/padding remain zero here.
  StoreLe32(bytes, kSequenceOffset, sequence);
  StoreLe32(bytes, kElementCountOffset, out.elementCount);

  // rpc_message_header_v03_00 (32 bytes).
  StoreLe32(bytes, kRpcOffset + 0u,  kRpcHeaderVersion3);
  StoreLe32(bytes, kRpcOffset + 4u,  kRpcSignatureValid);
  StoreLe32(bytes, kRpcOffset + 8u,  static_cast<std::uint32_t>(kRpcHeaderBytes + payload.size()));
  StoreLe32(bytes, kRpcOffset + 12u, function);
  StoreLe32(bytes, kRpcOffset + 16u, kRpcPending);
  StoreLe32(bytes, kRpcOffset + 20u, kRpcPending);
  StoreLe32(bytes, kRpcOffset + 24u, 0u); // RPC header sequence field
  StoreLe32(bytes, kRpcOffset + 28u, 0u); // union/init field

  std::copy(payload.begin(), payload.end(), out.bytes.begin() + kProtocolOverheadBytes);

  out.checksum = RpcChecksum(std::span<const std::uint8_t>(out.bytes.data(), unpaddedBytes));
  StoreLe32(bytes, kChecksumOffset, out.checksum);
  return out;
}

bool ValidateRpcRecord(
    std::span<const std::uint8_t> record,
    std::size_t messageSize,
    std::uint32_t maxElements) noexcept {
  if (messageSize == 0u || maxElements == 0u || record.size() < kProtocolOverheadBytes) return false;
  if (LoadLe32(record, kRpcOffset + 0u) != kRpcHeaderVersion3) return false;
  if (LoadLe32(record, kRpcOffset + 4u) != kRpcSignatureValid) return false;

  const std::uint32_t rpcLength = LoadLe32(record, kRpcLengthOffset);
  if (rpcLength < kRpcHeaderBytes) return false;
  const std::size_t unpaddedBytes = kQueueElementHeaderBytes + static_cast<std::size_t>(rpcLength);
  if (unpaddedBytes > record.size()) return false;

  const std::uint32_t elementCount = LoadLe32(record, kElementCountOffset);
  if (elementCount == 0u || elementCount > maxElements) return false;
  if (elementCount > std::numeric_limits<std::size_t>::max() / messageSize) return false;
  if (static_cast<std::size_t>(elementCount) * messageSize != record.size()) return false;
  if (CeilDiv(unpaddedBytes, messageSize) != elementCount) return false;

  // With the checksum field populated, XOR validation across the unpadded
  // queue element + RPC bytes must reduce to zero.
  return RpcChecksum(record.first(unpaddedBytes)) == 0u;
}

} // namespace rtxmac::nvidia::gsp
