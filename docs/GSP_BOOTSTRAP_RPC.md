# GSP bootstrap RPC prefill

The two host-owned command records required before the Ampere GSP launch are now serialized offline.

## GSP_SET_SYSTEM_INFO (function 72)

For the 570.144 ABI the payload is 928 bytes. We zero-fill the complete object and write only the fields used by the working GA10x bring-up path at their generated ABI offsets:

- BAR0 / BAR1 / BAR3 physical addresses: 0 / 8 / 16
- PCI BDF: 32
- max user VA: 72
- PCI config mirror base/size: 80 / 84
- PCI device/subdevice/revision: 88 / 92 / 96
- passthrough flag: 840

This avoids compiler-specific C structure packing entirely.

## SET_REGISTRY (function 73)

The registry payload uses the NVIDIA packed-table ABI:

- 8-byte table header
- 16-byte entries
- NUL-terminated names after the entry array

The default bootstrap records match the current working open bring-up path:

- `RMForcePcieConfigSave = 1`
- `RMSecBusResetEnable = 1`

## Command queue image

`BuildBootstrapCommandQueue()` creates the complete 0x40000 host command queue:

- 32-byte TX header
- RX-header slot at offset 32
- entries begin at 0x1000
- 0x1000-byte message elements
- 63 ring entries
- sequence 0: system info
- sequence 1: registry table
- final write pointer = 2

Both records use the existing RPC v3/checksum implementation. The result is still just a byte vector; no shared/device memory is modified.
