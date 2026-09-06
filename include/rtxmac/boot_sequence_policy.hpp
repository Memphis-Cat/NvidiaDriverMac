#pragma once

#include "rtxmac/falcon_policy.hpp"
#include "rtxmac/gsp_manifest.hpp"

#include <cstddef>
#include <cstdint>

namespace rtxmac::nvidia::gsp {

enum class BootSequencePolicyFailure : std::uint8_t {
  None = 0,
  InvalidManifest,
  InvalidSequence,
  InvalidExpectations,
  WrongPhaseCount,
  WrongPhaseOrder,
  FalconActionDenied,
  UnexpectedAction,
  MissingAction,
  UnexpectedCheck,
  MissingCheck,
};

struct BootSequencePolicyExpectations {
  // Independent dynamic facts used to validate the few direct actions that are
  // not fully determined by the static GA102 register allowlist.
  std::uint64_t libosInitArguments{};
  std::uint32_t gspAppVersion{};
};

struct BootSequencePolicyReport {
  bool valid{};
  BootSequencePolicyFailure failure{BootSequencePolicyFailure::None};
  std::size_t phaseIndex{};
  std::size_t actionIndex{};
  std::size_t actionCount{};
  std::size_t checkCount{};
};

// Audit the complete GA102 bootstrap sequence independently of the planner.
// Besides fixed phase order and the Falcon static allowlist, this requires the
// exact dynamic libOS mailbox pointer and parsed GSP appVersion. Check-only
// phases are forbidden from smuggling in otherwise-allowed writes.
[[nodiscard]] BootSequencePolicyReport CheckGa102BootSequencePolicy(
    const BootManifest& manifest,
    const BootSequence& sequence,
    const BootSequencePolicyExpectations& expectations) noexcept;

} // namespace rtxmac::nvidia::gsp
