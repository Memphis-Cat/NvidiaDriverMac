# GA102 PRAMIN VRAM staging

## Why this exists

The GSP FRTS FWSEC image and the SEC2 booter image are consumed by Falcon DMA as framebuffer-memory sources. Their addresses are GPU VRAM offsets, not host virtual addresses and not BAR1 physical addresses.

A full-size BAR1 mapping cannot be assumed on an Intel Mac. The first implementation therefore must not depend on Resizable BAR exposing the whole framebuffer.

## GA102 path

Current Nouveau identifies GA102 with the legacy `nv50_instmem` implementation. Its slow CPU access path selects a 1 MiB framebuffer window and accesses that window through BAR0 PRAMIN:

- BAR0 `0x001700`: PRAMIN window selector
- selector value: `windowBase >> 16`
- selected window size: `0x100000` bytes
- BAR0 `0x700000 + inWindowOffset`: CPU-visible PRAMIN aperture

For a VRAM byte offset `vramOffset`:

```text
windowBase = vramOffset & ~0xFFFFF
inWindow   = vramOffset &  0xFFFFF
selector   = windowBase >> 16
bar0Offset = 0x700000 + inWindow
```

A transfer crossing a 1 MiB boundary must select the next window before continuing.

## Current implementation

`PlanPraminStage()` is planning-only. It validates that the complete target range is inside VRAM and splits it into chunks that never cross a PRAMIN window. Each chunk records:

- source-image offset
- VRAM target offset
- byte count
- selected 1 MiB window base
- selector value for BAR0 `0x1700`
- BAR0 PRAMIN aperture offset

`PlanFramebufferStaging()` additionally binds this to the GSP boot manifest. FRTS FWSEC and SEC2 source images are rejected unless they are:

- page aligned
- entirely inside framebuffer memory
- entirely below `gspFwRsvdStart`
- non-overlapping with one another

This prevents temporary source images from overwriting the reserved/WPR/VBIOS tail.

## Safety boundary

No current DriverKit attach path executes this plan. `RTXMacDriver::Start_Impl()` remains read-only.

Before any PRAMIN write executor is enabled we still need:

1. an explicit write gate that defaults off;
2. BAR0 range mapping with exact bounds checks;
3. selector-write + readback/flush semantics verified for GA102;
4. a rollback path for every failure;
5. staging-image length/padding verification;
6. a dry-run trace showing every window transition and target range.

Do not infer GH100/Blackwell behavior from the GA102 constants. Newer Nouveau code uses a different BAR0-window register on GH100-class hardware.
