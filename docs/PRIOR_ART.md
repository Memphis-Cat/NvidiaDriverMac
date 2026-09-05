# Prior art and provenance

We study existing projects, but NvidiaDriverMac begins with independently written scaffolding and a deliberately small ABI.

## tinygrad / TinyGPU

Repository: `tinygrad/tinygrad` — MIT licensed.

Relevant ideas: PCIDriverKit attachment, BAR mapping, DriverKit DMA allocation, userspace PCI transport, NVIDIA GSP/MMU/RPC work in tinygrad's NVIDIA runtime.

## mac-amdgpu

Repository: `lemonade-sdk/mac-amdgpu` — MIT licensed.

Relevant ideas: organizing a modern Linux-derived GPU bring-up flow behind a native PCIDriverKit system extension on macOS Tahoe; staged diagnostics and userspace ABI design.

## NVIDIA open-gpu-kernel-modules

Repository: `NVIDIA/open-gpu-kernel-modules`.

Files carry their own SPDX terms; many are MIT or dual MIT/GPL. Do not copy a file without checking its SPDX header and preserving required notices. Relevant ideas include Ampere+ GSP-RM, PCI/resource management and GPU firmware interfaces.

## Mesa NVK / NAK

Mesa's open NVIDIA Vulkan driver and shader compiler are future graphics references. Their licenses/provenance must be reviewed before code is imported.

## Rule for copied code

Any future copied or substantially derived source must:

1. identify the upstream file/commit,
2. preserve the upstream copyright/license notices,
3. remain compatible with this repository's distribution terms, and
4. be called out in a provenance note or THIRD_PARTY file.
