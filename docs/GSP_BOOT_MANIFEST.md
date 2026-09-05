# Offline GSP boot manifest

`gsp_manifest.*` is the bridge between the portable parsers/planners and a future DriverKit DMA/MMIO backend. It still performs **zero hardware operations**.

## Phase A: memory requirements

The manifest currently requires nine core allocations:

1. GSP command/status queue backing (system memory)
2. cached GSP arguments page (system memory)
3. libOS init-argument page (system memory)
4. WPR metadata page (system memory)
5. Radix3 GSP firmware tree/image (system memory)
6. GSP firmware signature (system memory)
7. GSP RISC-V bootloader (system memory)
8. patched FRTS FWSEC image (framebuffer)
9. patched SEC2 booter image (framebuffer)

Each item has a logical size, rounded allocation size, required alignment, memory domain and whether a DriverKit DMA mapping is required.

Firmware-specific libOS log/task backing regions remain caller-provided rather than being frozen into driver policy.

## Phase B: resolved artifacts

Once physical addresses exist, the core can build:

- `GSP_ARGUMENTS_CACHED`
- 256-byte `GspFwWprMeta`
- 4 KiB libOS init page

It validates that the parsed bootloader payload size matches the manifest and that all resolved core allocations are page aligned.

## Phase C: dry-run hardware sequence

The operation list mirrors the current Ampere boot order:

1. prefill bootstrap RPC records (currently marked as an unresolved host-preparation dependency)
2. reset GSP Falcon for FWSEC
3. execute FRTS FWSEC
4. require WPR2 high != 0
5. reset GSP selecting RISC-V
6. program GSP libOS argument mailbox
7. reset SEC2
8. execute SEC2 authenticated booter with WPR metadata mailbox
9. require SEC2 mailbox0 == 0
10. release GSP RISC-V via FALCON_OS
11. require GSP RISC-V CPU active
12. wait for the GSP status queue header

The sequence combines existing deny-by-default Falcon operation plans; there is still no live executor.

## Remaining explicit blocker

Before this manifest can report `executableWithCurrentCore=true`, we still need byte-exact serializers for the two command-queue records prefilled before GSP launch:

- GSP system information
- registry table

That is the next offline task.
