#include "rtxmac/nvidia_boot.hpp"

#include <cassert>
#include <iostream>

int main() {
  // Synthetic BOOT_42 value: arch=GA100 family (0x17), implementation=4
  // (GA104), major=1, minor=2, extended-minor=3.
  constexpr std::uint32_t raw = 0x17412300u;
  constexpr auto id = rtxmac::nvidia::DecodeBoot42(raw);

  static_assert(id.raw == raw);
  static_assert(id.architecture == 0x17);
  static_assert(id.implementation == 0x4);
  static_assert(id.majorRevision == 1);
  static_assert(id.minorRevision == 2);
  static_assert(id.minorExtendedRevision == 3);
  static_assert(rtxmac::nvidia::IsAmpere(id));

  assert(rtxmac::nvidia::ArchitectureName(id.architecture) == "Ampere");
  assert(rtxmac::nvidia::ChipName(id) == "GA104");

  constexpr auto ga106 = rtxmac::nvidia::DecodeBoot42(0x17600000u);
  assert(rtxmac::nvidia::ChipName(ga106) == "GA106");

  constexpr auto unknown = rtxmac::nvidia::DecodeBoot42(0x1F400000u);
  assert(!rtxmac::nvidia::IsAmpere(unknown));
  assert(rtxmac::nvidia::ChipName(unknown) == "Unknown");

  std::cout << "rtxmac NVIDIA BOOT_42 tests passed\n";
  return 0;
}
