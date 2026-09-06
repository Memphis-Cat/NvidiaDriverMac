#include "rtxmac/boot_orchestrator.hpp"

namespace rtxmac::nvidia::gsp {

BootAttempt BeginBootAttempt(
    const BootManifest& manifest,
    const BootSequence& sequence,
    const BootSequencePolicyExpectations& expectations,
    const BootCommitPrerequisites& prerequisites) noexcept {
  BootAttempt out{};
  out.preflight = CheckBootCommitPreflight(prerequisites);
  out.sequencePolicy = CheckGa102BootSequencePolicy(manifest, sequence, expectations);
  if (!out.preflight.ready || !out.sequencePolicy.valid) {
    out.state = BootAttemptState::Rejected;
    return out;
  }
  out.state = BootAttemptState::Ready;
  return out;
}

BootAttemptEventStatus RecordBootPhaseResult(
    BootAttempt& attempt,
    const BootSequence& sequence,
    std::size_t phaseIndex,
    bool succeeded,
    bool hardwareActionsStarted) noexcept {
  if (attempt.state != BootAttemptState::Ready &&
      attempt.state != BootAttemptState::Running) {
    return BootAttemptEventStatus::NotRunnable;
  }
  if (!sequence.valid || phaseIndex != attempt.nextPhaseIndex ||
      phaseIndex >= sequence.phases.size()) {
    return BootAttemptEventStatus::WrongPhase;
  }

  const PhasePlan& phase = sequence.phases[phaseIndex];
  const bool hasActions = !phase.actions.empty();
  if (succeeded && hasActions != hardwareActionsStarted) {
    return BootAttemptEventStatus::InvalidHardwareActionFlag;
  }
  if (!hasActions && hardwareActionsStarted) {
    return BootAttemptEventStatus::InvalidHardwareActionFlag;
  }

  if (phase.phase == BootPhase::ResetGspForFrts && hardwareActionsStarted) {
    attempt.firstResetCommitCrossed = true;
  }

  if (!succeeded) {
    attempt.recovery = RecoveryForBootFailure(phase.phase, hardwareActionsStarted);
    if (attempt.firstResetCommitCrossed && !attempt.recovery.fullGpuResetRequired) {
      attempt.recovery = RecoveryForBootFailure(BootPhase::ResetGspForFrts, true);
    }
    attempt.state = attempt.recovery.fullGpuResetRequired
        ? BootAttemptState::FailedNeedsGpuReset
        : BootAttemptState::FailedColdReversible;
    return BootAttemptEventStatus::Ok;
  }

  ++attempt.nextPhaseIndex;
  if (attempt.nextPhaseIndex == sequence.phases.size()) {
    attempt.state = BootAttemptState::Succeeded;
  } else {
    attempt.state = BootAttemptState::Running;
  }
  return BootAttemptEventStatus::Ok;
}

BootAttemptEventStatus RecordBootResetPostcheck(
    BootAttempt& attempt,
    const ResetPostcheckReport& postcheck) noexcept {
  if (attempt.state != BootAttemptState::FailedNeedsGpuReset) {
    return BootAttemptEventStatus::NotAwaitingReset;
  }
  attempt.resetPostcheck = postcheck;
  attempt.state = postcheck.recovered &&
                  postcheck.mayReleasePinnedBootBuffers &&
                  postcheck.mayBeginColdRebuild &&
                  !postcheck.keepPinnedAndRequireReset
      ? BootAttemptState::ResetRecovered
      : BootAttemptState::Unrecovered;
  return BootAttemptEventStatus::Ok;
}

} // namespace rtxmac::nvidia::gsp
