# First hardware test gate

Do **not** run this yet just because the files exist.

The first macOS hardware test should happen only after:

1. Windows CI passes for the portable core.
2. macOS 26 Intel CI compiles the DriverKit target.
3. The target GPU's exact Windows PCI hardware ID is captured and the DEXT match is narrowed to that exact device.
4. The signing/DriverKit entitlement path is known.
5. The diagnostics script has been reviewed and can collect the entire first result in one session.

## What the first test is allowed to do

- activate/attach the DEXT,
- open the target `IOPCIDevice`,
- read vendor/device/subsystem/revision PCI config fields,
- query BAR metadata,
- log those observations,
- collect macOS PCI, DriverKit, IOMMU/DMAR and RTXMac log information.

## Explicitly forbidden in the first test

- PCI config writes,
- MMIO writes,
- device resets,
- VBIOS writes/flashing,
- GSP/SEC2 firmware loading,
- DMA setup,
- clock, voltage, fan or power changes.

Those belong to later gates after the read-only transport is proven.
