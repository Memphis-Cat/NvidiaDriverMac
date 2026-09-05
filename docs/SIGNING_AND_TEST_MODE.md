# DriverKit signing and local test mode

DriverKit normally requires Apple-granted entitlements. The PCI transport entitlement identifies the PCI devices a DEXT may claim.

Apple's current DriverKit documentation also explicitly permits local development/testing while entitlement requests are pending by temporarily disabling the relevant system security checks. Apple's System Extensions documentation describes using developer mode for repeated extension replacement during development.

For RTXMac we keep two build concepts separate:

1. **CI / compile-only** — no signing and no installation. Used on GitHub's macOS 26 Intel runner to catch source/Xcode/DriverKit errors without touching the Hackintosh.
2. **Local hardware prototype** — only when the hardware-test gate is reached. We will first inspect the Hackintosh's current SIP/developer-mode state and choose the least disruptive supported development signing path.

Do not disable additional protections preemptively. We will not change SIP, boot arguments or OpenCore settings merely to make an early build install.

Official Apple references:

- Requesting Entitlements for DriverKit Development
- Debugging and testing system extensions
- `com.apple.developer.driverkit.transport.pci`
- PCIDriverKit
