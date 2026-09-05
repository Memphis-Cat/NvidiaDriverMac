#include "rtxmac/dma_plan.hpp"
#include "rtxmac/gsp_boot.hpp"

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

  const auto layout = PlanQueueMemory();
  assert(layout.has_value());
  assert(layout->queueBytes == 0x40000ull);
  assert(layout->queuePageCount == 128ull);
  assert(layout->pageTableEntryCount == 129ull);
  assert(layout->pageTableBytes == 0x1000ull);
  assert(layout->commandQueueOffset == 0x1000ull);
  assert(layout->statusQueueOffset == 0x41000ull);
  assert(layout->totalBytes == 0x81000ull);

  // Regression for the Intel DriverKit failure mode: 0x81000 spans 129 4K
  // pages. At 32 worst-case segments per PrepareForDMA, it requires 5 chunks.
  const auto dma = rtxmac::PlanConservativeDmaChunks(layout->totalBytes);
  assert(dma.status == rtxmac::DmaPlanStatus::Ok);
  assert(dma.chunks.size() == 5u);
  assert(dma.chunks[0].offset == 0x00000ull && dma.chunks[0].length == 0x20000ull);
  assert(dma.chunks[3].offset == 0x60000ull && dma.chunks[3].length == 0x20000ull);
  assert(dma.chunks[4].offset == 0x80000ull && dma.chunks[4].length == 0x01000ull);

  const auto args = BuildCachedArguments(*layout, 0x12345000ull);
  assert(args.size() == 72u);
  assert(LoadLe64(args, 0u) == 0x12345000ull);
  assert(LoadLe32(args, 8u) == 129u);
  assert(LoadLe64(args, 16u) == 0x1000ull);
  assert(LoadLe64(args, 24u) == 0x41000ull);
  assert(LoadLe32(args, 48u) == 1u);

  assert(!PlanQueueMemory(0x40001ull).has_value());

  std::cout << "rtxmac GSP boot-memory planning tests passed\n";
  return 0;
}
