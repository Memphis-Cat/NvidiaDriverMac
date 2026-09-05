#include "rtxmac/mmio_trace.hpp"

namespace rtxmac {

Read32Result RecordingMmio::Read32(std::uint32_t byteOffset) {
  const auto result = backend_.Read32(byteOffset);
  events_.push_back(MmioReadEvent{.offset = byteOffset, .status = result.status, .value = result.value});
  return result;
}

Read32Result ReplayMmio::Read32(std::uint32_t byteOffset) {
  if (mismatch_ || position_ >= events_.size()) {
    mismatch_ = true;
    return {IoStatus::BackendError, 0};
  }

  const auto event = events_[position_];
  if (event.offset != byteOffset) {
    mismatch_ = true;
    return {IoStatus::BackendError, 0};
  }

  ++position_;
  return {event.status, event.value};
}

} // namespace rtxmac
