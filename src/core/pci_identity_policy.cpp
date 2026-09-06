#include "rtxmac/pci_identity.hpp"

namespace rtxmac {

bool IsNvidia(const PciIdentity& id) noexcept {
  return id.vendor == 0x10DEu;
}

bool IsKnownRtx3060Ti(const PciIdentity& id) noexcept {
  // Confirmed GA104 RTX 3060 Ti ID used by the current DriverKit personality.
  // Keep this intentionally narrow until additional device IDs are validated.
  return IsNvidia(id) && id.device == 0x2489u;
}

} // namespace rtxmac
