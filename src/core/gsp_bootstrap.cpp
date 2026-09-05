#include "rtxmac/gsp_bootstrap.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace rtxmac::nvidia::gsp {
namespace {

void StoreLe32(std::span<std::uint8_t> out, std::size_t off, std::uint32_t value) noexcept {
  out[off + 0u] = static_cast<std::uint8_t>(value >> 0u);
  out[off + 1u] = static_cast<std::uint8_t>(value >> 8u);
  out[off + 2u] = static_cast<std::uint8_t>(value >> 16u);
  out[off + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

void StoreLe64(std::span<std::uint8_t> out, std::size_t off, std::uint64_t value) noexcept {
  for (std::size_t i = 0; i < 8u; ++i) out[off + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}

bool PutRecord(std::vector<std::uint8_t>& queue, std::uint32_t msgCount,
               std::uint32_t& writePtr, const RpcRecord& record) {
  if (msgCount == 0u || record.elementCount == 0u || record.elementCount > msgCount) return false;
  const std::uint64_t ringBytes = static_cast<std::uint64_t>(msgCount) * kBootstrapMessageSize;
  const std::uint64_t ringOffset = static_cast<std::uint64_t>(writePtr) * kBootstrapMessageSize;
  if (record.bytes.size() > ringBytes || kBootstrapEntryOffset + ringBytes > queue.size()) return false;
  const std::size_t first = static_cast<std::size_t>(std::min<std::uint64_t>(record.bytes.size(), ringBytes - ringOffset));
  std::copy_n(record.bytes.begin(), first, queue.begin() + kBootstrapEntryOffset + static_cast<std::size_t>(ringOffset));
  if (first < record.bytes.size())
    std::copy(record.bytes.begin() + static_cast<std::ptrdiff_t>(first), record.bytes.end(), queue.begin() + kBootstrapEntryOffset);
  writePtr = (writePtr + record.elementCount) % msgCount;
  return true;
}

} // namespace

std::optional<std::uint64_t> EncodePciBdf(
    std::uint32_t bus, std::uint32_t device, std::uint32_t function) noexcept {
  if (bus > 0xFFu || device > 0x1Fu || function > 0x7u) return std::nullopt;
  return (static_cast<std::uint64_t>(bus) << 8u) |
      (static_cast<std::uint64_t>(device) << 3u) | function;
}

std::array<std::uint8_t, kGspSystemInfo570Bytes> BuildGspSystemInfo570(
    const GspSystemInfoInputs& in) noexcept {
  std::array<std::uint8_t, kGspSystemInfo570Bytes> out{};
  auto b = std::span<std::uint8_t>(out);
  StoreLe64(b, 0u, in.bar0Physical);
  StoreLe64(b, 8u, in.bar1Physical);
  StoreLe64(b, 16u, in.bar3Physical);
  StoreLe64(b, 32u, in.domainBusDeviceFunction);
  StoreLe64(b, 72u, in.maxUserVa);
  StoreLe32(b, 80u, in.pciConfigMirrorBase);
  StoreLe32(b, 84u, in.pciConfigMirrorSize);
  StoreLe32(b, 88u, in.pciDeviceIdDword);
  StoreLe32(b, 92u, in.pciSubDeviceIdDword);
  StoreLe32(b, 96u, in.pciRevisionId);
  out[840u] = in.passthrough ? 1u : 0u;
  return out;
}

std::optional<std::vector<std::uint8_t>> BuildRegistryTable(
    std::span<const RegistryDwordEntry> entries) {
  constexpr std::uint64_t headerBytes = 8u;
  constexpr std::uint64_t entryBytes = 16u;
  if (entries.size() > (std::numeric_limits<std::uint32_t>::max() - headerBytes) / entryBytes) return std::nullopt;

  std::uint64_t stringsBytes = 0u;
  for (const auto& e : entries) {
    if (e.name.empty() || e.name.find('\0') != std::string_view::npos) return std::nullopt;
    if (stringsBytes > std::numeric_limits<std::uint32_t>::max() - e.name.size() - 1u) return std::nullopt;
    stringsBytes += e.name.size() + 1u;
  }
  const std::uint64_t entriesBytes = entries.size() * entryBytes;
  const std::uint64_t total = headerBytes + entriesBytes + stringsBytes;
  if (total > std::numeric_limits<std::uint32_t>::max() || total > std::numeric_limits<std::size_t>::max()) return std::nullopt;

  std::vector<std::uint8_t> out(static_cast<std::size_t>(total), 0u);
  auto b = std::span<std::uint8_t>(out);
  StoreLe32(b, 0u, static_cast<std::uint32_t>(total));
  StoreLe32(b, 4u, static_cast<std::uint32_t>(entries.size()));

  std::size_t stringCursor = static_cast<std::size_t>(headerBytes + entriesBytes);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const std::size_t off = static_cast<std::size_t>(headerBytes + i * entryBytes);
    StoreLe32(b, off + 0u, static_cast<std::uint32_t>(stringCursor));
    b[off + 4u] = 1u; // REGISTRY_TABLE_ENTRY_TYPE_DWORD
    StoreLe32(b, off + 8u, entries[i].value);
    StoreLe32(b, off + 12u, 4u);
    std::copy(entries[i].name.begin(), entries[i].name.end(), out.begin() + static_cast<std::ptrdiff_t>(stringCursor));
    stringCursor += entries[i].name.size();
    out[stringCursor++] = 0u;
  }
  return out;
}

std::array<RegistryDwordEntry, 2> DefaultBootstrapRegistry() noexcept {
  return {{{"RMForcePcieConfigSave", 1u}, {"RMSecBusResetEnable", 1u}}};
}

std::optional<BootstrapQueueImage> BuildBootstrapCommandQueue(
    const QueueMemoryLayout& layout,
    const GspSystemInfoInputs& systemInfo,
    std::span<const RegistryDwordEntry> registry) {
  if (layout.queueBytes > std::numeric_limits<std::uint32_t>::max() ||
      layout.queueBytes < 3u * kBootstrapMessageSize ||
      layout.queueBytes > std::numeric_limits<std::size_t>::max() ||
      (layout.queueBytes % kBootstrapMessageSize) != 0u) return std::nullopt;

  const std::uint32_t msgCount = static_cast<std::uint32_t>((layout.queueBytes - kBootstrapEntryOffset) / kBootstrapMessageSize);
  if (msgCount < 2u) return std::nullopt;
  const auto reg = BuildRegistryTable(registry);
  if (!reg) return std::nullopt;
  const auto sys = BuildGspSystemInfo570(systemInfo);

  const auto sysRecord = BuildRpcRecord(kRpcFunctionGspSetSystemInfo, sys, 0u, kBootstrapMessageSize);
  const auto regRecord = BuildRpcRecord(kRpcFunctionSetRegistry, *reg, 1u, kBootstrapMessageSize);
  if (!sysRecord || !regRecord) return std::nullopt;

  BootstrapQueueImage out{};
  out.bytes.assign(static_cast<std::size_t>(layout.queueBytes), 0u);
  auto b = std::span<std::uint8_t>(out.bytes);
  StoreLe32(b, 0u, 0u); // MSGQ_VERSION
  StoreLe32(b, 4u, static_cast<std::uint32_t>(layout.queueBytes));
  StoreLe32(b, 8u, kBootstrapMessageSize);
  StoreLe32(b, 12u, msgCount);
  StoreLe32(b, 16u, 0u); // writePtr before prefill
  StoreLe32(b, 20u, 1u); // swap RX
  StoreLe32(b, 24u, kQueueTxHeaderBytes);
  StoreLe32(b, 28u, kBootstrapEntryOffset);

  std::uint32_t wp = 0u;
  if (!PutRecord(out.bytes, msgCount, wp, *sysRecord) || !PutRecord(out.bytes, msgCount, wp, *regRecord)) return std::nullopt;
  StoreLe32(b, 16u, wp);
  out.records = {*sysRecord, *regRecord};
  out.finalWritePointer = wp;
  return out;
}

std::optional<SharedQueueAllocationImage> BuildSharedQueueAllocationImage(
    const QueueMemoryLayout& layout,
    std::span<const std::uint64_t> dmaPageAddresses,
    const GspSystemInfoInputs& systemInfo,
    std::span<const RegistryDwordEntry> registry) {
  if (dmaPageAddresses.empty()) return std::nullopt;
  if (layout.totalBytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) return std::nullopt;
  if (layout.commandQueueOffset > layout.totalBytes ||
      layout.queueBytes > layout.totalBytes - layout.commandQueueOffset) return std::nullopt;
  if (layout.statusQueueOffset > layout.totalBytes ||
      layout.queueBytes > layout.totalBytes - layout.statusQueueOffset) return std::nullopt;

  const auto pageTable = BuildQueuePageTable(layout, dmaPageAddresses);
  const auto commandQueue = BuildBootstrapCommandQueue(layout, systemInfo, registry);
  if (!pageTable || !commandQueue) return std::nullopt;
  if (pageTable->size() != layout.pageTableBytes || commandQueue->bytes.size() != layout.queueBytes) return std::nullopt;

  SharedQueueAllocationImage out{};
  out.bytes.assign(static_cast<std::size_t>(layout.totalBytes), 0u);
  std::copy(pageTable->begin(), pageTable->end(), out.bytes.begin());
  std::copy(commandQueue->bytes.begin(), commandQueue->bytes.end(),
            out.bytes.begin() + static_cast<std::ptrdiff_t>(layout.commandQueueOffset));

  // The status queue remains all-zero because GSP-RM owns its transmit header.
  out.cachedArguments = BuildCachedArguments(layout, dmaPageAddresses[0]);
  out.commandQueue = *commandQueue;
  return out;
}

} // namespace rtxmac::nvidia::gsp
