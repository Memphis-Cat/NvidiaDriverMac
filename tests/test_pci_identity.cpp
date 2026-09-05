#include "rtxmac/pci_identity.hpp"

#include <cassert>
#include <iostream>

int main() {
  {
    const auto id = rtxmac::ParseWindowsPciHardwareId(
        "PCI\\VEN_10DE&DEV_2489&SUBSYS_39761462&REV_A1");
    assert(id.has_value());
    assert(id->vendor == 0x10DE);
    assert(id->device == 0x2489);
    assert(id->subsystemDevice == 0x3976);
    assert(id->subsystemVendor == 0x1462);
    assert(rtxmac::IsNvidia(*id));
    assert(rtxmac::IsKnownRtx3060Ti(*id));
  }

  {
    const auto id = rtxmac::ParseWindowsPciHardwareId(
        "pci\\ven_10de&dev_1234&subsys_00000000");
    assert(id.has_value());
    assert(rtxmac::IsNvidia(*id));
    assert(!rtxmac::IsKnownRtx3060Ti(*id));
  }

  assert(!rtxmac::ParseWindowsPciHardwareId("USB\\VID_10DE&PID_2489"));
  assert(!rtxmac::ParseWindowsPciHardwareId("PCI\\VEN_ZZZZ&DEV_2489"));

  std::cout << "rtxmac PCI identity tests passed\n";
  return 0;
}
