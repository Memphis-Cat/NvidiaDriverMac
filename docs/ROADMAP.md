# Roadmap

The roadmap is organized around **hardware-test gates**. We keep working offline until a gate can answer a question that cannot be answered from Windows/source inspection.

## Phase 0 — offline foundation (current)

- [x] Repository initialized
- [x] Portable C++ core
- [x] Windows PCI hardware-ID parser
- [x] Windows hardware capture script
- [x] Windows exact-target generator
- [x] Versioned read-only ABI draft
- [x] Ampere `NV_PMC_BOOT_42` offline decoder
- [x] Portable PCI + NVIDIA identity tests
- [x] DriverKit transport skeleton
- [x] First-prototype diagnostic bundle script
- [ ] Confirm the exact PCI hardware ID/subsystem ID of the target RTX 3060 Ti from Windows
- [ ] macOS 26 Intel compile validation of the DEXT
- [ ] Audit the signed/local-development DriverKit activation path for the test machine

**No macOS hardware test during this phase.**

## Phase 1 — first macOS read-only probe

One boot/test session should answer all of these at once:

- Does DriverKit attach to the exact NVIDIA PCI function?
- What PCI command/status/config values does Tahoe expose?
- Which BARs are available and what are their sizes/types?
- Can BAR0 be mapped safely?
- Can `NV_PMC_BOOT_0` and `NV_PMC_BOOT_42` be read consistently?
- Does the decoded architecture/implementation match the expected Ampere chip?
- What does the active VT-d/DMAR/IOMMU environment look like?

The probe must emit a single diagnostic bundle that can be copied back to Windows.

## Phase 2 — DMA/GSP transport

- Allocate DMA memory through DriverKit
- Record physical/I/O virtual segments
- Validate GPU-visible DMA addressing
- Bring SEC2/GSP boot far enough to establish host RPC
- Solve or route around Intel macOS IOMMU/AppleVTD issues

Hardware writes begin here and must be narrowly scoped.

## Phase 3 — compute proof

- GPU virtual memory/page tables
- GSP-RM RPC lifecycle
- Command queues
- Submit one deterministic compute workload
- Verify result on CPU

Success criterion: the RTX 3060 Ti demonstrably executes a GPU workload under macOS.

## Phase 4 — off-screen graphics

- Integrate/adapt NVK/NAK concepts
- Create buffers/images
- Compile shaders
- Render a deterministic triangle/image off-screen
- Read it back and compare against a reference image/hash

## Phase 5 — display scanout

- Display engine discovery
- EDID
- modesetting
- DisplayPort/HDMI scanout
- stable framebuffer output

## Phase 6 — macOS graphics integration

Research and implement the interface required for WindowServer/IOAccelerator and, if feasible, Metal-facing acceleration.

## Phase 7 — stability

Power management, reset/recovery, sleep/wake, multi-monitor, application compatibility, error handling and installation/upgrade behavior.
