#include "rtxmac/gsp_metadata.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

std::uint32_t LoadLe32(const auto& v, std::size_t off) {
  return static_cast<std::uint32_t>(v[off + 0]) |
      (static_cast<std::uint32_t>(v[off + 1]) << 8u) |
      (static_cast<std::uint32_t>(v[off + 2]) << 16u) |
      (static_cast<std::uint32_t>(v[off + 3]) << 24u);
}

std::uint64_t LoadLe64(const auto& v, std::size_t off) {
  std::uint64_t out = 0u;
  for (std::size_t i = 0; i < 8u; ++i) out |= static_cast<std::uint64_t>(v[off + i]) << (i * 8u);
  return out;
}

} // namespace

int main() {
  using namespace rtxmac::nvidia::gsp;

  // Synthetic 8 GiB GA104 layout. Dynamic firmware/heap sizes are deliberate
  // inputs; the planner only applies NVIDIA's production alignment/order rules.
  WprLayoutInputs li{
    .fbSize = 0x200000000ull,
    .vgaWorkspaceOffset = 0x1FFF00000ull,
    .vbiosReservedOffset = 0x1FFF00000ull,
    .wprEndMargin = 0u,
    .frtsSize = 0x100000ull,
    .bootloaderSize = 0x68000ull,
    .radix3ElfSize = 0x1A34000ull,
    .nonWprHeapSize = 0x100000ull,
    .requestedWprHeapSize = 0x8100000ull,
  };
  const auto layout = PlanWprLayout(li);
  assert(layout.has_value());
  assert(layout->gspFwWprEnd == 0x1FFF00000ull);
  assert(layout->frtsOffset == 0x1FFE00000ull);
  assert((layout->bootBinOffset & 0xFFFull) == 0u);
  assert((layout->gspFwOffset & 0xFFFFull) == 0u);
  assert((layout->gspFwHeapOffset & 0xFFFFFull) == 0u);
  assert(layout->gspFwWprStart + 0x100000ull == layout->gspFwHeapOffset);
  assert(layout->nonWprHeapOffset + layout->nonWprHeapSize == layout->gspFwWprStart);

  WprMetaInputs mi{
    .layout = *layout,
    .sysmemAddrOfRadix3Elf = 0x123450000ull,
    .sysmemAddrOfBootloader = 0x223450000ull,
    .bootloaderCodeOffset = 0x1000ull,
    .bootloaderDataOffset = 0x9000ull,
    .bootloaderManifestOffset = 0x200ull,
    .sysmemAddrOfSignature = 0x323450000ull,
    .sizeOfSignature = 0x1000ull,
    .gspFwHeapVfPartitionCount = 0u,
    .flags = 0u,
    .pmuReservedSize = 0u,
  };
  const auto meta = BuildWprMeta(mi);
  static_assert(meta.size() == 256u);
  assert(LoadLe64(meta, 0u) == kWprMetaMagic);
  assert(LoadLe64(meta, 8u) == kWprMetaRevision);
  assert(LoadLe64(meta, 16u) == mi.sysmemAddrOfRadix3Elf);
  assert(LoadLe64(meta, 24u) == li.radix3ElfSize);
  assert(LoadLe64(meta, 88u) == layout->gspFwRsvdStart);
  assert(LoadLe64(meta, 112u) == layout->gspFwWprStart);
  assert(LoadLe64(meta, 152u) == layout->frtsOffset);
  assert(LoadLe64(meta, 176u) == li.fbSize);
  assert(LoadLe64(meta, 200u) == 0u);
  assert(LoadLe32(meta, 244u) == 0u);
  assert(LoadLe64(meta, 248u) == 0u);

  const std::array<LibosRegion, 6> regions{{
    {"LOGINIT", 0x10000000ull, 0x10000ull},
    {"LOGINTR", 0x10010000ull, 0x10000ull},
    {"LOGRM",   0x10020000ull, 0x10000ull},
    {"LOGMNOC", 0x10030000ull, 0x10000ull},
    {"LOGKRNL", 0x10040000ull, 0x10000ull},
    {"RMARGS",  0x20000000ull, 0x1000ull},
  }};
  const auto libos = BuildLibosInitPage(regions);
  assert(libos.has_value());
  const auto rmargs = MakeLibosId8("RMARGS");
  assert(rmargs.has_value());
  assert(LoadLe64(*libos, 5u * kLibosRegionBytes + 0u) == *rmargs);
  assert(LoadLe64(*libos, 5u * kLibosRegionBytes + 8u) == 0x20000000ull);
  assert(LoadLe64(*libos, 5u * kLibosRegionBytes + 16u) == 0x1000ull);
  assert((*libos)[5u * kLibosRegionBytes + 24u] == 1u);
  assert((*libos)[5u * kLibosRegionBytes + 25u] == 1u);
  assert(!MakeLibosId8("TOO-LONG-ID").has_value());

  WprLayoutInputs bad = li;
  bad.vbiosReservedOffset = 0x1000ull;
  assert(!PlanWprLayout(bad).has_value());

  std::cout << "rtxmac GSP WPR/libOS metadata tests passed\n";
  return 0;
}
