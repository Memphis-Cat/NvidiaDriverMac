#pragma once

#include <cstdint>
#include <limits>
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

// Some hardware interfaces expose a bounded MMIO aperture whose contents are
// data, not one fixed register. Model those explicitly instead of granting a
// broad BAR-wide write exception.
struct MmioWriteRegionRule {
  std::uint32_t offset{};
  std::uint32_t length{};
  std::uint32_t alignment{4u};
};

enum class RegionWriteDecision : std::uint8_t {
  Allowed = 0,
  DeniedEmpty,
  DeniedOverflow,
  DeniedUnaligned,
  DeniedUnknownRange,
};

[[nodiscard]] constexpr RegionWriteDecision CheckMmioRegionWrite(
    std::span<const MmioWriteRegionRule> rules,
    std::uint32_t offset,
    std::uint64_t bytes) noexcept {
  if (bytes == 0u) return RegionWriteDecision::DeniedEmpty;
  if (bytes > std::numeric_limits<std::uint32_t>::max()) {
    return RegionWriteDecision::DeniedOverflow;
  }
  const std::uint64_t end = static_cast<std::uint64_t>(offset) + bytes;
  if (end > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1ull) {
    return RegionWriteDecision::DeniedOverflow;
  }

  for (const auto& rule : rules) {
    if (rule.length == 0u || rule.alignment == 0u) continue;
    const std::uint64_t ruleEnd =
        static_cast<std::uint64_t>(rule.offset) + rule.length;
    if (ruleEnd > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1ull) continue;
    if (offset < rule.offset || end > ruleEnd) continue;
    if ((offset % rule.alignment) != 0u || (bytes % rule.alignment) != 0u) {
      return RegionWriteDecision::DeniedUnaligned;
    }
    return RegionWriteDecision::Allowed;
  }
  return RegionWriteDecision::DeniedUnknownRange;
}

struct PlannedMmioRegionWrite {
  std::uint32_t offset{};
  std::uint64_t bytes{};
  RegionWriteDecision decision{RegionWriteDecision::DeniedUnknownRange};
};

class DryRunRegionWritePlan {
public:
  explicit DryRunRegionWritePlan(std::span<const MmioWriteRegionRule> rules) : rules_(rules) {}

  RegionWriteDecision Add(std::uint32_t offset, std::uint64_t bytes) {
    const auto decision = CheckMmioRegionWrite(rules_, offset, bytes);
    writes_.push_back({offset, bytes, decision});
    if (decision != RegionWriteDecision::Allowed) allAllowed_ = false;
    return decision;
  }

  [[nodiscard]] std::span<const PlannedMmioRegionWrite> Writes() const noexcept { return writes_; }
  [[nodiscard]] bool AllAllowed() const noexcept { return allAllowed_; }

private:
  std::span<const MmioWriteRegionRule> rules_;
  std::vector<PlannedMmioRegionWrite> writes_;
  bool allAllowed_{true};
};

} // namespace rtxmac
