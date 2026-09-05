#include "rtxmac/nvidia_boot.hpp"

namespace rtxmac::nvidia {

std::string_view ArchitectureName(std::uint8_t architecture) noexcept {
  switch (architecture) {
    case 0x16: return "Turing";
    case 0x17: return "Ampere";
    case 0x18: return "Hopper";
    case 0x19: return "Ada";
    case 0x1A: return "Blackwell";
    default: return "Unknown";
  }
}

std::string_view ChipName(const Boot42Identity& id) noexcept {
  if (id.architecture != 0x17) return "Unknown";

  // NVIDIA open-gpu-kernel-modules g_hal_archimpl.h maps GA100-family
  // implementation values to these chip names.
  switch (id.implementation) {
    case 0x0: return "GA100";
    case 0x2: return "GA102";
    case 0x3: return "GA103";
    case 0x4: return "GA104";
    case 0x6: return "GA106";
    case 0x7: return "GA107";
    default: return "Unknown Ampere";
  }
}

} // namespace rtxmac::nvidia
