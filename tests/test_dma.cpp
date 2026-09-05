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
    assert(ResolveLinearDmaRange(segments, 0x3000).status == DmaLinearRangeStatus::AddressDiscontinuity);

    const auto pages = ExpandDmaSegmentsToPages(segments, 0x3000);
    assert(pages.status == DmaPageMapStatus::Ok);
    assert(pages.pageAddresses.size() == 3u);
    assert(pages.pageAddresses[0] == 0x100000ull);
    assert(pages.pageAddresses[1] == 0x300000ull);
    assert(pages.pageAddresses[2] == 0x301000ull);
  }

  {
    // DriverKit may split one linear IOVA range into multiple descriptors. That
    // is still safe for base+size firmware ABIs when addresses are adjacent.
    const std::vector<DmaSegment> segments{{0x500000, 0x1000}, {0x501000, 0x2000}};
    const auto linear = ResolveLinearDmaRange(segments, 0x3000);
    assert(linear.status == DmaLinearRangeStatus::Ok);
    assert(linear.address == 0x500000ull);
    assert(linear.length == 0x3000ull);
  }

  {
    std::vector<DmaSegment> segments;
    for (std::uint64_t i = 0; i < 32; ++i) segments.push_back({0x100000 + i * 0x2000, 0x1000});
    const auto result = ValidateDmaSegments(segments, 0x81000);
    assert(result.status == DmaCoverageStatus::Truncated);
    assert(result.coveredBytes == 0x20000);
    assert(ResolveLinearDmaRange(segments, 0x81000).status == DmaLinearRangeStatus::CoverageError);
    assert(ExpandDmaSegmentsToPages(segments, 0x81000).status == DmaPageMapStatus::CoverageError);
  }

  {
    const std::vector<DmaSegment> segments{{0x100000, 0x2000}};
    const auto result = ValidateDmaSegments(segments, 0x1000);
    assert(result.status == DmaCoverageStatus::Overrun);
    assert(ResolveLinearDmaRange(segments, 0x1000).status == DmaLinearRangeStatus::CoverageError);
    assert(ExpandDmaSegmentsToPages(segments, 0x1000).status == DmaPageMapStatus::CoverageError);
  }

  {
    const std::vector<DmaSegment> segments{{0x1000, 0}};
    assert(ValidateDmaSegments(segments, 1).status == DmaCoverageStatus::ZeroLength);
  }

  {
    const std::vector<DmaSegment> segments{{std::numeric_limits<std::uint64_t>::max() - 1, 4}};
    assert(ValidateDmaSegments(segments, 4).status == DmaCoverageStatus::AddressOverflow);
  }

  {
    const std::vector<DmaSegment> segments{{0x100800, 0x1000}};
    assert(ExpandDmaSegmentsToPages(segments, 0x1000).status == DmaPageMapStatus::SegmentAddressUnaligned);
  }

  {
    const std::vector<DmaSegment> segments{{0x100000, 0x0800}, {0x200000, 0x0800}};
    assert(ExpandDmaSegmentsToPages(segments, 0x1000).status == DmaPageMapStatus::SegmentLengthUnaligned);
  }

  {
    const std::vector<DmaSegment> segments{{0x100000, 0x1000}};
    assert(ExpandDmaSegmentsToPages(segments, 0x1000, 3000).status == DmaPageMapStatus::InvalidPageSize);
    assert(ExpandDmaSegmentsToPages(segments, 0x0800).status == DmaPageMapStatus::RequestedSizeNotPageAligned);
  }

  std::cout << "rtxmac DMA scatter-list, linear-range, and page-map validation tests passed\n";
  return 0;
}
