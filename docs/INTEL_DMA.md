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

A second important finding appears when testing without DMA remapping: TinyGPU's DEXT used a fixed 32-element segment array. Large mappings were silently represented by only the first 32 segments. One example requested `0x81000` bytes but the returned segments covered only `0x20000` bytes.

## RTXMac design rule

RTXMac must **never silently accept a partial DMA scatter list**.

The portable core therefore validates every future DriverKit DMA result:

1. no zero-length segments,
2. no address arithmetic overflow,
3. no accumulated-size overflow,
4. total segment coverage must be at least the requested DMA length.

If DriverKit cannot return the entire mapping with the supplied segment capacity, the call must retry with a larger buffer or fail. Continuing with a truncated list is forbidden.

## Why this is offline-testable

The validation logic is platform-independent C++ and is covered by a regression test reproducing the `32 × 4 KiB = 0x20000` truncation pattern against a requested `0x81000` mapping. We can fix this class of bug before our first macOS run.
