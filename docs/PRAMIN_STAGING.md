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

`PlanPraminStage()` validates that the complete target range is inside VRAM and splits it into chunks that never cross a PRAMIN window. Each chunk records:

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

`BuildFramebufferStageArtifact()` binds exact firmware bytes to a staging plan. The logical firmware occupies the prefix of the transfer image and every page-rounded padding byte is explicitly zeroed. This prevents stale/uninitialized host memory from being copied into VRAM.

The DriverKit layer now also contains cold PRAMIN execution primitives:

- `RTXMacStagePramin()` — policy-bounded selector + aperture writes
- `RTXMacVerifyPramin()` — readback comparison after staging
- `RTXMacReadPramin()` — capture an existing staging range into caller-owned memory
- `RTXMacStagePraminTransactional()` — backup, stage, verify, and restore+verify on failure

The register/data policy grants only the PRAMIN selector register and the exact 1 MiB aperture. Every planner chunk is independently revalidated before BAR0 access. PCIe posted writes are ordered with readback before changing windows or returning to the caller.

## Safety boundary

`RTXMacDriver::Start_Impl()` remains read-only. None of the PRAMIN staging, verification, backup, rollback, reset, firmware-execution, or GSP-boot code is called when the DEXT attaches.

Every public PRAMIN operation has an explicit `writesEnabled` gate that defaults to `false`. An accidental call therefore returns `kIOReturnNotPermitted` before BAR0 access. Readback/backup also uses this gate because changing the PRAMIN selector is itself an MMIO write.

The original pre-write checklist is now implemented as follows:

1. explicit write gate defaulting off — implemented;
2. exact BAR0 bounds checking — implemented;
3. selector write + posted-write readback/flush — implemented;
4. rollback path — implemented as caller-buffered transactional backup/restore with post-restore verification;
5. staging-image length/padding validation — implemented with deterministic zero padding;
6. dry-run trace of every selector/aperture transition — implemented in the portable core.

These pieces being present does **not** mean hardware execution is enabled or ready for a user boot test. Before that milestone, the higher-level boot orchestration still needs to bind real parsed FRTS/SEC2 images to the staging artifacts, keep PRAMIN execution behind an explicit experimental mode, validate the PCI bus-master transition, and execute Falcon/GSP phases with per-phase failure checks.

Do not infer GH100/Blackwell behavior from the GA102 constants. Newer Nouveau code uses a different BAR0-window register on GH100-class hardware.
