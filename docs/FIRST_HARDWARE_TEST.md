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
- emit logs and collect system metadata.

It must **not**:

- write PCI configuration space,
- write MMIO,
- reset the GPU,
- prepare DMA,
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

## Gate conditions

Before asking for the first macOS test:

1. Portable Windows/offline tests are green.
2. DriverKit target has been compile-checked on a macOS 26 Intel environment if available.
3. Exact RTX 3060 Ti PCI match has been generated from the user's Windows hardware capture.
4. The diagnostic collection script is ready to package logs and system state in one command.
5. There is no remaining useful offline work that would materially improve prototype 1.
