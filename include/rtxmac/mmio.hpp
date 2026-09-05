#pragma once

#include <cstdint>

namespace rtxmac {

enum class IoStatus : std::uint8_t {
  Ok = 0,
  Unmapped,
  Misaligned,
  BackendError,
};

struct Read32Result {
  IoStatus status{IoStatus::BackendError};
  std::uint32_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return status == IoStatus::Ok;
  }
};

class ReadOnlyMmio {
public:
  virtual ~ReadOnlyMmio() = default;
  [[nodiscard]] virtual Read32Result Read32(std::uint32_t byteOffset) = 0;
};

} // namespace rtxmac
