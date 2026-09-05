# Offline GSP boot manifest

`gsp_manifest.*` bridges the portable parsers/planners and a future DriverKit DMA/MMIO backend. It performs **zero hardware operations**.

The manifest defines nine core allocations: queue backing, cached GSP args, libOS init page, WPR metadata, Radix3 firmware, firmware signature, GSP bootloader, patched FRTS FWSEC image, and patched SEC2 booter image. Each records logical/rounded size, alignment, memory domain and DMA-mapping requirement.

Once physical addresses are resolved, the core now builds all host-side initial artifacts:

- `GSP_ARGUMENTS_CACHED`
- 256-byte `GspFwWprMeta`
- 4 KiB libOS init page
- fully prefilled 0x40000 GSP command queue containing system-info and registry RPCs

The dry-run hardware sequence is:

1. prefilled bootstrap RPC records are already present
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

`executableWithCurrentCore` can now be true: the **offline** boot plan itself has no known host-preparation gap. This does not mean a live executor exists; DriverKit allocation/DMA/MMIO remains separately gated.
