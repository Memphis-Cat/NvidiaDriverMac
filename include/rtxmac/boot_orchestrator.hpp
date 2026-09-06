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
// (including dynamic libOS/appVersion expectations) and the final pre-commit
// software gate must pass before the attempt becomes runnable. No hardware I/O.
[[nodiscard]] BootAttempt BeginBootAttempt(
    const BootManifest& manifest,
    const BootSequence& sequence,
    const BootSequencePolicyExpectations& expectations,
    const BootCommitPrerequisites& prerequisites) noexcept;

[[nodiscard]] BootAttemptEventStatus RecordBootPhaseResult(
    BootAttempt& attempt,
    const BootSequence& sequence,
    std::size_t phaseIndex,
    bool succeeded,
    bool hardwareActionsStarted) noexcept;

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
