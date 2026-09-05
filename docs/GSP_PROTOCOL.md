# GSP-RM protocol work

This phase is intentionally offline. Nothing in `gsp_rpc.*` can touch PCIe or MMIO.

## Implemented

- GSP queue-element header serialization (48 bytes).
- RPC v3 header serialization (32 bytes).
- NVIDIA vGPU/GSP valid signature `0x43505256`.
- pending RPC result value `0xFFFFFFFF`.
- queue checksum algorithm: XOR little-endian 64-bit words, then XOR the high and low 32-bit halves.
- queue-element count calculation and zero padding.
- structural + checksum validation.
- payload fragmentation planning with a configurable maximum number of queue elements per record.
- deterministic known-vector unit test.

The layout and constants are cross-checked against NVIDIA Open GPU Kernel Modules (`rpc_headers.h` / generated GSP structures) and tinygrad's current GSP implementation. The implementation in this repository is a small, independent C++ serializer rather than a copy of tinygrad's Python runtime.

## Deliberately not implemented yet

- queue doorbell/head writes,
- live producer/consumer pointers,
- continuation-function submission,
- CPU-sequencer event execution,
- firmware loading,
- any MMIO write.

Those layers will be connected only after the offline representation, DMA model, and write policy are complete.
