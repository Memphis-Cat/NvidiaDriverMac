# Falcon dry-run execution plan

`falcon_plan.*` converts the current Ampere FWSEC/SEC2 authenticated-execution flow into a deterministic list of operations. It is **not an MMIO executor**.

## Represented actions

- full 32-bit writes
- masked read/modify/write operations
- register polls with explicit masks/expected values/timeouts
- delays
- conditional regions based on live register state
- the `CPUCTL.alias_en`-dependent CPU-start operation

## Reset model

For GSP (`0x110000`) or SEC2 (`0x840000`):

1. assert engine reset bit
2. delay 100 ms
3. deassert reset
4. poll `HWCFG2.mem_scrubbing == 0`
5. either configure RISC-V boot explicitly, or conditionally perform the current Falcon-vs-RISC-V handoff when `HWCFG2.riscv == 1`

## Authenticated execution model

1. permit physical FB accesses without context
2. disable DMA context requirement
3. select physical framebuffer target for context DMA 0
4. DMA IMEM in 256-byte chunks
5. DMA DMEM in 256-byte chunks
6. program RSA-3072 BROM parameter address / engine ID / ucode ID / algorithm
7. program boot vector
8. optionally program 64-bit mailbox input
9. start CPU while respecting `CPUCTL.alias_en`
10. poll until Falcon reports halted

Every register address used by the plan is derived from the current published/generated Ampere register definitions. Connecting this plan to live MMIO remains a separate, gated step.
