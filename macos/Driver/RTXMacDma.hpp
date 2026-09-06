#pragma once

#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/IODMACommand.h>
#include <DriverKit/IOReturn.h>
#include <PCIDriverKit/PCIDriverKit.h>

#include <cstddef>
#include <cstdint>

constexpr std::uint64_t kRTXMacDmaPageBytes = 0x1000ull;
constexpr std::uint32_t kRTXMacMaxDmaSegments = 32u;
constexpr std::uint64_t kRTXMacMaxDmaChunkBytes =
    static_cast<std::uint64_t>(kRTXMacMaxDmaSegments) * kRTXMacDmaPageBytes;

struct RTXMacPreparedDmaChunk {
  IODMACommand* command{nullptr};
  IOAddressSegment segments[kRTXMacMaxDmaSegments]{};
  std::uint32_t segmentCount{};
  std::uint64_t flags{};
  std::uint64_t offset{};
  std::uint64_t length{};
};

struct RTXMacPreparedDmaBuffer {
  IOBufferMemoryDescriptor* memory{nullptr};
  RTXMacPreparedDmaChunk* chunks{nullptr};
  std::uint32_t chunkCount{};
  std::uint64_t length{};
};

// Allocate a page-aligned host buffer and prepare it for device DMA in bounded
// <=32-segment chunks. No GPU MMIO is performed.
[[nodiscard]] kern_return_t RTXMacAllocateAndPrepareDmaBuffer(
    IOPCIDevice* pci,
    std::uint64_t length,
    RTXMacPreparedDmaBuffer* out) noexcept;

void RTXMacReleasePreparedDmaBuffer(RTXMacPreparedDmaBuffer* prepared) noexcept;

[[nodiscard]] kern_return_t RTXMacCollectDmaPageAddresses(
    const RTXMacPreparedDmaBuffer* prepared,
    std::uint64_t* pageAddresses,
    std::uint32_t pageCapacity,
    std::uint32_t* pageCount) noexcept;

// Copy host bytes into the original IOBufferMemoryDescriptor, then explicitly
// propagate each prepared subrange into the active DMA mapping with
// PerformOperation(Write). This is safe even if the mapping is not identical to
// the CPU-visible storage.
[[nodiscard]] kern_return_t RTXMacCopyIntoPreparedDmaBuffer(
    const RTXMacPreparedDmaBuffer* prepared,
    const void* source,
    std::uint64_t sourceBytes) noexcept;

// Synchronize one 32-bit word from the active device DMA mapping back into the
// original memory descriptor with PerformOperation(Read), then return it.
[[nodiscard]] kern_return_t RTXMacReadPreparedDmaU32(
    const RTXMacPreparedDmaBuffer* prepared,
    std::uint64_t offset,
    std::uint32_t* value) noexcept;
