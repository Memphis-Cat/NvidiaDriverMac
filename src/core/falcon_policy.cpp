#include "rtxmac/falcon_policy.hpp"

#include <array>
#include <limits>

namespace rtxmac::nvidia::falcon {
namespace {
struct RegisterRule {
  std::uint32_t relative{};
  std::uint32_t mask{};
};

constexpr std::uint32_t kGspBase = 0x00110000u;
constexpr std::uint32_t kSec2Base = 0x00840000u;
constexpr std::uint32_t kCpuCtl = 0x100u;

constexpr std::array kWriteRules{
    RegisterRule{0x03C0u, 0x00000001u}, // engine reset
    RegisterRule{0x1668u, 0x00000111u}, // RISC-V BCR control
    RegisterRule{0x0084u, 0xFFFFFFFFu}, // Falcon RM/chip id
    RegisterRule{0x0624u, 0x00000080u}, // FBIF allow physical/no context
    RegisterRule{0x010Cu, 0x00000007u}, // DMA control
    RegisterRule{0x0600u, 0x00000007u}, // FBIF transaction config 0
    RegisterRule{0x0110u, 0xFFFFFFFFu}, // DMA base low
    RegisterRule{0x0128u, 0x000001FFu}, // DMA base high
    RegisterRule{0x0114u, 0x00FFFFFFu}, // DMA memory offset
    RegisterRule{0x011Cu, 0xFFFFFFFFu}, // DMA framebuffer offset
    RegisterRule{0x0118u, 0x0001773Fu}, // DMA command
    RegisterRule{0x1210u, 0xFFFFFFFFu}, // BROM parameter address
    RegisterRule{0x119Cu, 0xFFFFFFFFu}, // BROM engine id mask
    RegisterRule{0x1198u, 0x000000FFu}, // BROM ucode id
    RegisterRule{0x1180u, 0x000000FFu}, // BROM module select
    RegisterRule{0x0104u, 0xFFFFFFFFu}, // boot vector
    RegisterRule{0x0040u, 0xFFFFFFFFu}, // mailbox 0
    RegisterRule{0x0044u, 0xFFFFFFFFu}, // mailbox 1
};

constexpr std::array kReadRules{
    RegisterRule{0x00F4u, (1u << 10u) | (1u << 12u)}, // HWCFG2 riscv/scrubbing
    RegisterRule{0x0118u, (1u << 0u) | (1u << 1u)},   // DMA full/idle
    RegisterRule{0x1668u, 0x00000111u},               // RISC-V BCR
    RegisterRule{0x0100u, 0xFFFFFFFFu},               // CPUCTL, alias/start/halted
};

bool DecodeEngineAddress(std::uint32_t address,
                         std::uint32_t* relative) noexcept {
  if (!relative) return false;
  if (address >= kGspBase && address < kGspBase + 0x2000u) {
    *relative = address - kGspBase;
    return true;
  }
  if (address >= kSec2Base && address < kSec2Base + 0x2000u) {
    *relative = address - kSec2Base;
    return true;
  }
  return false;
}

template <std::size_t N>
const RegisterRule* FindRule(const std::array<RegisterRule, N>& rules,
                             std::uint32_t relative) noexcept {
  for (const auto& rule : rules) {
    if (rule.relative == relative) return &rule;
  }
  return nullptr;
}

bool ValueFitsMask(const Action& action) noexcept {
  return (action.value & ~action.mask) == 0u;
}
} // namespace

ActionPolicyDecision CheckGa102ActionPolicy(const Action& action) noexcept {
  std::uint32_t relative = 0u;
  switch (action.kind) {
    case ActionKind::Write32:
    case ActionKind::MaskedWrite: {
      if (!DecodeEngineAddress(action.address, &relative)) {
        return ActionPolicyDecision::DeniedUnknownAddress;
      }
      const RegisterRule* rule = FindRule(kWriteRules, relative);
      if (!rule) return ActionPolicyDecision::DeniedUnknownAddress;
      if (action.mask == 0u || (action.mask & ~rule->mask) != 0u) {
        return ActionPolicyDecision::DeniedMask;
      }
      if (!ValueFitsMask(action)) return ActionPolicyDecision::DeniedValue;
      if (action.timeoutMs != 0u) return ActionPolicyDecision::DeniedTimeout;
      return ActionPolicyDecision::Allowed;
    }

    case ActionKind::PollMaskEqual:
    case ActionKind::IfMaskEqualBegin: {
      if (!DecodeEngineAddress(action.address, &relative)) {
        return ActionPolicyDecision::DeniedUnknownAddress;
      }
      const RegisterRule* rule = FindRule(kReadRules, relative);
      if (!rule || action.mask == 0u || (action.mask & ~rule->mask) != 0u) {
        return rule ? ActionPolicyDecision::DeniedMask
                    : ActionPolicyDecision::DeniedUnknownAddress;
      }
      if (!ValueFitsMask(action)) return ActionPolicyDecision::DeniedValue;
      if (action.kind == ActionKind::PollMaskEqual) {
        if (action.timeoutMs == 0u || action.timeoutMs > 60000u) {
          return ActionPolicyDecision::DeniedTimeout;
        }
      } else if (action.timeoutMs != 0u) {
        return ActionPolicyDecision::DeniedTimeout;
      }
      return ActionPolicyDecision::Allowed;
    }

    case ActionKind::DelayMilliseconds:
      if (action.address != 0u || action.mask != 0u || action.timeoutMs != 0u ||
          action.value == 0u || action.value > 10000u) {
        return ActionPolicyDecision::DeniedTimeout;
      }
      return ActionPolicyDecision::Allowed;

    case ActionKind::StartCpuRespectAlias:
      if (!DecodeEngineAddress(action.address, &relative) || relative != kCpuCtl ||
          action.value != 0u || action.mask != 0u || action.timeoutMs != 0u) {
        return ActionPolicyDecision::DeniedUnknownAddress;
      }
      return ActionPolicyDecision::RequiresStartCpuAliasSupport;

    case ActionKind::EndIf:
      return (action.address == 0u && action.value == 0u && action.mask == 0u &&
              action.timeoutMs == 0u)
          ? ActionPolicyDecision::Allowed
          : ActionPolicyDecision::DeniedControlFlow;
  }
  return ActionPolicyDecision::DeniedControlFlow;
}

PlanPolicyReport CheckGa102PlanPolicy(const Plan& plan) noexcept {
  PlanPolicyReport out{};
  out.actionCount = plan.actions.size();
  out.firstDeniedIndex = std::numeric_limits<std::size_t>::max();
  if (!plan.valid) {
    out.firstDeniedIndex = 0u;
    out.firstDenied = ActionPolicyDecision::DeniedControlFlow;
    return out;
  }

  bool inIf = false;
  for (std::size_t i = 0u; i < plan.actions.size(); ++i) {
    const Action& action = plan.actions[i];
    const auto decision = CheckGa102ActionPolicy(action);
    if (decision == ActionPolicyDecision::RequiresStartCpuAliasSupport) {
      out.requiresStartCpuAliasSupport = true;
    } else if (decision != ActionPolicyDecision::Allowed) {
      out.firstDeniedIndex = i;
      out.firstDenied = decision;
      return out;
    }

    if (action.kind == ActionKind::IfMaskEqualBegin) {
      if (inIf) {
        out.firstDeniedIndex = i;
        out.firstDenied = ActionPolicyDecision::DeniedControlFlow;
        return out;
      }
      inIf = true;
    } else if (action.kind == ActionKind::EndIf) {
      if (!inIf) {
        out.firstDeniedIndex = i;
        out.firstDenied = ActionPolicyDecision::DeniedControlFlow;
        return out;
      }
      inIf = false;
    }
  }

  if (inIf) {
    out.firstDeniedIndex = plan.actions.size();
    out.firstDenied = ActionPolicyDecision::DeniedControlFlow;
    return out;
  }
  out.valid = true;
  return out;
}

} // namespace rtxmac::nvidia::falcon
