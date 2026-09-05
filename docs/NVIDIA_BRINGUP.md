# NVIDIA bring-up notes

This document records only the NVIDIA details that have been promoted into our implementation after cross-checking public sources.

## Identity registers used by prototype 1

NVIDIA's published `nv_ref.h` defines:

- `NV_PMC_BOOT_0` at BAR0 offset `0x00000000` (read-only)
- `NV_PMC_BOOT_42` at BAR0 offset `0x00000A00` (read-only)

For `NV_PMC_BOOT_42` the fields used by our decoder are:

- bits 28:24 — architecture
- bits 23:20 — implementation
- bits 19:16 — major revision
- bits 15:12 — minor revision
- bits 11:8 — extended minor revision

NVIDIA currently assigns architecture `0x17` to the GA100/Ampere family. Its generated HAL mapping identifies implementations:

| implementation | chip |
| ---: | --- |
| `0x0` | GA100 |
| `0x2` | GA102 |
| `0x3` | GA103 |
| `0x4` | GA104 |
| `0x6` | GA106 |
| `0x7` | GA107 |

The RTXMac portable core decodes these values offline. The first DriverKit hardware probe is only allowed to map the first 4 KiB of BAR0 and read `BOOT_0` and `BOOT_42`. There is deliberately no generic MMIO write API.

## Why this matters

Before GSP, DMA or firmware work, one macOS run can prove all of the following:

1. the DEXT attached to the intended PCI function,
2. BAR0 is actually mappable through PCIDriverKit on the Hackintosh,
3. the GPU is responding to MMIO reads,
4. the chip architecture/implementation reported by hardware is coherent with the PCI model.

If any of these fail, we stop there and analyze the diagnostic bundle from Windows instead of trying higher-risk bring-up stages.

## Public references

- NVIDIA `open-gpu-kernel-modules`, `src/common/inc/swref/published/nv_ref.h`
- NVIDIA `open-gpu-kernel-modules`, generated `g_hal_archimpl.h`
- tinygrad TinyGPU/tinygrad NVIDIA runtime as architectural reference

No NVIDIA source file has been copied wholesale into this repository; the constants/field layout above are public register definitions and are documented with provenance.
