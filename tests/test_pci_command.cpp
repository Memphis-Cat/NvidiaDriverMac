#include "rtxmac/pci_command.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  using namespace rtxmac;

  // Preserve unrelated command bits while enabling Memory Space + Bus Master.
  const std::uint16_t original = 0x0141u; // unrelated bits + I/O Space set
  const auto enable = PlanPciCommandEnable(original);
  assert(enable.status == PciCommandPlanStatus::Ok);
  assert(enable.oldValue == original);
  assert(enable.rollbackValue == original);
  assert(enable.newValue == static_cast<std::uint16_t>(original | 0x0006u));
  assert(enable.changedMask == 0x0006u);
  assert(ValidatePciCommandTransition(enable, original));
  assert(!ValidatePciCommandTransition(enable, static_cast<std::uint16_t>(original | 0x0002u)));

  // Already-enabled state is a valid no-op and must stay bit-identical.
  const std::uint16_t already = 0x0207u;
  const auto noop = PlanPciCommandEnable(already);
  assert(noop.status == PciCommandPlanStatus::NoChange);
  assert(noop.newValue == already && noop.changedMask == 0u);
  assert(ValidatePciCommandTransition(noop, already));

  // We intentionally do not grant I/O-space or any other command bit through
  // this DMA transition policy.
  const auto bad = PlanPciCommandEnable(original, kPciCommandIoSpace);
  assert(bad.status == PciCommandPlanStatus::InvalidRequestedMask);
  assert(!ValidatePciCommandTransition(bad, original));

  // A tampered plan that clears Bus Master must be rejected.
  auto tampered = enable;
  tampered.newValue = static_cast<std::uint16_t>(enable.newValue & ~kPciCommandBusMaster);
  tampered.changedMask = static_cast<std::uint16_t>(tampered.oldValue ^ tampered.newValue);
  assert(!ValidatePciCommandTransition(tampered, original));

  std::cout << "rtxmac PCI command transition tests passed\n";
  return 0;
}
