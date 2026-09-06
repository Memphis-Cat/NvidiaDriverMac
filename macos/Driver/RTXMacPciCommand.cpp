#include "RTXMacPciCommand.hpp"

namespace {
std::uint16_t ReadCommand(IOPCIDevice* pci) noexcept {
  std::uint16_t value = 0u;
  pci->ConfigurationRead16(kIOPCIConfigurationOffsetCommand, &value);
  return value;
}

bool PlanShapeValid(const rtxmac::PciCommandTransition& plan) noexcept {
  using namespace rtxmac;
  if (plan.status != PciCommandPlanStatus::Ok &&
      plan.status != PciCommandPlanStatus::NoChange) return false;
  if (plan.rollbackValue != plan.oldValue) return false;
  if ((plan.changedMask & ~kPciDmaWritableMask) != 0u) return false;
  if (static_cast<std::uint16_t>(plan.oldValue ^ plan.newValue) != plan.changedMask) return false;
  if ((plan.newValue & ~kPciDmaWritableMask) !=
      (plan.oldValue & ~kPciDmaWritableMask)) return false;
  if ((plan.oldValue & kPciDmaWritableMask & ~plan.newValue) != 0u) return false;
  return true;
}
} // namespace

kern_return_t RTXMacApplyPciCommandTransition(
    IOPCIDevice* pci,
    const rtxmac::PciCommandTransition& plan,
    bool writesEnabled) noexcept {
  using namespace rtxmac;
  if (!writesEnabled) return kIOReturnNotPermitted;
  if (!pci || !PlanShapeValid(plan)) return kIOReturnBadArgument;

  const std::uint16_t observed = ReadCommand(pci);
  if (!ValidatePciCommandTransition(plan, observed)) return kIOReturnNotReady;
  if (plan.status == PciCommandPlanStatus::NoChange) return kIOReturnSuccess;

  pci->ConfigurationWrite16(kIOPCIConfigurationOffsetCommand, plan.newValue);
  if (ReadCommand(pci) == plan.newValue) return kIOReturnSuccess;

  // A failed/partial transition is immediately rolled back. ConfigurationWrite
  // has no useful error return in this DriverKit API, so readback is the commit
  // point and the rollback is verified the same way.
  pci->ConfigurationWrite16(kIOPCIConfigurationOffsetCommand, plan.rollbackValue);
  return ReadCommand(pci) == plan.rollbackValue ? kIOReturnIOError : kIOReturnError;
}

kern_return_t RTXMacRollbackPciCommandTransition(
    IOPCIDevice* pci,
    const rtxmac::PciCommandTransition& plan,
    bool writesEnabled) noexcept {
  if (!writesEnabled) return kIOReturnNotPermitted;
  if (!pci || !PlanShapeValid(plan)) return kIOReturnBadArgument;

  const std::uint16_t observed = ReadCommand(pci);
  if (observed == plan.rollbackValue) return kIOReturnSuccess;
  // Do not clobber a command register that changed in an unrelated way after
  // our transition. Rollback is valid only from the exact planned new state.
  if (observed != plan.newValue) return kIOReturnNotReady;

  pci->ConfigurationWrite16(kIOPCIConfigurationOffsetCommand, plan.rollbackValue);
  return ReadCommand(pci) == plan.rollbackValue ? kIOReturnSuccess : kIOReturnError;
}
