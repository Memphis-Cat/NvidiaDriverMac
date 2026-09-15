#pragma once

#include "rtxmac/gsp_bootstrap.hpp"
#include "rtxmac/pci_bar.hpp"

#include <DriverKit/IOReturn.h>
#include <PCIDriverKit/PCIDriverKit.h>

#include <array>
#include <cstdint>

struct RTXMacSystemBar {
  std::uint8_t barIndex{};
  std::uint8_t memoryIndex{};
  std::uint8_t driverKitType{};
  std::uint64_t size{};
  rtxmac::pci::DecodedBar decoded{};
};

struct RTXMacSystemInfoSnapshot {
  rtxmac::nvidia::gsp::GspSystemInfoInputs inputs{};
  std::array<RTXMacSystemBar, 3u> bars{}; // BAR0, BAR1, BAR3
  std::uint8_t bus{};
  std::uint8_t device{};
  std::uint8_t function{};
};

// Collect the GSP_SET_SYSTEM_INFO fields using only read-only PCI/DriverKit
// operations. No command-register change, DMA preparation, MMIO write, reset,
// or firmware execution occurs.
[[nodiscard]] kern_return_t RTXMacCollectSystemInfo(
    IOPCIDevice* pci,
    RTXMacSystemInfoSnapshot* out) noexcept;
