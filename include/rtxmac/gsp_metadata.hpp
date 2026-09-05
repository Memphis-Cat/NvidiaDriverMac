#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace rtxmac::nvidia::gsp {

inline constexpr std::uint64_t kWprMetaMagic = 0xdc3aae21371a60b3ull;
inline constexpr std::uint64_t kWprMetaRevision = 1ull;
inline constexpr std::uint64_t kWprMetaVerified = 0xa0a0a0a0a0a0a0a0ull;
inline constexpr std::size_t kWprMetaBytes = 256u;
inline constexpr std::size_t kLibosInitPageBytes = 4096u;
inline constexpr std::size_t kLibosRegionBytes = 32u;
inline constexpr std::size_t kLibosRegionCapacity = kLibosInitPageBytes / kLibosRegionBytes;

struct WprLayoutInputs {
  std::uint64_t fbSize{};
  std::uint64_t vgaWorkspaceOffset{};
  std::uint64_t vbiosReservedOffset{};
  std::uint64_t wprEndMargin{};
  std::uint64_t frtsSize{};
  std::uint64_t bootloaderSize{};
  std::uint64_t radix3ElfSize{};
  std::uint64_t nonWprHeapSize{};
  std::uint64_t requestedWprHeapSize{};
};

struct WprLayout {
  std::uint64_t fbSize{};
  std::uint64_t vgaWorkspaceOffset{};
  std::uint64_t vgaWorkspaceSize{};
  std::uint64_t gspFwRsvdStart{};
  std::uint64_t nonWprHeapOffset{};
  std::uint64_t nonWprHeapSize{};
  std::uint64_t gspFwWprStart{};
  std::uint64_t gspFwHeapOffset{};
  std::uint64_t gspFwHeapSize{};
  std::uint64_t gspFwOffset{};
  std::uint64_t bootBinOffset{};
  std::uint64_t frtsOffset{};
  std::uint64_t frtsSize{};
  std::uint64_t gspFwWprEnd{};
  std::uint64_t sizeOfRadix3Elf{};
  std::uint64_t sizeOfBootloader{};
};

// Mirrors the TU102 WPR layout HAL used by GA104, but requires all dynamic
// sizes/offsets to be supplied by the caller. No hardware state is inferred.
[[nodiscard]] std::optional<WprLayout> PlanWprLayout(const WprLayoutInputs& in) noexcept;

struct WprMetaInputs {
  WprLayout layout{};
  std::uint64_t sysmemAddrOfRadix3Elf{};
  std::uint64_t sysmemAddrOfBootloader{};
  std::uint64_t bootloaderCodeOffset{};
  std::uint64_t bootloaderDataOffset{};
  std::uint64_t bootloaderManifestOffset{};
  std::uint64_t sysmemAddrOfSignature{};
  std::uint64_t sizeOfSignature{};
  std::uint8_t gspFwHeapVfPartitionCount{};
  std::uint8_t flags{};
  std::uint32_t pmuReservedSize{};
};

// Serializes the initial-boot form of NVIDIA's 256-byte GspFwWprMeta.
// bootCount and verified are intentionally zero.
[[nodiscard]] std::array<std::uint8_t, kWprMetaBytes> BuildWprMeta(const WprMetaInputs& in) noexcept;

struct LibosRegion {
  std::string_view id;
  std::uint64_t physicalAddress{};
  std::uint64_t size{};
  std::uint8_t kind{1u}; // LIBOS_MEMORY_REGION_CONTIGUOUS
  std::uint8_t location{1u}; // LIBOS_MEMORY_REGION_LOC_SYSMEM
};

[[nodiscard]] std::optional<std::uint64_t> MakeLibosId8(std::string_view id) noexcept;

// Serializes a zero-padded 4K LIBOS init page. Each region occupies 32 bytes
// using the ABI from libos_init_args.h; unused entries remain zero.
[[nodiscard]] std::optional<std::array<std::uint8_t, kLibosInitPageBytes>> BuildLibosInitPage(
    std::span<const LibosRegion> regions) noexcept;

} // namespace rtxmac::nvidia::gsp
