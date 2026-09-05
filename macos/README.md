# macOS transport

This directory contains the beginnings of the native DriverKit side. It is **not ready to install yet**.

Before generating/signing an Xcode project we need the exact target GPU PCI ID from the Windows hardware capture and the Apple DriverKit signing/entitlement details that will be used for the test machine.

The first DEXT is intentionally read-only. Its `Start` path opens the PCI provider and logs identity/BAR metadata. It does not reset the GPU, change PCI command bits, map/write registers, load firmware, or alter clocks/power.
