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
  WrongPhaseCount,
  WrongPhaseOrder,
  FalconActionDenied,
  UnexpectedCheck,
  MissingCheck,
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
// This validates the fixed phase order, every Falcon/direct MMIO action through
// CheckGa102PlanPolicy(), and the exact non-action checks expected by the boot
// protocol. It performs no hardware access.
[[nodiscard]] BootSequencePolicyReport CheckGa102BootSequencePolicy(
    const BootManifest& manifest,
    const BootSequence& sequence) noexcept;

} // namespace rtxmac::nvidia::gsp
