# First hardware test gate

Do **not** test on macOS yet unless the project explicitly reaches this gate.

The goal is to spend one macOS boot on a useful, read-only evidence collection rather than repeatedly rebooting just to discover one missing fact.

## Prototype 1 behavior

Prototype 1 must remain read-only with respect to the NVIDIA GPU. It may:

- attach to the exact NVIDIA PCI display device,
- read PCI vendor/device/revision/subsystem identity,
- enumerate BAR metadata,
- map only the BAR0 pages containing allow-listed diagnostic registers,
- read the fixed diagnostic snapshot,
- validate one `.rtxpkg` against the attached PCI identity,
- allocate, zero-fill, populate, and `PrepareForDMA()` cold SYSRAM buffers,
- construct the queue, cached arguments, LIBOS init page, WPR metadata, Radix3 image, and five zeroed log regions,
- resolve and report the complete cold boot address graph and planned phase count,
- retain those buffers only while the user-client connection is open,
- emit logs and collect system metadata.

It must **not**:

- write PCI configuration space,
- write MMIO,
- reset the GPU,
- enable PCI Memory Space or Bus Master,
- submit or execute prepared DMA,
- load firmware,
- alter clocks, power, fan, voltage, or VBIOS state.

## Snapshot collected in the first boot

The current allow-list includes:

- `NV_PMC_BOOT_0`
- `NV_PMC_BOOT_42`
- `NV_PFB_PRI_MMU_WPR2_ADDR_HI`
- GSP Falcon mailbox 0/1
- GSP RISC-V `CPUCTL`
- usable-VRAM scratch (`SECURE_SCRATCH_GROUP_42`)
- GFW boot-progress scratch (`SECURE_SCRATCH_GROUP_05(0)`)
- BSI secure scratch 14
- SEC2 Falcon mailbox 0/1

This set is intentionally chosen to answer several future bring-up questions in one boot: exact chip identity, whether prior firmware state/WPR2 survived, GSP active/halted state, firmware progress, reported VRAM, and SEC2/GSP mailbox state.

The host app additionally reports the package/live PCI match, prepared DMA page summaries, the complete generated boot-memory graph, artifact/sequence construction status, decoded MMU-lock range, offline prototype boundary, effective live boundary, and whether the package layout must be rebuilt before any future write stage.

## Gate conditions

Before asking for the first macOS test:

1. All portable tests run with assertions enabled and are green.
2. DriverKit and Swift host targets compile on the macOS 26 Intel CI runner.
3. Exact RTX 3060 Ti PCI/subsystem match is generated from the user's Windows hardware capture.
4. The host exports its validation, staging, cold-session, system-info, and boundary results, and the collection script merges that JSON with system logs into one ZIP.
5. Signing/activation and recovery steps are documented for the exact test machine.
6. There is no remaining useful offline work that would materially improve prototype 1.
