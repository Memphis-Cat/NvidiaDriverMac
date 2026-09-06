#pragma once

#include "rtxmac/boot_preflight.hpp"
#include "rtxmac/boot_recovery.hpp"
#include "rtxmac/boot_sequence_policy.hpp"
#include "rtxmac/reset_postcheck.hpp"

#include <cstddef>
#include <cstdint>

namespace rtxmac::nvidia::gsp {

enum class BootAttemptState : std::uint8_t {
  Cold = 0,
  Ready,
  Running,
  Succeeded,
  Rejected,
  FailedColdReversible,
  FailedNeedsGpuReset,
  ResetRecovered,
  Unrecovered,
};

enum class BootAttemptEventStatus : std::uint8_t {
  Ok = 0,
  NotRunnable,
  WrongPhase,
  InvalidHardwareActionFlag,
  NotAwaitingReset,
};

struct BootAttempt {
  BootAttemptState state{BootAttemptState::Cold};
  BootPreflightReport preflight{};
  BootSequencePolicyReport sequencePolicy{};
  BootFailureRecovery recovery{};
  ResetPostcheckReport resetPostcheck{};
  std::size_t nextPhaseIndex{};
  bool firstResetCommitCrossed{};
};

// Construct a fresh attempt. Both the independent full-sequence policy audit
// and the final pre-commit software gate must pass before the attempt becomes
// runnable. This performs no hardware access.
[[nodiscard]] BootAttempt BeginBootAttempt(
    const BootManifest& manifest,
    const BootSequence& sequence,
    const BootCommitPrerequisites& prerequisites) noexcept;

// Record exactly one phase result in strict sequence order. A successful phase
// containing hardware actions must report hardwareActionsStarted=true; a phase
// with no actions must report false. Once a failure occurs, no further phase can
// be recorded on this attempt. The first live ResetGspForFrts action is the
// commit boundary used by RecoveryForBootFailure().
[[nodiscard]] BootAttemptEventStatus RecordBootPhaseResult(
    BootAttempt& attempt,
    const BootSequence& sequence,
    std::size_t phaseIndex,
    bool succeeded,
    bool hardwareActionsStarted) noexcept;

// Complete recovery after a failed committed attempt. Even a successful GPU
// reset does not resume the old attempt: it transitions to ResetRecovered and a
// brand-new BeginBootAttempt() is required with newly resolved DMA/artifacts.
[[nodiscard]] BootAttemptEventStatus RecordBootResetPostcheck(
    BootAttempt& attempt,
    const ResetPostcheckReport& postcheck) noexcept;

[[nodiscard]] constexpr bool BootAttemptTerminal(const BootAttempt& attempt) noexcept {
  switch (attempt.state) {
    case BootAttemptState::Succeeded:
    case BootAttemptState::Rejected:
    case BootAttemptState::FailedColdReversible:
    case BootAttemptState::ResetRecovered:
    case BootAttemptState::Unrecovered:
      return true;
    default:
      return false;
  }
}

} // namespace rtxmac::nvidia::gsp
