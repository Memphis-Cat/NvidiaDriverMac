# GSP WPR and libOS metadata

This layer is deliberately offline-only. It serializes the boot metadata that a later hardware backend will need, but it performs no allocation, DMA or MMIO.

## GA104 production path

NVIDIA's current HAL dispatch routes GA104 through `kgspPopulateWprMeta_TU102`. The planner therefore mirrors that layout order and its alignments:

1. usable FB / VBIOS-reserved boundary
2. WPR end (128 KiB aligned)
3. FRTS
4. boot binary (4 KiB aligned)
5. GSP Radix3 ELF (64 KiB aligned)
6. WPR heap (1 MiB aligned)
7. one MiB reserved for the 256-byte WPR metadata block, matching the production HAL's rounded `wprMetaSize`
8. non-WPR heap (1 MiB aligned)

Heap sizes and the VBIOS/WPR-end boundary are inputs. The code does **not** guess values that NVIDIA obtains dynamically from firmware, registry policy or hardware state.

## GspFwWprMeta

`BuildWprMeta()` serializes the initial-boot form of NVIDIA's 256-byte structure with:

- magic `0xdc3aae21371a60b3`
- revision `1`
- Radix3 ELF / bootloader / signature system addresses
- bootloader code, data and manifest offsets
- complete framebuffer layout
- optional VF count, flags and PMU reservation
- `bootCount = 0`
- `verified = 0`

`verified` is intentionally never pre-filled with NVIDIA's verified sentinel; that state transition belongs to Booter.

## libOS init page

`BuildLibosInitPage()` emits the 4 KiB page of 32-byte `LibosMemoryRegionInitArgument` records from NVIDIA's `libos_init_args.h`. ID8 identifiers are encoded exactly as NVIDIA/Nouveau do: shift the ASCII name into a 64-bit integer, then store it in native little-endian ABI form.

The builder accepts arbitrary region lists (up to 128 entries) so firmware-specific logging/task requirements remain data, not hard-coded driver policy.
