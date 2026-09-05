#pragma once

#include <cstdint>
#include <span>

namespace rtxmac::nvidia::vbios {

enum class ParseStatus : std::uint8_t {
  Ok = 0,
  TooSmall,
  InvalidRomSignature,
  PciDataOutOfRange,
  InvalidPciDataSignature,
  InvalidImageLength,
  ImageOutOfRange,
  BitHeaderNotFound,
  InvalidBitHeader,
  TokenTableOutOfRange,
  FalconDataOutOfRange,
  InvalidUcodeTable,
  UcodeEntryOutOfRange,
  FwsecProductionNotFound,
  DescriptorOutOfRange,
  UnsupportedDescriptor,
  StoredImageOutOfRange,
};

struct DescriptorV3 {
  std::uint32_t storedSize{};
  std::uint32_t pkcDataOffset{};
  std::uint32_t interfaceOffset{};
  std::uint32_t imemPhysBase{};
  std::uint32_t imemLoadSize{};
  std::uint32_t imemVirtBase{};
  std::uint32_t dmemPhysBase{};
  std::uint32_t dmemLoadSize{};
  std::uint16_t engineIdMask{};
  std::uint8_t ucodeId{};
  std::uint8_t signatureCount{};
  std::uint16_t signatureVersions{};
};

struct FwsecInfo {
  ParseStatus status{ParseStatus::TooSmall};
  std::uint32_t biosSize{};
  std::uint32_t expansionRomOffset{};
  std::uint32_t bitOffset{};
  std::uint32_t falconTableOffset{};
  std::uint32_t descriptorOffset{};
  std::uint32_t descriptorSize{};
  std::uint8_t descriptorVersion{};
  DescriptorV3 v3{};
};

// Parse an already-extracted PCI VBIOS image beginning at its ROM signature.
// The function only locates/validates metadata; it never modifies the image.
[[nodiscard]] FwsecInfo ParseProductionFwsec(std::span<const std::uint8_t> image) noexcept;
[[nodiscard]] const char* ParseStatusName(ParseStatus status) noexcept;

} // namespace rtxmac::nvidia::vbios
