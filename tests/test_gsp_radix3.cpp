#include "rtxmac/gsp_radix3.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
std::uint64_t Load64(const std::vector<std::uint8_t>& v, std::size_t o) {
  std::uint64_t x=0; for (std::size_t i=0;i<8;++i) x |= static_cast<std::uint64_t>(v[o+i]) << (i*8u); return x;
}
}

int main() {
  using namespace rtxmac::nvidia::gsp;
  // 513 image pages forces two level-2 pages and exercises all three tree levels.
  const std::uint64_t imageBytes = 513ull * 4096ull - 17ull;
  const auto l = PlanRadix3(imageBytes);
  assert(l.has_value());
  assert(l->pageCounts[3] == 513u);
  assert(l->pageCounts[2] == 2u);
  assert(l->pageCounts[1] == 1u);
  assert(l->pageCounts[0] == 1u);
  assert(l->offsets[0] == 0u);
  assert(l->offsets[1] == 0x1000u);
  assert(l->offsets[2] == 0x2000u);
  assert(l->offsets[3] == 0x4000u);
  assert(l->allocationPages == 517u);

  std::vector<std::uint64_t> pages(static_cast<std::size_t>(l->allocationPages));
  for (std::size_t i=0;i<pages.size();++i) pages[i] = 0x10000000ull + i * 0x2000ull;
  const auto tables = BuildRadix3Tables(*l, pages);
  assert(tables.has_value());
  // root -> level1 page 1
  assert(Load64(*tables, 0x0000) == pages[1]);
  // level1 -> two level2 pages at indices 2,3
  assert(Load64(*tables, 0x1000) == pages[2]);
  assert(Load64(*tables, 0x1008) == pages[3]);
  // level2 tables collectively -> image pages beginning at page 4
  assert(Load64(*tables, 0x2000) == pages[4]);
  assert(Load64(*tables, 0x2FF8) == pages[515]);
  assert(Load64(*tables, 0x3000) == pages[516]);

  std::vector<std::uint8_t> firmware(static_cast<std::size_t>(imageBytes));
  for(std::size_t i=0;i<firmware.size();++i) firmware[i]=static_cast<std::uint8_t>((i*37u+11u)&0xffu);
  const auto allocation=BuildRadix3AllocationImage(*l,pages,firmware);
  assert(allocation.has_value());
  assert(allocation->size()==static_cast<std::size_t>(l->allocationPages*kRadixPageBytes));
  assert(Load64(*allocation,0x0000)==pages[1]);
  assert((*allocation)[static_cast<std::size_t>(l->offsets[3])]==firmware[0]);
  assert((*allocation)[static_cast<std::size_t>(l->offsets[3]+imageBytes-1u)]==firmware.back());
  // Final partial page is zero padded rather than containing uninitialized bytes.
  assert(allocation->back()==0u);

  auto shortFirmware=firmware;shortFirmware.pop_back();
  assert(!BuildRadix3AllocationImage(*l,pages,shortFirmware).has_value());
  pages[10] += 1;
  assert(!BuildRadix3Tables(*l, pages).has_value());
  assert(!BuildRadix3AllocationImage(*l,pages,firmware).has_value());
  assert(!PlanRadix3(0u).has_value());
  std::cout << "rtxmac GSP Radix3 allocation tests passed\n";
}
