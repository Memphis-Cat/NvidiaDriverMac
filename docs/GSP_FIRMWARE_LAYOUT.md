# GSP firmware image and Radix3 layout

The GA104 path uses the `nvidia/ga102/gsp` firmware family. This module keeps firmware inspection and page-tree construction portable/offline.

## ELF extraction

`elf.*` is a strict ELF64 little-endian section locator. For the GSP firmware bundle we need:

- `.fwimage`
- `.fwsignature_ga10x`

The parser bounds-checks the ELF section table, string table, section payloads and section names. It does not load or execute ELF code.

## Radix3

GSP libOS consumes the firmware image through a three-level page-address tree. `PlanRadix3()` mirrors the current working Ampere layout:

- 4 KiB pages
- 512 64-bit entries per table page
- level page counts are computed upward from the `.fwimage` page count
- table levels are packed first; raw `.fwimage` bytes follow them

`BuildRadix3Tables()` requires the future allocator's complete page-physical-address list and emits only the page-table prefix. It rejects unaligned addresses or a mismatched page count.

No DriverKit allocation or DMA happens in this module.

## RISC-V bootloader descriptor

`ParseRiscvBootloader()` reads the NVIDIA firmware container and its `RM_RISCV_UCODE_DESC`. It accepts both the 84-byte descriptor shape used by older 570.144 tooling and the current 92-byte shape with SMP/PLIC fields, based on the actual header region size. Every code/data/manifest subrange is checked against the firmware data payload.
