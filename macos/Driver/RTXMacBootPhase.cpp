#include "RTXMacBootPhase.hpp"

#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>

namespace {
constexpr std::uint64_t kPageBytes = 0x1000ull;

struct RegisterMapping {
  IOMemoryMap* map{nullptr};
  volatile std::uint32_t* reg{nullptr};
};

void ReleaseMapping(RegisterMapping* mapping) noexcept {
  if (!mapping) return;
  if (mapping->map) mapping->map->release();
  mapping->map = nullptr;
  mapping->reg = nullptr;
}

kern_return_t ReadBar0U32(IOMemoryDescriptor* bar0,
                          std::uint64_t bar0Size,
                          std::uint64_t offset,
                          std::uint32_t* value) noexcept {
  if (!bar0 || !value || (offset & 3ull) != 0ull ||
      offset > bar0Size || sizeof(std::uint32_t) > bar0Size - offset) {
    return kIOReturnBadArgument;
  }
  const std::uint64_t pageBase = offset & ~(kPageBytes - 1u);
  const std::uint64_t inPage = offset - pageBase;
  const std::uint64_t mapLength =
      pageBase + kPageBytes <= bar0Size ? kPageBytes : bar0Size - pageBase;
  if (inPage + sizeof(std::uint32_t) > mapLength) return kIOReturnNoResources;

  IOMemoryMap* map = nullptr;
  kern_return_t kr = bar0->CreateMapping(0, 0, pageBase, mapLength, 0, &map);
  if (kr != kIOReturnSuccess || !map) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }
  const auto base = static_cast<std::uintptr_t>(map->GetAddress());
  if (base == 0u) {
    map->release();
    return kIOReturnNoResources;
  }

  RegisterMapping mapping{map,
      reinterpret_cast<volatile std::uint32_t*>(base + inPage)};
  *value = *mapping.reg;
  ReleaseMapping(&mapping);
  return kIOReturnSuccess;
}

bool CheckSatisfied(const rtxmac::nvidia::gsp::Check& check,
                    std::uint32_t value) noexcept {
  using namespace rtxmac::nvidia::gsp;
  switch (check.kind) {
    case CheckKind::MmioMaskEqual:
    case CheckKind::SharedMemoryU32Equal:
      return (value & check.mask) == check.value;
    case CheckKind::MmioNonZero:
      return (value & check.mask) != 0u;
    case CheckKind::HostPreparationRequired:
      return false;
  }
  return false;
}

kern_return_t ExecuteCheck(const rtxmac::nvidia::gsp::Check& check,
                           IOMemoryDescriptor* bar0,
                           std::uint64_t bar0Size,
                           const RTXMacPreparedDmaBuffer* sharedQueueDma,
                           std::uint64_t maxWaitMilliseconds,
                           std::uint64_t* waitMilliseconds) noexcept {
  using namespace rtxmac::nvidia::gsp;
  if (!waitMilliseconds || maxWaitMilliseconds == 0u) return kIOReturnBadArgument;
  *waitMilliseconds = 0u;
  if (check.kind == CheckKind::HostPreparationRequired) return kIOReturnNotPermitted;

  for (;;) {
    std::uint32_t value = 0u;
    kern_return_t kr = kIOReturnError;
    if (check.kind == CheckKind::SharedMemoryU32Equal) {
      if (!sharedQueueDma) return kIOReturnBadArgument;
      kr = RTXMacReadPreparedDmaU32(sharedQueueDma, check.addressOrOffset, &value);
    } else {
      kr = ReadBar0U32(bar0, bar0Size, check.addressOrOffset, &value);
    }
    if (kr != kIOReturnSuccess) return kr;
    if (CheckSatisfied(check, value)) return kIOReturnSuccess;
    if (*waitMilliseconds >= maxWaitMilliseconds) return kIOReturnTimeout;
    IOSleep(1u);
    ++*waitMilliseconds;
  }
}
} // namespace

RTXMacBootPhaseResult RTXMacExecuteNextBootPhase(
    rtxmac::nvidia::gsp::BootAttempt& attempt,
    const rtxmac::nvidia::gsp::BootManifest& manifest,
    const rtxmac::nvidia::gsp::BootSequence& sequence,
    const rtxmac::nvidia::gsp::BootSequencePolicyExpectations& expectations,
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const RTXMacPreparedDmaBuffer* sharedQueueDma,
    bool writesEnabled,
    std::uint64_t maxPhaseWaitMilliseconds) noexcept {
  using namespace rtxmac::nvidia::gsp;

  RTXMacBootPhaseResult out{};
  out.status = kIOReturnNotPermitted;
  out.phaseIndex = attempt.nextPhaseIndex;

  if ((attempt.state != BootAttemptState::Ready &&
       attempt.state != BootAttemptState::Running) ||
      !bar0 || bar0Size == 0u || maxPhaseWaitMilliseconds == 0u) {
    return out;
  }

  const auto policy = CheckGa102BootSequencePolicy(manifest, sequence, expectations);
  if (!policy.valid || attempt.nextPhaseIndex >= sequence.phases.size()) return out;

  const std::size_t phaseIndex = attempt.nextPhaseIndex;
  const PhasePlan& phase = sequence.phases[phaseIndex];
  out.phaseIndex = phaseIndex;
  out.phase = phase.phase;

  // Hardware writes always require the separate live gate. Refusing here does
  // not mutate the BootAttempt, so a dry invocation cannot consume/fail it.
  if (!phase.actions.empty() && !writesEnabled) {
    out.status = kIOReturnNotPermitted;
    return out;
  }

  bool succeeded = true;
  bool hardwareActionsStarted = false;

  if (!phase.actions.empty()) {
    rtxmac::nvidia::falcon::Plan plan{true, phase.actions};
    const auto exec = RTXMacExecuteFalconPlan(
        bar0, bar0Size, plan, writesEnabled, maxPhaseWaitMilliseconds);
    out.waitMilliseconds += exec.totalWaitMilliseconds;
    hardwareActionsStarted = exec.hardwareActionsStarted;
    if (exec.status != kIOReturnSuccess) {
      out.status = exec.status;
      succeeded = false;
    }
  }

  if (succeeded) {
    for (const auto& check : phase.checks) {
      std::uint64_t checkWait = 0u;
      if (out.waitMilliseconds >= maxPhaseWaitMilliseconds) {
        out.status = kIOReturnTimeout;
        succeeded = false;
        break;
      }
      const std::uint64_t remaining = maxPhaseWaitMilliseconds - out.waitMilliseconds;
      const kern_return_t kr = ExecuteCheck(
          check, bar0, bar0Size, sharedQueueDma, remaining, &checkWait);
      out.waitMilliseconds += checkWait;
      if (kr != kIOReturnSuccess) {
        out.status = kr;
        succeeded = false;
        break;
      }
    }
  }

  out.hardwareActionsStarted = hardwareActionsStarted;
  out.checkPassed = succeeded;
  if (succeeded) out.status = kIOReturnSuccess;

  const auto event = RecordBootPhaseResult(
      attempt, sequence, phaseIndex, succeeded, hardwareActionsStarted);
  if (event != BootAttemptEventStatus::Ok) {
    out.status = kIOReturnNotPermitted;
    return out;
  }
  out.attemptUpdated = true;
  return out;
}
