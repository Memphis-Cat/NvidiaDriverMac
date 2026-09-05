# NvidiaDriverMac

this is an project that will make nvidia cards in macos possible (probably)

> [!WARNING]
> Experimental research project. Nothing here should be considered a production GPU driver yet. Early builds must be treated as potentially crash-prone and should only be tested after the repository reaches an explicit hardware-test milestone.

## Goal

Bring modern NVIDIA GPUs—starting with the RTX 3060 Ti / Ampere—up on macOS Tahoe using a native macOS DriverKit transport plus an open userspace stack.

The long-term target is much larger than simple PCI detection: reliable GPU initialization, GSP-RM communication, memory management, command submission, rendering, display scanout, and eventually macOS graphics integration.

## Development rule

**Minimize macOS reboots.** Most development, static validation, ABI work, captured-data replay, and portable tests should run from Windows 11. We will not ask for a macOS hardware test until a prototype can answer a useful question that cannot be answered offline.

## Current phase

`Phase 0 — repository + offline research harness`

No GPU writes are enabled yet. The first hardware prototype will be intentionally conservative and diagnostics-first.

See [`docs/ROADMAP.md`](docs/ROADMAP.md) and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).
