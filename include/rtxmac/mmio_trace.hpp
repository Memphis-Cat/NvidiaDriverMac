#pragma once

#include "rtxmac/mmio.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rtxmac {

struct MmioReadEvent {
  std::uint32_t offset{};
  IoStatus status{IoStatus::BackendError};
  std::uint32_t value{};

  constexpr bool operator==(const MmioReadEvent&) const noexcept = default;
};

class RecordingMmio final : public ReadOnlyMmio {
public:
  explicit RecordingMmio(ReadOnlyMmio& backend) : backend_(backend) {}

  [[nodiscard]] Read32Result Read32(std::uint32_t byteOffset) override;
  [[nodiscard]] std::span<const MmioReadEvent> Events() const noexcept { return events_; }
  void Clear() { events_.clear(); }

private:
  ReadOnlyMmio& backend_;
  std::vector<MmioReadEvent> events_;
};

class ReplayMmio final : public ReadOnlyMmio {
public:
  explicit ReplayMmio(std::span<const MmioReadEvent> events) : events_(events) {}

  [[nodiscard]] Read32Result Read32(std::uint32_t byteOffset) override;
  [[nodiscard]] bool Complete() const noexcept { return position_ == events_.size() && !mismatch_; }
  [[nodiscard]] bool Mismatch() const noexcept { return mismatch_; }
  [[nodiscard]] std::size_t Position() const noexcept { return position_; }

private:
  std::span<const MmioReadEvent> events_;
  std::size_t position_{};
  bool mismatch_{};
};

} // namespace rtxmac
