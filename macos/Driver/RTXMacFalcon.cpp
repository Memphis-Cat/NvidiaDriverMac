#include "RTXMacFalcon.hpp"

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

kern_return_t MapRegister(IOMemoryDescriptor* bar0,
                          std::uint64_t bar0Size,
                          std::uint32_t offset,
                          RegisterMapping* out) noexcept {
  if (!bar0 || !out) return kIOReturnBadArgument;
  out->map = nullptr;
  out->reg = nullptr;
  if ((offset & 3u) != 0u ||
      static_cast<std::uint64_t>(offset) + sizeof(std::uint32_t) > bar0Size) {
    return kIOReturnNoResources;
  }

  const std::uint64_t pageBase = static_cast<std::uint64_t>(offset) & ~(kPageBytes - 1u);
  const std::uint64_t inPage = static_cast<std::uint64_t>(offset) - pageBase;
  const std::uint64_t mapLength =
      pageBase + kPageBytes <= bar0Size ? kPageBytes : bar0Size - pageBase;
  if (inPage + sizeof(std::uint32_t) > mapLength) return kIOReturnNoResources;

  IOMemoryMap* map = nullptr;
  const kern_return_t kr = bar0->CreateMapping(0, 0, pageBase, mapLength, 0, &map);
  if (kr != kIOReturnSuccess || !map) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }

  const auto base = static_cast<std::uintptr_t>(map->GetAddress());
  if (base == 0u) {
    map->release();
    return kIOReturnNoResources;
  }

  out->map = map;
  out->reg = reinterpret_cast<volatile std::uint32_t*>(base + inPage);
  return kIOReturnSuccess;
}

kern_return_t ReadRegister(IOMemoryDescriptor* bar0,
                           std::uint64_t bar0Size,
                           std::uint32_t offset,
                           std::uint32_t* value) noexcept {
  if (!value) return kIOReturnBadArgument;
  RegisterMapping mapping{};
  const kern_return_t kr = MapRegister(bar0, bar0Size, offset, &mapping);
  if (kr != kIOReturnSuccess) return kr;
  *value = *mapping.reg;
  ReleaseMapping(&mapping);
  return kIOReturnSuccess;
}

kern_return_t ExecuteConcreteWrite(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::falcon::Action& action) noexcept {
  using namespace rtxmac::nvidia::falcon;
  if (CheckGa102ActionPolicy(action) != ActionPolicyDecision::Allowed ||
      (action.kind != ActionKind::Write32 && action.kind != ActionKind::MaskedWrite)) {
    return kIOReturnNotPermitted;
  }

  RegisterMapping mapping{};
  kern_return_t kr = MapRegister(bar0, bar0Size, action.address, &mapping);
  if (kr != kIOReturnSuccess) return kr;

  if (action.kind == ActionKind::Write32) {
    *mapping.reg = action.value;
  } else {
    const std::uint32_t oldValue = *mapping.reg;
    const std::uint32_t newValue =
        (oldValue & ~action.mask) | (action.value & action.mask);
    *mapping.reg = newValue;
  }

  // BAR0 writes may be posted. A read from the same register is used only as an
  // ordering point; many Falcon registers change autonomously, so equality with
  // the just-written value is intentionally not required here.
  const volatile std::uint32_t postedWriteFlush = *mapping.reg;
  (void)postedWriteFlush;
  ReleaseMapping(&mapping);
  return kIOReturnSuccess;
}

bool CanConsumeWait(std::uint64_t total,
                    std::uint64_t addition,
                    std::uint64_t maximum) noexcept {
  return addition <= maximum && total <= maximum - addition;
}
} // namespace

RTXMacFalconExecutionResult RTXMacExecuteFalconPlan(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::falcon::Plan& plan,
    bool writesEnabled,
    std::uint64_t maxTotalWaitMilliseconds) noexcept {
  using namespace rtxmac::nvidia::falcon;

  RTXMacFalconExecutionResult out{};
  out.status = kIOReturnNotPermitted;
  if (!writesEnabled) return out;
  if (!bar0 || bar0Size == 0u || maxTotalWaitMilliseconds == 0u) {
    out.status = kIOReturnBadArgument;
    return out;
  }

  const auto report = CheckGa102PlanPolicy(plan);
  if (!report.valid) {
    out.status = kIOReturnNotPermitted;
    out.actionIndex = report.firstDeniedIndex;
    return out;
  }

  bool skippingConditionalBody = false;
  for (std::size_t i = 0u; i < plan.actions.size(); ++i) {
    out.actionIndex = i;
    const Action& action = plan.actions[i];

    if (skippingConditionalBody && action.kind != ActionKind::EndIf) {
      ++out.actionsCompleted;
      continue;
    }

    switch (action.kind) {
      case ActionKind::Write32:
      case ActionKind::MaskedWrite: {
        const kern_return_t kr = ExecuteConcreteWrite(bar0, bar0Size, action);
        if (kr != kIOReturnSuccess) {
          out.status = kr;
          return out;
        }
        out.hardwareActionsStarted = true;
        break;
      }

      case ActionKind::PollMaskEqual: {
        std::uint64_t actionWait = 0u;
        for (;;) {
          std::uint32_t value = 0u;
          const kern_return_t kr = ReadRegister(bar0, bar0Size, action.address, &value);
          if (kr != kIOReturnSuccess) {
            out.status = kr;
            return out;
          }
          if ((value & action.mask) == action.value) break;
          if (actionWait >= action.timeoutMs ||
              !CanConsumeWait(out.totalWaitMilliseconds, 1u, maxTotalWaitMilliseconds)) {
            out.status = kIOReturnTimeout;
            return out;
          }
          IOSleep(1u);
          ++actionWait;
          ++out.totalWaitMilliseconds;
        }
        break;
      }

      case ActionKind::DelayMilliseconds:
        if (!CanConsumeWait(out.totalWaitMilliseconds,
                            action.value,
                            maxTotalWaitMilliseconds)) {
          out.status = kIOReturnTimeout;
          return out;
        }
        IOSleep(action.value);
        out.totalWaitMilliseconds += action.value;
        break;

      case ActionKind::StartCpuRespectAlias: {
        std::uint32_t cpuCtl = 0u;
        kern_return_t kr = ReadRegister(bar0, bar0Size, action.address, &cpuCtl);
        if (kr != kIOReturnSuccess) {
          out.status = kr;
          return out;
        }
        const auto resolved = ResolveGa102StartCpuAction(action, cpuCtl);
        if (!resolved.valid) {
          out.status = kIOReturnNotPermitted;
          return out;
        }
        kr = ExecuteConcreteWrite(bar0, bar0Size, resolved.action);
        if (kr != kIOReturnSuccess) {
          out.status = kr;
          return out;
        }
        out.hardwareActionsStarted = true;
        break;
      }

      case ActionKind::IfMaskEqualBegin: {
        std::uint32_t value = 0u;
        const kern_return_t kr = ReadRegister(bar0, bar0Size, action.address, &value);
        if (kr != kIOReturnSuccess) {
          out.status = kr;
          return out;
        }
        skippingConditionalBody = (value & action.mask) != action.value;
        break;
      }

      case ActionKind::EndIf:
        skippingConditionalBody = false;
        break;
    }

    ++out.actionsCompleted;
  }

  out.actionIndex = plan.actions.size();
  out.status = kIOReturnSuccess;
  return out;
}
