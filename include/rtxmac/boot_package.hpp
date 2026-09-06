#pragma once

#include "rtxmac/pci_identity.hpp"
#include "rtxmac/sha256.hpp"
#include "rtxmac/vbios.hpp"
#include "rtxmac/nvfw.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rtxmac::nvidia::package {

inline constexpr std::array<std::uint8_t,8> kMagic{'R','T','X','M','A','C','P','1'};
inline constexpr std::uint32_t kVersion=1u;
inline constexpr std::uint32_t kHeaderBytes=160u;
inline constexpr std::uint32_t kSectionRecordBytes=56u;
inline constexpr std::uint32_t kSectionCount=5u;
inline constexpr std::uint64_t kPayloadAlignment=0x1000ull;
inline constexpr std::uint64_t kSectionTableOffset=kHeaderBytes;

// Exact prepared payloads consumed by later DriverKit allocation/staging.
enum class SectionKind : std::uint32_t {
  GspFirmwareImage=1u,
  GspFirmwareSignature=2u,
  GspBootloader=3u,
  FrtsFwsecImage=4u,
  Sec2BooterImage=5u,
};

struct SectionRecord {
  SectionKind kind{};
  std::uint64_t offset{};
  std::uint64_t size{};
  rtxmac::Sha256Digest sha256{};
};

struct StaticMetadata {
  rtxmac::PciIdentity pci{};
  std::uint64_t vramBytes{};

  std::uint32_t gspAppVersion{};
  std::uint32_t gspMonitorCodeOffset{};
  std::uint32_t gspMonitorDataOffset{};
  std::uint32_t gspManifestOffset{};

  std::uint32_t fwsecPkcDataOffset{};
  std::uint32_t fwsecImemPhysBase{};
  std::uint32_t fwsecImemLoadSize{};
  std::uint32_t fwsecImemVirtBase{};
  std::uint32_t fwsecDmemPhysBase{};
  std::uint32_t fwsecDmemLoadSize{};
  std::uint16_t fwsecEngineIdMask{};
  std::uint8_t fwsecUcodeId{};

  std::uint32_t sec2CodeOffset{};
  std::uint32_t sec2CodeSize{};
  std::uint32_t sec2DataOffset{};
  std::uint32_t sec2DataSize{};
};

struct SourceInputs {
  rtxmac::PciIdentity pci{};
  std::uint64_t vramBytes{};
  std::span<const std::uint8_t> vbios;
  std::span<const std::uint8_t> gspElf;
  std::span<const std::uint8_t> gspBootloaderContainer;
  std::span<const std::uint8_t> sec2BooterContainer;
};

enum class BuildStatus : std::uint8_t {
  Ok=0,
  InvalidTarget,
  InvalidVram,
  VbiosParseFailed,
  VbiosSignatureMissing,
  FrtsPatchFailed,
  GspFirmwareSectionMissing,
  GspSignatureSectionMissing,
  GspBootloaderParseFailed,
  Sec2BooterParseFailed,
  Sec2BooterPatchFailed,
  SizeOverflow,
};

struct BuildResult {
  BuildStatus status{BuildStatus::InvalidTarget};
  StaticMetadata metadata{};
  std::array<SectionRecord,kSectionCount> sections{};
  std::vector<std::uint8_t> bytes;
};

// Build a self-contained GA10x prototype package from the four Windows-side
// source files. The GSP ELF contributes .fwimage + .fwsignature_ga10x; the
// VBIOS contributes production FWSEC + its descriptor signature; SEC2 booter is
// production-signature patched before serialization.
[[nodiscard]] BuildResult BuildGa10xPackage(const SourceInputs& in);
[[nodiscard]] const char* BuildStatusName(BuildStatus status) noexcept;

enum class ParseStatus : std::uint8_t {
  Ok=0,
  TooSmall,
  BadMagic,
  UnsupportedVersion,
  BadHeader,
  BadPackageSize,
  BadTarget,
  BadSectionTable,
  DuplicateOrUnknownSection,
  BadSectionRange,
  BadSectionAlignment,
  OverlappingSections,
  HashMismatch,
};

struct PackageView {
  ParseStatus status{ParseStatus::TooSmall};
  StaticMetadata metadata{};
  std::array<SectionRecord,kSectionCount> sections{};
  std::uint64_t packageBytes{};
};

[[nodiscard]] PackageView ParseAndVerify(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] std::span<const std::uint8_t> FindSection(
    std::span<const std::uint8_t> bytes,
    const PackageView& view,
    SectionKind kind) noexcept;
[[nodiscard]] const char* ParseStatusName(ParseStatus status) noexcept;

} // namespace rtxmac::nvidia::package
