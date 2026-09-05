#include "rtxmac/dma.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

int main() {
  using namespace rtxmac;

  {
    const std::vector<DmaSegment> segments{{0x100000, 0x1000}, {0x300000, 0x2000}};
    const auto result = ValidateDmaSegments(segments, 0x3000);
    assert(result.status == DmaCoverageStatus::Ok);
    assert(result.coveredBytes == 0x3000);
    assert(result.segmentCount == 2);
  }

  {
    std::vector<DmaSegment> segments;
    for (std::uint64_t i = 0; i < 32; ++i) segments.push_back({0x100000 + i * 0x2000, 0x1000});
    const auto result = ValidateDmaSegments(segments, 0x81000);
    assert(result.status == DmaCoverageStatus::Truncated);
    assert(result.coveredBytes == 0x20000);
  }

  {
    const std::vector<DmaSegment> segments{{0x1000, 0}};
    assert(ValidateDmaSegments(segments, 1).status == DmaCoverageStatus::ZeroLength);
  }

  {
    const std::vector<DmaSegment> segments{{std::numeric_limits<std::uint64_t>::max() - 1, 4}};
    assert(ValidateDmaSegments(segments, 4).status == DmaCoverageStatus::AddressOverflow);
  }

  std::cout << "rtxmac DMA scatter-list validation tests passed\n";
  return 0;
}
