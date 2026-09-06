#include "rtxmac/boot_sequence_policy.hpp"

#include <array>
#include <limits>

namespace rtxmac::nvidia::gsp {
namespace {
constexpr std::uint32_t kWpr2Hi = 0x001FA828u;
constexpr std::uint32_t kSec2Mailbox0 = 0x00840040u;
constexpr std::uint32_t kGspRiscvCpuCtl = 0x00111388u;
constexpr std::uint32_t kRiscvActiveMask = 1u << 7u;
constexpr std::uint64_t kStatusQueueEntryOffField = 28ull;
constexpr std::uint32_t kExpectedQueueEntryOff = 0x1000u;

constexpr std::array kExpectedPhases{
    BootPhase::PrefillBootstrapRpcRecords,
    BootPhase::ResetGspForFrts,
    BootPhase::ExecuteFrtsFwsec,
    BootPhase::VerifyWpr2,
    BootPhase::ResetGspForRiscv,
    BootPhase::ProgramLibosMailbox,
    BootPhase::ResetSec2,
    BootPhase::ExecuteSec2Booter,
    BootPhase::VerifySec2Booter,
    BootPhase::ReleaseGspRiscv,
    BootPhase::VerifyGspRiscv,
    BootPhase::WaitStatusQueue,
};

bool ExactCheck(const Check& check,
                CheckKind kind,
                std::uint64_t address,
                std::uint32_t mask,
                std::uint32_t value) noexcept {
  return check.kind == kind &&
         check.addressOrOffset == address &&
         check.mask == mask &&
         check.value == value;
}

bool PhaseCheckPolicy(const BootManifest& manifest,
                      const PhasePlan& phase) noexcept {
  switch (phase.phase) {
    case BootPhase::VerifyWpr2:
      return phase.checks.size() == 1u &&
          ExactCheck(phase.checks[0], CheckKind::MmioNonZero,
                     kWpr2Hi, 0xFFFFFFFFu, 0u);

    case BootPhase::VerifySec2Booter:
      return phase.checks.size() == 1u &&
          ExactCheck(phase.checks[0], CheckKind::MmioMaskEqual,
                     kSec2Mailbox0, 0xFFFFFFFFu, 0u);

    case BootPhase::VerifyGspRiscv:
      return phase.checks.size() == 1u &&
          ExactCheck(phase.checks[0], CheckKind::MmioMaskEqual,
                     kGspRiscvCpuCtl, kRiscvActiveMask, kRiscvActiveMask);

    case BootPhase::WaitStatusQueue:
      if (manifest.queues.statusQueueOffset >
          std::numeric_limits<std::uint64_t>::max() - kStatusQueueEntryOffField) {
        return false;
      }
      return phase.checks.size() == 1u &&
          ExactCheck(phase.checks[0], CheckKind::SharedMemoryU32Equal,
                     manifest.queues.statusQueueOffset + kStatusQueueEntryOffField,
                     0xFFFFFFFFu, kExpectedQueueEntryOff);

    default:
      return phase.checks.empty();
  }
}
} // namespace

BootSequencePolicyReport CheckGa102BootSequencePolicy(
    const BootManifest& manifest,
    const BootSequence& sequence) noexcept {
  BootSequencePolicyReport out{};
  out.actionIndex = std::numeric_limits<std::size_t>::max();

  if (!manifest.valid) {
    out.failure = BootSequencePolicyFailure::InvalidManifest;
    return out;
  }
  if (!sequence.valid || !sequence.executableWithCurrentCore) {
    out.failure = BootSequencePolicyFailure::InvalidSequence;
    return out;
  }
  if (sequence.phases.size() != kExpectedPhases.size()) {
    out.failure = BootSequencePolicyFailure::WrongPhaseCount;
    return out;
  }

  for (std::size_t i = 0u; i < sequence.phases.size(); ++i) {
    out.phaseIndex = i;
    const PhasePlan& phase = sequence.phases[i];
    if (phase.phase != kExpectedPhases[i]) {
      out.failure = BootSequencePolicyFailure::WrongPhaseOrder;
      return out;
    }

    if (!phase.actions.empty()) {
      falcon::Plan actionPlan{true, phase.actions};
      const auto actionReport = falcon::CheckGa102PlanPolicy(actionPlan);
      out.actionCount += phase.actions.size();
      if (!actionReport.valid) {
        out.failure = BootSequencePolicyFailure::FalconActionDenied;
        out.actionIndex = actionReport.firstDeniedIndex;
        return out;
      }
    }

    out.checkCount += phase.checks.size();
    if (!PhaseCheckPolicy(manifest, phase)) {
      out.failure = phase.checks.empty()
          ? BootSequencePolicyFailure::MissingCheck
          : BootSequencePolicyFailure::UnexpectedCheck;
      return out;
    }
  }

  out.valid = true;
  out.failure = BootSequencePolicyFailure::None;
  out.phaseIndex = sequence.phases.size();
  return out;
}

} // namespace rtxmac::nvidia::gsp
