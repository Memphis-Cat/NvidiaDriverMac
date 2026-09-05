# Architecture

NvidiaDriverMac is split so most work can be developed and validated from Windows before macOS hardware testing.

## Layers

### 1. Offline / Windows tooling

Collect exact PCI identity, board/BIOS information and NVIDIA driver metadata. Replay captured structures and validate parsers/ABIs without touching the macOS machine state.

### 2. `rtxmac_core`

Portable C++20. No Apple, Windows, Linux or NVIDIA-driver headers. It owns stable data structures, parsers and protocol rules shared by tools and future driver-facing code.

The core now includes a `ReadOnlyMmio` transport abstraction. NVIDIA identity probing is written against this interface rather than DriverKit. Unit tests run the same probe against a fake GPU and assert that prototype 1 reads **only** `NV_PMC_BOOT_0` and `NV_PMC_BOOT_42`.

### 3. macOS PCI transport (DriverKit)

A PCIDriverKit system extension owns the target NVIDIA PCI function and provides the hardware implementation of the portable transport. Prototype 1 is deliberately read-only: identity, PCI config reads, BAR metadata, and two allow-listed MMIO identity reads.

### 4. Ampere bring-up runtime

Future work. GSP firmware boot, DMA, MMU/page tables, GPU virtual address spaces, RPC queues and command submission. TinyGPU/tinygrad and NVIDIA's open GPU kernel modules are reference implementations; code reuse must retain upstream licensing and attribution.

This layer must stay transport-independent so the state machine can run against fake/captured devices on Windows before it is allowed to run on the real GPU.

### 5. Graphics userspace

Future work. NVK/NAK are the likely reference for Ampere graphics command generation and shader compilation. A macOS-specific winsys/WSI layer would be required.

### 6. macOS display/graphics integration

Future work and currently the largest unknown: modesetting/scanout, framebuffer integration, WindowServer/IOAccelerator behavior, and eventually Metal-facing integration.

## Non-negotiable development rules

- Keep the first hardware stages observable and reversible.
- No VBIOS flashing.
- No clock/voltage/power-limit changes in early stages.
- No arbitrary MMIO write primitive exposed to userspace.
- New write paths require an explicit allow-list and a documented reason.
- Captures and parsers should be testable offline before they are used on hardware.
- Prefer one information-rich macOS test over many small reboot/test cycles.

## First macOS prototype

The first prototype is deliberately boring: attach to the exact GPU, read PCI identity/configuration, enumerate BAR sizes, and read only `NV_PMC_BOOT_0` / `NV_PMC_BOOT_42`. It must produce one self-contained diagnostic bundle for analysis back on Windows.
