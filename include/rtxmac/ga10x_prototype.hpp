#pragma once

#include "rtxmac/boot_package.hpp"
#include "rtxmac/gsp_manifest.hpp"
#include "rtxmac/nvfw.hpp"
#include "rtxmac/vbios.hpp"

#include <cstdint>

namespace rtxmac::nvidia::prototype {

// Deliberately narrow non-FMC GA10x research profile matching the currently
// proven TinyGPU/tinygrad Ampere bring-up layout. These are prototype defaults,
// not universal NVIDIA constants. Live reserved-memory state must be checked
// before any framebuffer/MMIO write is enabled.
inline constexpr std::uint64_t kVgaWorkspaceBytes = 0x100000ull;
inline constexpr std::uint64_t kFrtsBytes = 0x100000ull;
inline constexpr std::uint64_t kNonWprHeapBytes = 0x100000ull;
inline constexpr std::uint64_t kRequestedWprHeapBytes = 0x8100000ull;
inline constexpr std::uint64_t kWprEndMarginBytes = 0ull;

enum class ProfileStatus : std::uint8_t {
  Ok = 0,
  PackageNotVerified,
  UnsupportedTarget,
  InvalidVram,
  MissingSection,
  SectionTooLarge,
  InvalidMetadata,
  ManifestRejected,
};

struct Profile {
  ProfileStatus status{ProfileStatus::PackageNotVerified};
  bool assumesNoLowerVbiosMmuLock{true};
  gsp::ManifestInputs manifestInputs{};
  gsp::BootManifest manifest{};
  fw::RiscvBootloaderInfo gspBootloader{};
  vbios::DescriptorV3 fwsec{};
  fw::BooterImageInfo sec2Booter{};
};

// Builds the cold/offline GA10x prototype inputs entirely from an already
// verified .rtxpkg. No hardware access is performed. The returned WPR layout
// intentionally assumes vbiosReservedOffset == vgaWorkspaceOffset; a future
// live preflight must prove that assumption before writes are permitted.
[[nodiscard]] Profile BuildGa10xPrototypeProfile(
    const package::PackageView& package) noexcept;

[[nodiscard]] const char* ProfileStatusName(ProfileStatus status) noexcept;

} // namespace rtxmac::nvidia::prototype
