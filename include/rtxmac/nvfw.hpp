#pragma once

#include <cstdint>
#include <span>
#include <vector>

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
  RiscvDescriptorOutOfRange,
  RiscvPayloadOutOfRange,
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
  // Despite the legacy field name, this is an offset to a u32 signature count
  // in NVIDIA's nvfw HS header, not the count value itself.
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

enum class BooterPatchStatus : std::uint8_t {
  Ok = 0,
  InvalidBooter,
  NumSignaturesPointerOutOfRange,
  ZeroSignatures,
  InvalidSignatureTable,
  SignatureSelectionOutOfRange,
  PatchTargetOutOfRange,
};

struct BooterPatchResult {
  BooterPatchStatus status{BooterPatchStatus::InvalidBooter};
  std::uint32_t signatureCount{};
  std::uint32_t signatureBytes{};
  std::uint32_t selectedSignatureOffset{};
  std::uint32_t patchOffset{};
  std::vector<std::uint8_t> bytes;
};

// Build the exact authenticated payload staged to VRAM for NVIDIA HS booters.
// The HS header contains pointers to patch_loc, patch_sig and num_sig values.
// The selected production signature is copied into a private copy of the
// [bin.dataOffset, dataOffset+dataSize) payload; the input is never modified.
[[nodiscard]] BooterPatchResult PatchBooterProductionSignature(
    std::span<const std::uint8_t> image,
    const BooterImageInfo& info) noexcept;
[[nodiscard]] const char* BooterPatchStatusName(BooterPatchStatus status) noexcept;

struct RiscvUcodeDescriptor {
  std::uint32_t version{};
  std::uint32_t bootloaderOffset{};
  std::uint32_t bootloaderSize{};
  std::uint32_t bootloaderParamOffset{};
  std::uint32_t bootloaderParamSize{};
  std::uint32_t riscvElfOffset{};
  std::uint32_t riscvElfSize{};
  std::uint32_t appVersion{};
  std::uint32_t manifestOffset{};
  std::uint32_t manifestSize{};
  std::uint32_t monitorDataOffset{};
  std::uint32_t monitorDataSize{};
  std::uint32_t monitorCodeOffset{};
  std::uint32_t monitorCodeSize{};
  std::uint32_t isMonitorEnabled{};
  std::uint32_t swbromCodeOffset{};
  std::uint32_t swbromCodeSize{};
  std::uint32_t swbromDataOffset{};
  std::uint32_t swbromDataSize{};
  std::uint32_t fbReservedSize{};
  std::uint32_t signedAsCode{};
  std::uint32_t isSmp{};
  std::uint32_t isPlicEnabled{};
  bool hasExtendedFields{};
};

struct RiscvBootloaderInfo {
  ParseStatus status{ParseStatus::TooSmall};
  BinHeader bin{};
  RiscvUcodeDescriptor descriptor{};
};

[[nodiscard]] BooterImageInfo ParseBooterImage(std::span<const std::uint8_t> image) noexcept;
[[nodiscard]] RiscvBootloaderInfo ParseRiscvBootloader(std::span<const std::uint8_t> image) noexcept;
[[nodiscard]] const char* ParseStatusName(ParseStatus status) noexcept;

} // namespace rtxmac::nvidia::fw
