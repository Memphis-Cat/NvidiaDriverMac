#include "rtxmac/boot_sequence_policy.hpp"

#include <array>
#include <limits>

namespace rtxmac::nvidia::gsp {
namespace {
constexpr std::uint32_t kWpr2Hi = 0x001FA828u;
constexpr std::uint32_t kGspMailbox0 = 0x00110040u;
constexpr std::uint32_t kGspMailbox1 = 0x00110044u;
constexpr std::uint32_t kGspFalconOs = 0x00110080u;
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

bool ExactWrite32(const falcon::Action& action,
                  std::uint32_t address,
                  std::uint32_t value) noexcept {
  return action.kind == falcon::ActionKind::Write32 &&
         action.address == address &&
         action.value == value &&
         action.mask == 0xFFFFFFFFu &&
         action.timeoutMs == 0u;
}

BootSequencePolicyFailure PhaseActionShapePolicy(
    const PhasePlan& phase,
    const BootSequencePolicyExpectations& expectations) noexcept {
  switch (phase.phase) {
    case BootPhase::PrefillBootstrapRpcRecords:
    case BootPhase::VerifyWpr2:
    case BootPhase::VerifySec2Booter:
    case BootPhase::VerifyGspRiscv:
    case BootPhase::WaitStatusQueue:
      return phase.actions.empty()
          ? BootSequencePolicyFailure::None
          : BootSequencePolicyFailure::UnexpectedAction;

    case BootPhase::ProgramLibosMailbox: {
      if (phase.actions.size() < 2u) return BootSequencePolicyFailure::MissingAction;
      if (phase.actions.size() != 2u) return BootSequencePolicyFailure::UnexpectedAction;
      const auto low = static_cast<std::uint32_t>(expectations.libosInitArguments);
      const auto high = static_cast<std::uint32_t>(expectations.libosInitArguments >> 32u);
      return ExactWrite32(phase.actions[0], kGspMailbox0, low) &&
             ExactWrite32(phase.actions[1], kGspMailbox1, high)
          ? BootSequencePolicyFailure::None
          : BootSequencePolicyFailure::UnexpectedAction;
    }

    case BootPhase::ReleaseGspRiscv:
      if (phase.actions.empty()) return BootSequencePolicyFailure::MissingAction;
      if (phase.actions.size() != 1u) return BootSequencePolicyFailure::UnexpectedAction;
      return ExactWrite32(phase.actions[0], kGspFalconOs, expectations.gspAppVersion)
          ? BootSequencePolicyFailure::None
          : BootSequencePolicyFailure::UnexpectedAction;

    case BootPhase::ResetGspForFrts:
    case BootPhase::ExecuteFrtsFwsec:
    case BootPhase::ResetGspForRiscv:
    case BootPhase::ResetSec2:
    case BootPhase::ExecuteSec2Booter:
      return phase.actions.empty()
          ? BootSequencePolicyFailure::MissingAction
          : BootSequencePolicyFailure::None;
  }
  return BootSequencePolicyFailure::UnexpectedAction;
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
    const BootSequence& sequence,
    const BootSequencePolicyExpectations& expectations) noexcept {
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
  if (expectations.libosInitArguments == 0u ||
      (expectations.libosInitArguments & 0xFFFull) != 0u) {
    out.failure = BootSequencePolicyFailure::InvalidExpectations;
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

    const auto shapeFailure = PhaseActionShapePolicy(phase, expectations);
    if (shapeFailure != BootSequencePolicyFailure::None) {
      out.failure = shapeFailure;
      return out;
    }

    out.actionCount += phase.actions.size();
    if (!phase.actions.empty()) {
      falcon::Plan actionPlan{true, phase.actions};
      const auto actionReport = falcon::CheckGa102PlanPolicy(actionPlan);
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
