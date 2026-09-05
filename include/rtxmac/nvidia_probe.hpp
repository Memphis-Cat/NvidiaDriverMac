#pragma once

#include "rtxmac/mmio.hpp"
#include "rtxmac/nvidia_boot.hpp"

#include <cstdint>

namespace rtxmac::nvidia {

enum class ProbeStatus : std::uint8_t {
  Ok = 0,
  Boot0ReadFailed,
  Boot42ReadFailed,
  InvalidAllZero,
  InvalidAllOnes,
  NotAmpere,
};

struct IdentityProbe {
  ProbeStatus status{ProbeStatus::Boot0ReadFailed};
  std::uint32_t boot0{};
  Boot42Identity boot42{};
};

[[nodiscard]] IdentityProbe ProbeIdentity(ReadOnlyMmio& mmio);

} // namespace rtxmac::nvidia
