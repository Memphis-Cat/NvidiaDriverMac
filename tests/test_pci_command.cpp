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
  assert(enable.requestedSetMask == kPciDmaWritableMask);
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

  // Requesting just one allowed bit is valid, but the requested mask is part
  // of the authenticated plan shape and cannot be silently changed later.
  const auto memoryOnly =
      PlanPciCommandEnable(original, kPciCommandMemorySpace);
  assert(memoryOnly.status == PciCommandPlanStatus::Ok);
  assert(memoryOnly.newValue ==
         static_cast<std::uint16_t>(original | kPciCommandMemorySpace));
  assert(ValidatePciCommandTransition(memoryOnly, original));

  // A tampered plan that clears Bus Master must be rejected.
  auto tampered = enable;
  tampered.newValue = static_cast<std::uint16_t>(enable.newValue & ~kPciCommandBusMaster);
  tampered.changedMask = static_cast<std::uint16_t>(tampered.oldValue ^ tampered.newValue);
  assert(!ValidatePciCommandTransition(tampered, original));

  auto tamperedIntent = memoryOnly;
  tamperedIntent.requestedSetMask = kPciDmaWritableMask;
  assert(!ValidatePciCommandTransition(tamperedIntent, original));

  std::cout << "rtxmac PCI command transition tests passed\n";
  return 0;
}
