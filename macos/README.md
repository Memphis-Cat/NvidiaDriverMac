# macOS transport

This directory contains the native DriverKit research transport and host app. It is **not ready for a live GPU boot attempt yet**.

GitHub CI generates the Xcode project and compile-checks both the DEXT and Swift host on macOS 26 Intel without signing. A real installation still needs the exact target GPU PCI/subsystem identity and the Apple DriverKit signing/entitlement setup for the test machine.

`RTXMacDriver::Start_Impl()` is intentionally read-only. The host can validate a package, stage its five cold SYSRAM buffers, read the allow-listed MMU boundary, rebuild lower WPR/VRAM placements, construct and retain the ten generated GSP boot buffers and their complete address graph, and read PCI/GSP system information. It cannot reset the GPU, change PCI command bits, write MMIO/PRAMIN, execute prepared DMA or Falcon firmware, or start GSP-RM.

For the eventual single-session evidence collection, export `RTXMac-Diagnostics.json` from the host and run:

```bash
./scripts/macos/collect-diagnostics.sh /path/to/RTXMac-Diagnostics.json
```

The script writes one ZIP on the Desktop containing the host report, system information, extension state, I/O registry evidence, DMAR context, and RTXMac logs.
