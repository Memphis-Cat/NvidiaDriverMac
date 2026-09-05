# Windows-first development workflow

The Windows side exists to minimize Hackintosh boots.

## One-command offline preflight

From the repository on Windows 11:

```powershell
scripts\windows\offline-check.ps1
```

or double-click/run:

```text
scripts\windows\offline-check.bat
```

This does four things without loading any custom driver:

1. builds the portable C++20 core,
2. runs all offline tests,
3. captures ordinary Windows/PnP/NVIDIA metadata,
4. derives the exact PCI match string needed by PCIDriverKit.

The capture stage uses Windows PnP/CIM and `nvidia-smi` metadata only. It does not map BARs or access GPU MMIO.

Generated files live in `artifacts/` and are ignored by Git.

## PCI match format

Apple's PCIDriverKit entitlement uses the same PCI matching keys as the DEXT `Info.plist`. For `IOPCIPrimaryMatch`, the 32-bit value is device ID followed by vendor ID. Example:

```text
vendor 10DE + device 2489 => 0x248910de
```

`prepare-target.ps1` derives this from the actual Windows hardware report instead of assuming every RTX 3060 Ti has the same device ID.

## Why we collect this before macOS

The first DEXT should claim exactly the intended GPU. A broad NVIDIA wildcard would be convenient for development but is unnecessary risk and creates more signing/entitlement ambiguity.
