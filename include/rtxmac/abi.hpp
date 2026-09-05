#pragma once

#include <cstdint>

namespace rtxmac::abi {

inline constexpr std::uint32_t kMagic = 0x5254584Du; // 'RTXM'
inline constexpr std::uint16_t kMajor = 0;
inline constexpr std::uint16_t kMinor = 1;

// The first hardware prototype exposes observations only. No write selectors
// exist in ABI v0.1 by design.
enum class Selector : std::uint32_t {
  GetVersion = 0,
  GetIdentity = 1,
  GetBarInfo = 2,
  ConfigRead = 3,
  MmioRead32 = 4,
};

struct Version {
  std::uint32_t magic{kMagic};
  std::uint16_t major{kMajor};
  std::uint16_t minor{kMinor};
};

struct Identity {
  std::uint16_t vendor{};
  std::uint16_t device{};
  std::uint16_t subsystemVendor{};
  std::uint16_t subsystemDevice{};
  std::uint32_t classCode{};
  std::uint8_t revision{};
  std::uint8_t reserved[3]{};
};

struct BarInfo {
  std::uint32_t index{};
  std::uint32_t memoryType{};
  std::uint64_t size{};
};

static_assert(sizeof(Version) == 8);
static_assert(sizeof(Identity) == 16);
static_assert(sizeof(BarInfo) == 16);

} // namespace rtxmac::abi
