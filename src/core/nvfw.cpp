#include "rtxmac/nvfw.hpp"

#include <cstddef>
#include <limits>

namespace rtxmac::nvidia::fw {
namespace {

constexpr std::size_t kBinHeaderBytes = 24u;
constexpr std::size_t kHsHeaderV2Bytes = 36u;
constexpr std::size_t kLoadHeaderV2Bytes = 20u;
constexpr std::size_t kAppHeaderV2Bytes = 16u;
constexpr std::size_t kRiscvDescriptorV1Bytes = 84u;
constexpr std::size_t kRiscvDescriptorCurrentBytes = 92u;

std::uint32_t LoadLe32(std::span<const std::uint8_t> data, std::size_t off) noexcept {
  return static_cast<std::uint32_t>(data[off + 0]) |
      (static_cast<std::uint32_t>(data[off + 1]) << 8u) |
      (static_cast<std::uint32_t>(data[off + 2]) << 16u) |
      (static_cast<std::uint32_t>(data[off + 3]) << 24u);
}

bool RangeFits(std::size_t total, std::uint64_t off, std::uint64_t size) noexcept {
  return off <= total && size <= static_cast<std::uint64_t>(total) - off;
}

BinHeader ReadBin(std::span<const std::uint8_t> d) noexcept {
  return {LoadLe32(d, 0), LoadLe32(d, 4), LoadLe32(d, 8), LoadLe32(d, 12), LoadLe32(d, 16), LoadLe32(d, 20)};
}

HsHeaderV2 ReadHs(std::span<const std::uint8_t> d, std::size_t o) noexcept {
  return {LoadLe32(d, o + 0), LoadLe32(d, o + 4), LoadLe32(d, o + 8), LoadLe32(d, o + 12),
          LoadLe32(d, o + 16), LoadLe32(d, o + 20), LoadLe32(d, o + 24), LoadLe32(d, o + 28), LoadLe32(d, o + 32)};
}

LoadHeaderV2 ReadLoad(std::span<const std::uint8_t> d, std::size_t o) noexcept {
  return {LoadLe32(d, o + 0), LoadLe32(d, o + 4), LoadLe32(d, o + 8), LoadLe32(d, o + 12), LoadLe32(d, o + 16)};
}

AppHeaderV2 ReadApp(std::span<const std::uint8_t> d, std::size_t o) noexcept {
  return {LoadLe32(d, o + 0), LoadLe32(d, o + 4), LoadLe32(d, o + 8), LoadLe32(d, o + 12)};
}

bool DescriptorRangeFits(const BinHeader& bin, std::uint32_t off, std::uint32_t size) noexcept {
  return static_cast<std::uint64_t>(off) + size <= bin.dataSize;
}

} // namespace

BooterImageInfo ParseBooterImage(std::span<const std::uint8_t> image) noexcept {
  BooterImageInfo out{};
  if (image.size() < kBinHeaderBytes) return out;

  out.bin = ReadBin(image);
  if (out.bin.binSize < kBinHeaderBytes || out.bin.binSize > image.size()) {
    out.status = ParseStatus::BinSizeOutOfRange;
    return out;
  }

  const std::size_t logicalSize = out.bin.binSize;
  if (!RangeFits(logicalSize, out.bin.headerOffset, kHsHeaderV2Bytes)) {
    out.status = ParseStatus::HeaderOutOfRange;
    return out;
  }
  out.hs = ReadHs(image, out.bin.headerOffset);

  if (!RangeFits(logicalSize, out.hs.headerOffset, kLoadHeaderV2Bytes)) {
    out.status = ParseStatus::LoadHeaderOutOfRange;
    return out;
  }
  out.load = ReadLoad(image, out.hs.headerOffset);
  if (out.load.numApps == 0u) {
    out.status = ParseStatus::NoApplications;
    return out;
  }

  const std::uint64_t appBytes = static_cast<std::uint64_t>(out.load.numApps) * kAppHeaderV2Bytes;
  const std::uint64_t appTableOff = static_cast<std::uint64_t>(out.hs.headerOffset) + kLoadHeaderV2Bytes;
  if (!RangeFits(logicalSize, appTableOff, appBytes)) {
    out.status = ParseStatus::AppTableOutOfRange;
    return out;
  }
  out.firstApp = ReadApp(image, static_cast<std::size_t>(appTableOff));

  if (!RangeFits(logicalSize, out.bin.dataOffset, out.bin.dataSize)) {
    out.status = ParseStatus::DataOutOfRange;
    return out;
  }
  if (!RangeFits(logicalSize, out.hs.sigProdOffset, out.hs.sigProdSize)) {
    out.status = ParseStatus::SignatureOutOfRange;
    return out;
  }
  if (!RangeFits(logicalSize, out.hs.patchLoc, sizeof(std::uint32_t)) ||
      !RangeFits(logicalSize, out.hs.patchSig, sizeof(std::uint32_t))) {
    out.status = ParseStatus::PatchPointerOutOfRange;
    return out;
  }

  if (static_cast<std::uint64_t>(out.firstApp.offset) + out.firstApp.size > out.bin.dataSize ||
      static_cast<std::uint64_t>(out.firstApp.dataOffset) + out.firstApp.dataSize > out.bin.dataSize) {
    out.status = ParseStatus::AppOutOfRange;
    return out;
  }
  if (static_cast<std::uint64_t>(out.load.osDataOffset) + out.load.osDataSize > out.bin.dataSize ||
      static_cast<std::uint64_t>(out.load.osCodeOffset) + out.load.osCodeSize > out.bin.dataSize) {
    out.status = ParseStatus::OsDataOutOfRange;
    return out;
  }

  out.status = ParseStatus::Ok;
  return out;
}

BooterPatchResult PatchBooterProductionSignature(
    std::span<const std::uint8_t> image,
    const BooterImageInfo& info) noexcept {
  BooterPatchResult out{};
  if (info.status != ParseStatus::Ok ||
      info.bin.binSize > image.size() ||
      !RangeFits(info.bin.binSize, info.bin.dataOffset, info.bin.dataSize) ||
      !RangeFits(info.bin.binSize, info.hs.sigProdOffset, info.hs.sigProdSize) ||
      !RangeFits(info.bin.binSize, info.hs.patchLoc, sizeof(std::uint32_t)) ||
      !RangeFits(info.bin.binSize, info.hs.patchSig, sizeof(std::uint32_t))) {
    return out;
  }

  if (!RangeFits(info.bin.binSize, info.hs.numSig, sizeof(std::uint32_t))) {
    out.status = BooterPatchStatus::NumSignaturesPointerOutOfRange;
    return out;
  }
  const std::uint32_t signatureCount = LoadLe32(image, info.hs.numSig);
  out.signatureCount = signatureCount;
  if (signatureCount == 0u) {
    out.status = BooterPatchStatus::ZeroSignatures;
    return out;
  }
  if (info.hs.sigProdSize == 0u || (info.hs.sigProdSize % signatureCount) != 0u) {
    out.status = BooterPatchStatus::InvalidSignatureTable;
    return out;
  }

  const std::uint32_t signatureBytes = info.hs.sigProdSize / signatureCount;
  const std::uint32_t patchOffset = LoadLe32(image, info.hs.patchLoc);
  const std::uint32_t selectedSignatureOffset = LoadLe32(image, info.hs.patchSig);
  out.signatureBytes = signatureBytes;
  out.patchOffset = patchOffset;
  out.selectedSignatureOffset = selectedSignatureOffset;

  if (selectedSignatureOffset > info.hs.sigProdSize ||
      signatureBytes > info.hs.sigProdSize - selectedSignatureOffset) {
    out.status = BooterPatchStatus::SignatureSelectionOutOfRange;
    return out;
  }
  if (patchOffset > info.bin.dataSize || signatureBytes > info.bin.dataSize - patchOffset) {
    out.status = BooterPatchStatus::PatchTargetOutOfRange;
    return out;
  }

  const std::uint64_t signatureSource =
      static_cast<std::uint64_t>(info.hs.sigProdOffset) + selectedSignatureOffset;
  if (!RangeFits(info.bin.binSize, signatureSource, signatureBytes)) {
    out.status = BooterPatchStatus::SignatureSelectionOutOfRange;
    return out;
  }

  out.bytes.assign(image.begin() + info.bin.dataOffset,
                   image.begin() + info.bin.dataOffset + info.bin.dataSize);
  for (std::uint32_t i = 0u; i < signatureBytes; ++i) {
    out.bytes[patchOffset + i] = image[static_cast<std::size_t>(signatureSource) + i];
  }
  out.status = BooterPatchStatus::Ok;
  return out;
}

const char* BooterPatchStatusName(BooterPatchStatus status) noexcept {
  switch (status) {
    case BooterPatchStatus::Ok: return "ok";
    case BooterPatchStatus::InvalidBooter: return "invalid-booter";
    case BooterPatchStatus::NumSignaturesPointerOutOfRange: return "num-signatures-pointer-out-of-range";
    case BooterPatchStatus::ZeroSignatures: return "zero-signatures";
    case BooterPatchStatus::InvalidSignatureTable: return "invalid-signature-table";
    case BooterPatchStatus::SignatureSelectionOutOfRange: return "signature-selection-out-of-range";
    case BooterPatchStatus::PatchTargetOutOfRange: return "patch-target-out-of-range";
  }
  return "unknown";
}

RiscvBootloaderInfo ParseRiscvBootloader(std::span<const std::uint8_t> image) noexcept {
  RiscvBootloaderInfo out{};
  if (image.size() < kBinHeaderBytes) return out;
  out.bin = ReadBin(image);
  if (out.bin.binSize < kBinHeaderBytes || out.bin.binSize > image.size()) {
    out.status = ParseStatus::BinSizeOutOfRange;
    return out;
  }
  if (!RangeFits(out.bin.binSize, out.bin.dataOffset, out.bin.dataSize)) {
    out.status = ParseStatus::DataOutOfRange;
    return out;
  }
  if (out.bin.headerOffset > out.bin.dataOffset ||
      !RangeFits(out.bin.binSize, out.bin.headerOffset, kRiscvDescriptorV1Bytes)) {
    out.status = ParseStatus::RiscvDescriptorOutOfRange;
    return out;
  }

  const std::uint64_t descriptorRegionBytes = static_cast<std::uint64_t>(out.bin.dataOffset) - out.bin.headerOffset;
  const bool extended = descriptorRegionBytes >= kRiscvDescriptorCurrentBytes &&
      RangeFits(out.bin.binSize, out.bin.headerOffset, kRiscvDescriptorCurrentBytes);
  const std::size_t o = out.bin.headerOffset;
  auto& d = out.descriptor;
  d.version = LoadLe32(image, o + 0u);
  d.bootloaderOffset = LoadLe32(image, o + 4u);
  d.bootloaderSize = LoadLe32(image, o + 8u);
  d.bootloaderParamOffset = LoadLe32(image, o + 12u);
  d.bootloaderParamSize = LoadLe32(image, o + 16u);
  d.riscvElfOffset = LoadLe32(image, o + 20u);
  d.riscvElfSize = LoadLe32(image, o + 24u);
  d.appVersion = LoadLe32(image, o + 28u);
  d.manifestOffset = LoadLe32(image, o + 32u);
  d.manifestSize = LoadLe32(image, o + 36u);
  d.monitorDataOffset = LoadLe32(image, o + 40u);
  d.monitorDataSize = LoadLe32(image, o + 44u);
  d.monitorCodeOffset = LoadLe32(image, o + 48u);
  d.monitorCodeSize = LoadLe32(image, o + 52u);
  d.isMonitorEnabled = LoadLe32(image, o + 56u);
  d.swbromCodeOffset = LoadLe32(image, o + 60u);
  d.swbromCodeSize = LoadLe32(image, o + 64u);
  d.swbromDataOffset = LoadLe32(image, o + 68u);
  d.swbromDataSize = LoadLe32(image, o + 72u);
  d.fbReservedSize = LoadLe32(image, o + 76u);
  d.signedAsCode = LoadLe32(image, o + 80u);
  if (extended) {
    d.isSmp = LoadLe32(image, o + 84u);
    d.isPlicEnabled = LoadLe32(image, o + 88u);
    d.hasExtendedFields = true;
  }

  const bool payloadOk =
      DescriptorRangeFits(out.bin, d.bootloaderOffset, d.bootloaderSize) &&
      DescriptorRangeFits(out.bin, d.bootloaderParamOffset, d.bootloaderParamSize) &&
      DescriptorRangeFits(out.bin, d.riscvElfOffset, d.riscvElfSize) &&
      DescriptorRangeFits(out.bin, d.manifestOffset, d.manifestSize) &&
      DescriptorRangeFits(out.bin, d.monitorDataOffset, d.monitorDataSize) &&
      DescriptorRangeFits(out.bin, d.monitorCodeOffset, d.monitorCodeSize) &&
      DescriptorRangeFits(out.bin, d.swbromCodeOffset, d.swbromCodeSize) &&
      DescriptorRangeFits(out.bin, d.swbromDataOffset, d.swbromDataSize);
  if (!payloadOk) {
    out.status = ParseStatus::RiscvPayloadOutOfRange;
    return out;
  }

  out.status = ParseStatus::Ok;
  return out;
}

const char* ParseStatusName(ParseStatus status) noexcept {
  switch (status) {
    case ParseStatus::Ok: return "ok";
    case ParseStatus::TooSmall: return "too-small";
    case ParseStatus::BinSizeOutOfRange: return "bin-size-out-of-range";
    case ParseStatus::HeaderOutOfRange: return "hs-header-out-of-range";
    case ParseStatus::LoadHeaderOutOfRange: return "load-header-out-of-range";
    case ParseStatus::AppTableOutOfRange: return "app-table-out-of-range";
    case ParseStatus::NoApplications: return "no-applications";
    case ParseStatus::DataOutOfRange: return "data-out-of-range";
    case ParseStatus::SignatureOutOfRange: return "signature-out-of-range";
    case ParseStatus::PatchPointerOutOfRange: return "patch-pointer-out-of-range";
    case ParseStatus::AppOutOfRange: return "app-out-of-range";
    case ParseStatus::OsDataOutOfRange: return "os-data-out-of-range";
    case ParseStatus::RiscvDescriptorOutOfRange: return "riscv-descriptor-out-of-range";
    case ParseStatus::RiscvPayloadOutOfRange: return "riscv-payload-out-of-range";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::fw
