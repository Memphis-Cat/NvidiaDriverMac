# Architecture

NvidiaDriverMac is split so most work can be developed and validated from Windows before macOS hardware testing.

## Layers

### 1. Offline / Windows tooling

Collect exact PCI identity, board/BIOS information and NVIDIA driver metadata. Replay captured structures and validate parsers/ABIs without touching the macOS machine state.

### 2. `rtxmac_core`

Portable C++20. No Apple, Windows, Linux or NVIDIA-driver headers. It owns stable data structures, parsers and protocol rules shared by tools and future driver-facing code.

The core includes read-only MMIO abstractions, package validation, DMA-layout validation, GA10x boot-memory planning, GSP artifact construction, an exact phase state machine, write policies, and recovery decisions. Hardware-independent tests exercise these contracts without DriverKit or a GPU.

### 3. macOS PCI transport (DriverKit)

A PCIDriverKit system extension owns the target NVIDIA PCI function and provides the hardware implementation of the portable transport. Its attach path is deliberately read-only. The user client currently exposes package validation, cold SYSRAM staging, PCI/GSP system information, and a read-only MMU reserved-boundary check.

### 4. Ampere bring-up runtime

The repository contains the offline/cold GA10x GSP boot model, DriverKit DMA and PRAMIN mechanisms, static Falcon policy, phase executor, and recovery rules. Live reset/write/execution remains disconnected and default-off. TinyGPU/tinygrad and NVIDIA's open GPU kernel modules are reference implementations; code reuse must retain upstream licensing and attribution.

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

The first prototype is deliberately boring: attach to the exact GPU, validate the package, collect PCI/GSP system information, prepare cold SYSRAM DMA buffers, and read the allow-listed MMU-lock page. It must produce one self-contained diagnostic bundle for analysis back on Windows and must not reset or start the GPU.
