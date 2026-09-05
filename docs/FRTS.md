# FWSEC / FRTS offline planning

This phase models the FRTS setup without executing a Falcon or writing GPU memory.

## FRTS command

The serializer emits the exact packed 44-byte command made from:

- `FWSECLIC_READ_VBIOS_DESC` (24 bytes)
  - version `1`
  - size `24`
  - flags `2`
- `FWSECLIC_FRTS_REGION_DESC` (20 bytes)
  - version `1`
  - size `20`
  - region offset in 4 KiB units
  - region size `0x100` pages (1 MiB)
  - media type `2` (framebuffer/VRAM)

For Ampere the current open bring-up path places the FRTS region at `VRAM size - 2 MiB`.

## FWSEC patch planner

Given a validated v3 FWSEC descriptor and its stored image, the planner:

1. locates the Falcon application-interface table at `IMEMLoadSize + InterfaceOffset`;
2. finds entry ID `4` (`DMEMMAPPER`);
3. validates the 64-byte v3 DMEM mapper;
4. validates the FRTS input command buffer and its advertised capacity;
5. identifies the mapper's `init_cmd` field;
6. validates the PKC signature destination at `IMEMLoadSize + PKCDataOffset`;
7. requires an RSA-3072 production-signature tail of 384 bytes;
8. only then allows an **offline copy** of the FWSEC image to be patched.

The transformation is still ordinary host memory. There is no MMIO, DMA, reset, Falcon execution, or firmware upload in this module.
