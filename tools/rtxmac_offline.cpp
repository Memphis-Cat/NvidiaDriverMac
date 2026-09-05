#include "rtxmac/pci_identity.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
  if (argc != 3 || std::string_view(argv[1]) != "parse") {
    std::cerr << "usage: rtxmac-offline parse \"PCI\\\\VEN_10DE&DEV_2489&SUBSYS_...\"\n";
    return 2;
  }

  const auto id = rtxmac::ParseWindowsPciHardwareId(argv[2]);
  if (!id) {
    std::cerr << "Could not parse PCI hardware ID.\n";
    return 1;
  }

  std::cout << rtxmac::Describe(*id) << '\n';
  std::cout << "nvidia=" << (rtxmac::IsNvidia(*id) ? "yes" : "no") << '\n';
  std::cout << "known_rtx3060ti=" << (rtxmac::IsKnownRtx3060Ti(*id) ? "yes" : "no") << '\n';
  return 0;
}
