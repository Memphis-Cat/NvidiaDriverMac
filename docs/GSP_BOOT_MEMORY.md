# GSP boot-memory planning

The first GSP-memory work is modeled offline before DriverKit is allowed to allocate or prepare anything.

## Default queue layout

The current Ampere/tinygrad-style queue setup uses two `0x40000`-byte queues. With 4 KiB pages and 8-byte PTEs:

- command + status queue pages: `128`
- PTE entries including the PTE-storage page: `129`
- page-table storage: `0x1000`
- command queue offset: `0x1000`
- status queue offset: `0x41000`
- total backing allocation: **`0x81000` bytes**

This is the exact allocation size that exposed the 32-segment DriverKit truncation problem in the Intel Mac Pro TinyGPU report.

## DriverKit-safe worst-case plan

A `0x81000` allocation is 129 4 KiB pages. If every page is a separate DMA segment and DriverKit can return at most 32 segments per preparation, the conservative plan is:

1. `0x00000..0x1FFFF` (`0x20000`)
2. `0x20000..0x3FFFF` (`0x20000`)
3. `0x40000..0x5FFFF` (`0x20000`)
4. `0x60000..0x7FFFF` (`0x20000`)
5. `0x80000..0x80FFF` (`0x01000`)

Runtime coverage validation remains mandatory. A preparation that represents fewer bytes than requested is an error, never a partial success.

## Cached GSP arguments

The portable core also serializes the 72-byte `GSP_ARGUMENTS_CACHED` block needed for initial queue setup. For now it fills only the fields required for the queue model:

- shared-memory physical address
- page-table entry count
- command queue offset
- status queue offset
- `bDmemStack = true`

All other fields remain zero until their semantics are explicitly needed and tested.
