# NVIDIA firmware inspection

The firmware parser is deliberately offline and read-only. It does not upload firmware, patch a GPU, or perform MMIO.

## Supported structure

The current parser understands the NVIDIA firmware container pieces needed by the GSP booter path:

- `nvfw_bin_hdr` (24 bytes)
- `nvfw_hs_header_v2` (36 bytes)
- `nvfw_hs_load_header_v2` (20-byte base header)
- `nvfw_hs_load_header_v2_app` (16 bytes per app)

It validates all important ranges before exposing offsets to later code:

- logical binary size
- HS header
- load header + app table
- firmware data image
- production signature range
- patch-location/patch-signature pointers
- first app code/data ranges
- OS code/data ranges

No pointer from a firmware blob should ever be trusted before passing this validation layer.

## Windows CLI

```text
rtxmac-offline firmware-info booter_load-570.144.bin
```

This prints parsed metadata and returns non-zero for structurally invalid input.

The layout is cross-checked against NVIDIA's open kernel-module firmware structures and tinygrad's Ampere GSP booter path.
