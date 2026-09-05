#include "rtxmac/nvfw.hpp"

#include <cstddef>
#include <limits>

namespace rtxmac::nvidia::fw {
namespace {

constexpr std::size_t kBinHeaderBytes = 24u;
constexpr std::size_t kHsHeaderV2Bytes = 36u;
constexpr std::size_t kLoadHeaderV2Bytes = 20u;
constexpr std::size_t kAppHeaderV2Bytes = 16u;

std::uint32_t LoadLe32(std::span<const std::uint8_t> data, std::size_t off) noexcept {
  return static_cast<std::uint32_t>(data[off + 0]) |
      (static_cast<std::uint32_t>(data[off + 1]) << 8u) |
      (static_cast<std::uint32_t>(data[off + 2]) << 16u) |
      (static_cast<std::uint32_t>(data[off + 3]) << 24u);
}

bool RangeFits(std::size_t total, std::uint64_t off, std::uint64_t size) noexcept {
  return off <= total && size <= total - static_cast<std::size_t>(off);
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

  // App/OS offsets are relative to the firmware data image copied from
  // [dataOffset, dataOffset+dataSize), matching NVIDIA/tinygrad usage.
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
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::fw
