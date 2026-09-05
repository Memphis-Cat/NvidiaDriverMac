#include "rtxmac/nvidia_probe.hpp"

namespace rtxmac::nvidia {

IdentityProbe ProbeIdentity(ReadOnlyMmio& mmio) {
  IdentityProbe out{};

  const auto boot0 = mmio.Read32(kPmcBoot0Offset);
  if (!boot0) {
    out.status = ProbeStatus::Boot0ReadFailed;
    return out;
  }
  out.boot0 = boot0.value;

  const auto boot42 = mmio.Read32(kPmcBoot42Offset);
  if (!boot42) {
    out.status = ProbeStatus::Boot42ReadFailed;
    return out;
  }
  out.boot42 = DecodeBoot42(boot42.value);

  if (out.boot0 == 0u && boot42.value == 0u) {
    out.status = ProbeStatus::InvalidAllZero;
    return out;
  }
  if (out.boot0 == 0xFFFFFFFFu && boot42.value == 0xFFFFFFFFu) {
    out.status = ProbeStatus::InvalidAllOnes;
    return out;
  }
  if (!IsAmpere(out.boot42)) {
    out.status = ProbeStatus::NotAmpere;
    return out;
  }

  out.status = ProbeStatus::Ok;
  return out;
}

} // namespace rtxmac::nvidia
