#include "rtxmac/pci_identity.hpp"

#include <charconv>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace rtxmac {
namespace {

std::string Upper(std::string_view in) {
  std::string out(in);
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

std::optional<std::uint16_t> Hex16(std::string_view s) {
  if (s.size() != 4) return std::nullopt;
  unsigned value = 0;
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, 16);
  if (ec != std::errc{} || ptr != s.data() + s.size() || value > 0xFFFFu) return std::nullopt;
  return static_cast<std::uint16_t>(value);
}

std::optional<std::string_view> Field(std::string_view s, std::string_view marker, std::size_t width) {
  const auto pos = s.find(marker);
  if (pos == std::string_view::npos) return std::nullopt;
  const auto begin = pos + marker.size();
  if (begin + width > s.size()) return std::nullopt;
  return s.substr(begin, width);
}

} // namespace

std::optional<PciIdentity> ParseWindowsPciHardwareId(std::string_view text) {
  const std::string upper = Upper(text);
  if (!upper.starts_with("PCI\\")) return std::nullopt;

  const auto venText = Field(upper, "VEN_", 4);
  const auto devText = Field(upper, "DEV_", 4);
  if (!venText || !devText) return std::nullopt;

  const auto ven = Hex16(*venText);
  const auto dev = Hex16(*devText);
  if (!ven || !dev) return std::nullopt;

  PciIdentity result{.vendor = *ven, .device = *dev};

  if (const auto subsys = Field(upper, "SUBSYS_", 8)) {
    const auto subDevice = Hex16(subsys->substr(0, 4));
    const auto subVendor = Hex16(subsys->substr(4, 4));
    if (subDevice && subVendor) {
      result.subsystemDevice = *subDevice;
      result.subsystemVendor = *subVendor;
    }
  }

  return result;
}

std::string Describe(const PciIdentity& id) {
  std::ostringstream out;
  out << std::hex << std::uppercase << std::setfill('0');
  out << "PCI " << std::setw(4) << id.vendor << ':' << std::setw(4) << id.device;
  if (id.subsystemVendor || id.subsystemDevice) {
    out << " subsystem " << std::setw(4) << id.subsystemVendor << ':' << std::setw(4) << id.subsystemDevice;
  }
  if (IsKnownRtx3060Ti(id)) out << " (known RTX 3060 Ti / GA104 ID)";
  else if (IsNvidia(id)) out << " (NVIDIA; exact model not classified yet)";
  return out.str();
}

} // namespace rtxmac
