# Roadmap

The roadmap is organized around **hardware-test gates**. Work stays on Windows, portable C++ tests, and macOS 26 Intel CI until the real RTX 3060 Ti can answer something source inspection cannot.

## Gate A — offline and CI foundation (complete)

- [x] Portable C++20 core and Windows tooling
- [x] RTXMACP1 package serializer/parser, SHA-256 verification, and tamper rejection
- [x] GSP/FWSEC/SEC2/VBIOS parsing and GA10x semantic validation
- [x] GSP queues, Radix3 page trees, WPR/libOS metadata, RPC bootstrap, and boot manifest
- [x] Static GA102 MMIO policy, exact phase order, bounded polling, and recovery classification
- [x] DriverKit DMA chunking, page validation, cold package staging, and persistent staged state
- [x] Transactional PRAMIN backup/write/readback/restore helpers
- [x] PCI Memory Space/Bus Master transition with intent validation and rollback
- [x] macOS 26 Intel DriverKit extension and Swift/IOKit host compile in GitHub CI
- [x] Forty-one portable test executables run with assertions enabled

The DEXT attach path remains read-only. Every write-capable helper is disconnected from the host interface and defaults to denied.

## Gate B — consolidated read-only macOS probe (implementation ready; hardware not run)

The host app can collect these results in one macOS session:

- [x] Activate/open the DriverKit service
- [x] Validate a `.rtxpkg` and match its PCI identity to the attached GPU
- [x] Read PCI identity, revision, BDF, BAR metadata, and GSP system-info inputs
- [x] Allocate/populate/prepare the five verified package DMA buffers without GPU execution
- [x] Allocate the ten generated DMA buffers and build the complete queue/arguments/WPR/Radix3/log artifact graph
- [x] Bind retained package and generated state to the exact package SHA-256 for the life of one user-client connection
- [x] Map one allow-listed BAR0 page read-only and capture the GA10x MMU lock
- [x] Compare the live MMU lock against the offline VRAM reserved boundary
- [x] Show `ok`, unreadable/unavailable, or `rebuild-required` without changing hardware state
- [x] Export host results as versioned JSON and merge them with system logs into one diagnostic ZIP
- [ ] Finish the signed/local-development activation and recovery instructions for the exact test machine
- [ ] Confirm the target board's exact Windows subsystem identity and build the final test package

Do not spend a Hackintosh reboot on this gate until the remaining offline items are finished or hardware data becomes the blocker.

## Gate C — deliberately armed cold bring-up

Most mechanisms exist but are intentionally not wired to a user-client selector:

- [x] Cold boot memory/address preparation
- [x] Framebuffer scratch placement and deterministic padding
- [x] GSP/SEC2 Falcon plans and static register masks
- [x] Phase executor, time bounds, and phase-level recovery policy
- [x] Conservative PCI reset/recovery and post-reset checks
- [x] Bind the live boundary result to the final boot manifest and rebuild lower layouts
- [x] Build the generated DriverKit DMA allocations and resolved artifacts as one retained session
- [x] Add an explicit package/GPU/session-bound experimental arming contract
- [ ] Connect only the audited cold sequence, with failure capture and controlled recovery
- [ ] Establish GSP-RM host RPC on the RTX 3060 Ti

Hardware writes begin only at this gate. There will be no generic userspace MMIO-write API.

## Gate D — compute proof

- GPU virtual memory/page tables
- GSP-RM RPC lifecycle
- command queues
- one deterministic GPU workload with CPU-verified output

Success means the RTX 3060 Ti demonstrably executes a GPU workload under macOS.

## Gate E — off-screen graphics

- Adapt the relevant NVK/NAK concepts
- create buffers/images and compile shaders
- render a deterministic off-screen result
- read back and compare against a reference image/hash

## Gate F — display and macOS graphics integration

- Display engine discovery, EDID, modesetting, and DP/HDMI scanout
- stable framebuffer output
- research WindowServer/IOAccelerator integration and feasible Metal-facing acceleration

## Gate G — stability

Power management, reset/recovery, sleep/wake, multi-monitor, application compatibility, error handling, and installation/upgrade behavior.
