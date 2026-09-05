# Intel macOS DMA / IOMMU notes

This is a major research area for RTXMac because the closest public internal-PCIe experiment is currently blocked here.

## TinyGPU Intel Mac Pro result

Tinygrad issue `#17763` reports an internal PCIe RTX 3060 on an Intel Mac Pro reaching all of the following before failure:

- BAR0/1/3 mapping,
- DriverKit DMA allocations,
- FRTS execution,
- WPR2 creation,
- SEC2 successfully DMA-reading and verifying the roughly 63 MB GSP-RM image,
- GSP RISC-V becoming active.

The GSP then halts before initializing the RPC queue. The reporter observed no GSP writes into host memory and suspects RISC-V-side DMA through AppleVTD/IOMMU.

A second important finding appears when testing without DMA remapping: a large preparation returned 32 segments whose lengths covered only part of the requested memory. One example requested `0x81000` bytes but the returned segments covered only `0x20000` bytes.

## DriverKit's 32-segment API limit

Apple's public DriverKit declaration is:

```cpp
PrepareForDMA(..., uint32_t *segmentsCount, IOAddressSegment segments[32]);
```

So the correct fix is **not** to pass a larger array. Thirty-two is an RPC/API boundary in DriverKit.

## RTXMac strategy

RTXMac must never silently accept a partial DMA mapping.

For every preparation we will validate:

1. no zero-length segments,
2. no address arithmetic overflow,
3. no accumulated-size overflow,
4. returned segment lengths cover the entire requested subrange.

If the returned 32 segments do not cover the requested subrange, the preparation is unusable and must be completed/released rather than passed to the GPU.

For a correctness-first fallback, the portable core can split memory into subranges containing no more than 32 pages. With 4 KiB pages that is `32 × 4096 = 0x20000` bytes per preparation. Even if every page becomes a separate DMA segment, each subrange fits the DriverKit result limit. Runtime coverage validation is still mandatory because an IOMMU is free to impose additional constraints.

This conservative strategy is not necessarily the final high-performance design. Later we can use larger preparations when the platform returns sufficiently contiguous IOVAs, while retaining the same validation/fallback path.

## Why this is offline-testable

The portable core contains both scatter-list validation and a conservative chunk planner. Tests reproduce the `0x81000` request / `0x20000` returned-coverage pattern and verify that a 63 MiB GSP image can be partitioned into complete DriverKit-safe subranges before any macOS test.
