#pragma once

#include "rtxmac/gsp_boot.hpp"
#include "rtxmac/gsp_rpc.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rtxmac::nvidia::gsp {

inline constexpr std::size_t kGspSystemInfo570Bytes = 928u;
inline constexpr std::uint32_t kRpcFunctionGspSetSystemInfo = 72u;
inline constexpr std::uint32_t kRpcFunctionSetRegistry = 73u;
inline constexpr std::uint32_t kBootstrapMessageSize = 0x1000u;
inline constexpr std::uint32_t kBootstrapEntryOffset = 0x1000u;
inline constexpr std::uint32_t kQueueTxHeaderBytes = 32u;

struct GspSystemInfoInputs {
  std::uint64_t bar0Physical{};
  std::uint64_t bar1Physical{};
  std::uint64_t bar3Physical{};
  std::uint64_t domainBusDeviceFunction{};
  std::uint64_t maxUserVa{0x7ffffffff000ull};
  std::uint32_t pciConfigMirrorBase{0x88000u};
  std::uint32_t pciConfigMirrorSize{0x1000u};
  // PCI config DWORD at 0x00: device ID in high 16, vendor ID in low 16.
  std::uint32_t pciDeviceIdDword{};
  // PCI config DWORD at 0x2c: subsystem device in high 16, subsystem vendor in low 16.
  std::uint32_t pciSubDeviceIdDword{};
  std::uint32_t pciRevisionId{};
  bool passthrough{true};
};

[[nodiscard]] std::optional<std::uint64_t> EncodePciBdf(
    std::uint32_t bus, std::uint32_t device, std::uint32_t function) noexcept;

// 570.144 ABI serializer. Unspecified GspSystemInfo fields are zero.
[[nodiscard]] std::array<std::uint8_t, kGspSystemInfo570Bytes> BuildGspSystemInfo570(
    const GspSystemInfoInputs& in) noexcept;

struct RegistryDwordEntry {
  std::string_view name;
  std::uint32_t value{};
};

[[nodiscard]] std::optional<std::vector<std::uint8_t>> BuildRegistryTable(
    std::span<const RegistryDwordEntry> entries);

[[nodiscard]] std::array<RegistryDwordEntry, 2> DefaultBootstrapRegistry() noexcept;

struct BootstrapQueueImage {
  std::vector<std::uint8_t> bytes;
  std::array<RpcRecord, 2> records;
  std::uint32_t finalWritePointer{};
};

// Builds the host-owned command queue exactly as it should look after the two
// pre-boot RPC records have been written. No shared memory is touched.
[[nodiscard]] std::optional<BootstrapQueueImage> BuildBootstrapCommandQueue(
    const QueueMemoryLayout& layout,
    const GspSystemInfoInputs& systemInfo,
    std::span<const RegistryDwordEntry> registry);

} // namespace rtxmac::nvidia::gsp
