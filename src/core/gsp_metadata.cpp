#include "rtxmac/gsp_metadata.hpp"

#include <limits>
#include <span>

namespace rtxmac::nvidia::gsp {
namespace {

constexpr std::uint64_t kKiB128 = 0x20000ull;
constexpr std::uint64_t kKiB64 = 0x10000ull;
constexpr std::uint64_t kKiB4 = 0x1000ull;
constexpr std::uint64_t kMiB = 0x100000ull;

constexpr std::uint64_t AlignDown(std::uint64_t value, std::uint64_t alignment) noexcept {
  return alignment == 0u ? 0u : value - (value % alignment);
}

std::optional<std::uint64_t> AlignUp(std::uint64_t value, std::uint64_t alignment) noexcept {
  if (alignment == 0u) return std::nullopt;
  const auto rem = value % alignment;
  if (rem == 0u) return value;
  const auto add = alignment - rem;
  if (value > std::numeric_limits<std::uint64_t>::max() - add) return std::nullopt;
  return value + add;
}

bool Sub(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (a < b) return false;
  out = a - b;
  return true;
}

void StoreLe32(std::span<std::uint8_t> out, std::size_t off, std::uint32_t value) noexcept {
  out[off + 0] = static_cast<std::uint8_t>(value >> 0u);
  out[off + 1] = static_cast<std::uint8_t>(value >> 8u);
  out[off + 2] = static_cast<std::uint8_t>(value >> 16u);
  out[off + 3] = static_cast<std::uint8_t>(value >> 24u);
}

void StoreLe64(std::span<std::uint8_t> out, std::size_t off, std::uint64_t value) noexcept {
  for (std::size_t i = 0; i < 8u; ++i) out[off + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}

} // namespace

std::optional<WprLayout> PlanWprLayout(const WprLayoutInputs& in) noexcept {
  if (in.fbSize == 0u || in.vgaWorkspaceOffset > in.fbSize || in.vbiosReservedOffset > in.fbSize) return std::nullopt;
  if (in.bootloaderSize == 0u || in.radix3ElfSize == 0u || in.requestedWprHeapSize == 0u) return std::nullopt;
  if (in.vbiosReservedOffset < in.wprEndMargin) return std::nullopt;

  WprLayout out{};
  out.fbSize = in.fbSize;
  out.vgaWorkspaceOffset = in.vgaWorkspaceOffset;
  out.vgaWorkspaceSize = in.fbSize - in.vgaWorkspaceOffset;
  out.frtsSize = in.frtsSize;
  out.sizeOfBootloader = in.bootloaderSize;
  out.sizeOfRadix3Elf = in.radix3ElfSize;

  std::uint64_t cursor = in.vbiosReservedOffset - in.wprEndMargin;
  out.gspFwWprEnd = AlignDown(cursor, kKiB128);

  if (!Sub(out.gspFwWprEnd, in.frtsSize, out.frtsOffset)) return std::nullopt;
  if (!Sub(out.frtsOffset, in.bootloaderSize, cursor)) return std::nullopt;
  out.bootBinOffset = AlignDown(cursor, kKiB4);
  if (!Sub(out.bootBinOffset, in.radix3ElfSize, cursor)) return std::nullopt;
  out.gspFwOffset = AlignDown(cursor, kKiB64);

  // NVIDIA's TU102 HAL aligns both heap classes to MiB boundaries. The WPR
  // heap request itself comes from kgspGetFwHeapSize(), which remains an
  // explicit input here instead of being guessed.
  const auto nonWprAligned = AlignUp(in.nonWprHeapSize, kMiB);
  if (!nonWprAligned) return std::nullopt;
  out.nonWprHeapSize = *nonWprAligned;

  if (!Sub(out.gspFwOffset, in.requestedWprHeapSize, cursor)) return std::nullopt;
  out.gspFwHeapOffset = AlignDown(cursor, kMiB);
  out.gspFwHeapSize = AlignDown(out.gspFwOffset - out.gspFwHeapOffset, kMiB);
  if (out.gspFwHeapSize == 0u) return std::nullopt;

  // sizeof(GspFwWprMeta) is 256, rounded to one MiB by the production HAL.
  if (!Sub(out.gspFwHeapOffset, kMiB, out.gspFwWprStart)) return std::nullopt;
  if (!Sub(out.gspFwWprStart, out.nonWprHeapSize, out.nonWprHeapOffset)) return std::nullopt;
  out.gspFwRsvdStart = out.nonWprHeapOffset;

  if (!(out.gspFwRsvdStart <= out.gspFwWprStart &&
        out.gspFwWprStart <= out.gspFwHeapOffset &&
        out.gspFwHeapOffset < out.gspFwOffset &&
        out.gspFwOffset < out.bootBinOffset &&
        out.bootBinOffset <= out.frtsOffset &&
        out.frtsOffset <= out.gspFwWprEnd &&
        out.gspFwWprEnd <= in.vbiosReservedOffset &&
        in.vbiosReservedOffset <= in.fbSize)) return std::nullopt;

  return out;
}

std::array<std::uint8_t, kWprMetaBytes> BuildWprMeta(const WprMetaInputs& in) noexcept {
  std::array<std::uint8_t, kWprMetaBytes> out{};
  auto b = std::span<std::uint8_t>(out);

  StoreLe64(b, 0u, kWprMetaMagic);
  StoreLe64(b, 8u, kWprMetaRevision);
  StoreLe64(b, 16u, in.sysmemAddrOfRadix3Elf);
  StoreLe64(b, 24u, in.layout.sizeOfRadix3Elf);
  StoreLe64(b, 32u, in.sysmemAddrOfBootloader);
  StoreLe64(b, 40u, in.layout.sizeOfBootloader);
  StoreLe64(b, 48u, in.bootloaderCodeOffset);
  StoreLe64(b, 56u, in.bootloaderDataOffset);
  StoreLe64(b, 64u, in.bootloaderManifestOffset);
  StoreLe64(b, 72u, in.sysmemAddrOfSignature);
  StoreLe64(b, 80u, in.sizeOfSignature);

  StoreLe64(b, 88u, in.layout.gspFwRsvdStart);
  StoreLe64(b, 96u, in.layout.nonWprHeapOffset);
  StoreLe64(b, 104u, in.layout.nonWprHeapSize);
  StoreLe64(b, 112u, in.layout.gspFwWprStart);
  StoreLe64(b, 120u, in.layout.gspFwHeapOffset);
  StoreLe64(b, 128u, in.layout.gspFwHeapSize);
  StoreLe64(b, 136u, in.layout.gspFwOffset);
  StoreLe64(b, 144u, in.layout.bootBinOffset);
  StoreLe64(b, 152u, in.layout.frtsOffset);
  StoreLe64(b, 160u, in.layout.frtsSize);
  StoreLe64(b, 168u, in.layout.gspFwWprEnd);
  StoreLe64(b, 176u, in.layout.fbSize);
  StoreLe64(b, 184u, in.layout.vgaWorkspaceOffset);
  StoreLe64(b, 192u, in.layout.vgaWorkspaceSize);
  StoreLe64(b, 200u, 0u); // bootCount

  // The partition-RPC / CrashCat union at 208..239 stays zero initially.
  b[240] = in.gspFwHeapVfPartitionCount;
  b[241] = in.flags;
  StoreLe32(b, 244u, in.pmuReservedSize);
  StoreLe64(b, 248u, 0u); // verified: Booter owns this transition.
  return out;
}

std::optional<std::uint64_t> MakeLibosId8(std::string_view id) noexcept {
  if (id.empty() || id.size() > 8u) return std::nullopt;
  std::uint64_t out = 0u;
  for (const unsigned char c : id) out = (out << 8u) | static_cast<std::uint64_t>(c);
  return out;
}

std::optional<std::array<std::uint8_t, kLibosInitPageBytes>> BuildLibosInitPage(
    std::span<const LibosRegion> regions) noexcept {
  if (regions.size() > kLibosRegionCapacity) return std::nullopt;

  std::array<std::uint8_t, kLibosInitPageBytes> out{};
  auto b = std::span<std::uint8_t>(out);
  for (std::size_t i = 0; i < regions.size(); ++i) {
    const auto id8 = MakeLibosId8(regions[i].id);
    if (!id8 || regions[i].size == 0u) return std::nullopt;
    const std::size_t off = i * kLibosRegionBytes;
    StoreLe64(b, off + 0u, *id8);
    StoreLe64(b, off + 8u, regions[i].physicalAddress);
    StoreLe64(b, off + 16u, regions[i].size);
    b[off + 24u] = regions[i].kind;
    b[off + 25u] = regions[i].location;
    // 26..31 are natural ABI padding and remain zero.
  }
  return out;
}

} // namespace rtxmac::nvidia::gsp
