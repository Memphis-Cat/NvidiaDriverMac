#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace rtxmac {

struct MmioWriteRule {
  std::uint32_t offset{};
  std::uint32_t writableMask{};
};

enum class WriteDecision : std::uint8_t {
  Allowed = 0,
  DeniedUnknownRegister,
  DeniedBitOutsideMask,
};

[[nodiscard]] constexpr WriteDecision CheckMmioWrite(
    std::span<const MmioWriteRule> rules,
    std::uint32_t offset,
    std::uint32_t oldValue,
    std::uint32_t newValue) noexcept {
  for (const auto& rule : rules) {
    if (rule.offset != offset) continue;
    const std::uint32_t changed = oldValue ^ newValue;
    return (changed & ~rule.writableMask) == 0
        ? WriteDecision::Allowed
        : WriteDecision::DeniedBitOutsideMask;
  }
  return WriteDecision::DeniedUnknownRegister;
}

struct PlannedMmioWrite {
  std::uint32_t offset{};
  std::uint32_t oldValue{};
  std::uint32_t newValue{};
  WriteDecision decision{WriteDecision::DeniedUnknownRegister};
};

class DryRunWritePlan {
public:
  explicit DryRunWritePlan(std::span<const MmioWriteRule> rules) : rules_(rules) {}

  WriteDecision Add(std::uint32_t offset, std::uint32_t oldValue, std::uint32_t newValue) {
    const auto decision = CheckMmioWrite(rules_, offset, oldValue, newValue);
    writes_.push_back({offset, oldValue, newValue, decision});
    if (decision != WriteDecision::Allowed) allAllowed_ = false;
    return decision;
  }

  [[nodiscard]] std::span<const PlannedMmioWrite> Writes() const noexcept { return writes_; }
  [[nodiscard]] bool AllAllowed() const noexcept { return allAllowed_; }

private:
  std::span<const MmioWriteRule> rules_;
  std::vector<PlannedMmioWrite> writes_;
  bool allAllowed_{true};
};

} // namespace rtxmac
