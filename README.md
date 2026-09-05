# NvidiaDriverMac

### This is an project that will make nvidia cards in macos possible. (Probably)

> [!WARNING]
> Experimental research project. Nothing here should be considered a production GPU driver yet. Early builds must be treated as potentially crash-prone and should only be tested after the repository reaches an explicit hardware-test milestone.


> [!WARNING]
> The only NVIDIA GPU that is gonna be tested is gonna be the RTX 3060 TI, other NVIDIA GPUS can not work.

## Goal

Bring modern NVIDIA GPUs—starting with the RTX 3060 Ti / Ampere—up on macOS Tahoe using a native macOS DriverKit transport plus an open userspace stack.

The long-term target is much larger than simple PCI detection: reliable GPU initialization, GSP-RM communication, memory management, command submission, rendering, display scanout, and eventually macOS graphics integration.
