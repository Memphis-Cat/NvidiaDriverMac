#pragma once

#include <cstdint>
#include <span>

namespace rtxmac::nvidia::fw {

enum class ParseStatus : std::uint8_t {
  Ok = 0,
  TooSmall,
  BinSizeOutOfRange,
  HeaderOutOfRange,
  LoadHeaderOutOfRange,
  AppTableOutOfRange,
  NoApplications,
  DataOutOfRange,
  SignatureOutOfRange,
  PatchPointerOutOfRange,
  AppOutOfRange,
  OsDataOutOfRange,
};

struct BinHeader {
  std::uint32_t magic{};
  std::uint32_t version{};
  std::uint32_t binSize{};
  std::uint32_t headerOffset{};
  std::uint32_t dataOffset{};
  std::uint32_t dataSize{};
};

struct HsHeaderV2 {
  std::uint32_t sigProdOffset{};
  std::uint32_t sigProdSize{};
  std::uint32_t patchLoc{};
  std::uint32_t patchSig{};
  std::uint32_t metaDataOffset{};
  std::uint32_t metaDataSize{};
  std::uint32_t numSig{};
  std::uint32_t headerOffset{};
  std::uint32_t headerSize{};
};

struct LoadHeaderV2 {
  std::uint32_t osCodeOffset{};
  std::uint32_t osCodeSize{};
  std::uint32_t osDataOffset{};
  std::uint32_t osDataSize{};
  std::uint32_t numApps{};
};

struct AppHeaderV2 {
  std::uint32_t offset{};
  std::uint32_t size{};
  std::uint32_t dataOffset{};
  std::uint32_t dataSize{};
};

struct BooterImageInfo {
  ParseStatus status{ParseStatus::TooSmall};
  BinHeader bin{};
  HsHeaderV2 hs{};
  LoadHeaderV2 load{};
  AppHeaderV2 firstApp{};
};

[[nodiscard]] BooterImageInfo ParseBooterImage(std::span<const std::uint8_t> image) noexcept;
[[nodiscard]] const char* ParseStatusName(ParseStatus status) noexcept;

} // namespace rtxmac::nvidia::fw
