# VBIOS / FWSEC discovery

This parser is an offline metadata locator. It never patches or executes VBIOS microcode.

## Walk performed

1. Validate PCI expansion-ROM signature.
2. Walk PCIR/NVIDIA expansion images and calculate `expansionRomOffset` using NVIDIA's base/extension-ROM rules.
3. Search for a BIT header and validate its checksum.
4. Support both 6-byte and 8-byte BIT token formats.
5. Locate Falcon-data v2 token (`0x70`).
6. Locate Falcon ucode table v1.
7. Locate the production FWSEC entry (`ApplicationID 0x85`).
8. Validate the descriptor header and accept v3 descriptors of at least 44 bytes.
9. Decode the v3 IMEM/DMEM/interface/PKC metadata.
10. Verify the rounded stored image fits inside the extracted VBIOS image.

All pointer arithmetic is range-checked before dereferencing.

The logic is cross-checked against NVIDIA Open GPU Kernel Modules' `kernel_gsp_fwsec.c` and `kernel_gsp_vbios_tu102.c`, with the same packed-format sizes and pointer-base rules.
