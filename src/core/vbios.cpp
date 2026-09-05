#include "rtxmac/vbios.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace rtxmac::nvidia::vbios {
namespace {

constexpr std::uint16_t kRomSig = 0xAA55u;
constexpr std::uint16_t kRomSigNv = 0x4E56u;
constexpr std::uint16_t kRomSigNv2 = 0xBB77u;
constexpr std::uint32_t kPcirSig = 0x52494350u;
constexpr std::uint32_t kPcirSigNv = 0x5344504Eu;
constexpr std::uint32_t kPcirSigNv2 = 0x53494752u;
constexpr std::uint32_t kNvPciExtSig = 0x4544504Eu;
constexpr std::uint16_t kNvPciExtRev10 = 0x0100u;
constexpr std::uint16_t kNvPciExtRev11 = 0x0101u;
constexpr std::uint32_t kImageBlockBytes = 512u;
constexpr std::uint8_t kCodeTypeBase = 0x00u;
constexpr std::uint8_t kCodeTypeExt = 0xE0u;
constexpr std::uint8_t kLastImage = 0x80u;

constexpr std::uint16_t kBitId = 0xB8FFu;
constexpr std::uint32_t kBitSignature = 0x00544942u;
constexpr std::uint8_t kFalconToken = 0x70u;
constexpr std::uint8_t kFalconDataVersion = 2u;
constexpr std::uint8_t kFwsecProdAppId = 0x85u;
constexpr std::uint8_t kUcodeTableVersion1 = 1u;
constexpr std::uint32_t kDescriptorV3PackedBytes = 44u;

bool Fits(std::size_t total, std::uint64_t off, std::uint64_t size) noexcept {
  return off <= total && size <= total - static_cast<std::size_t>(off);
}

std::uint8_t U8(std::span<const std::uint8_t> b, std::size_t o) noexcept { return b[o]; }
std::uint16_t U16(std::span<const std::uint8_t> b, std::size_t o) noexcept {
  return static_cast<std::uint16_t>(b[o]) | static_cast<std::uint16_t>(b[o + 1]) << 8u;
}
std::uint32_t U32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
  return static_cast<std::uint32_t>(b[o]) |
      (static_cast<std::uint32_t>(b[o + 1]) << 8u) |
      (static_cast<std::uint32_t>(b[o + 2]) << 16u) |
      (static_cast<std::uint32_t>(b[o + 3]) << 24u);
}

bool ValidRomSig(std::uint16_t v) noexcept { return v == kRomSig || v == kRomSigNv || v == kRomSigNv2; }
bool ValidPcirSig(std::uint32_t v) noexcept { return v == kPcirSig || v == kPcirSigNv || v == kPcirSigNv2; }

struct RomLocation {
  ParseStatus status{ParseStatus::Ok};
  std::uint32_t biosSize{};
  std::uint32_t expansionRomOffset{};
};

RomLocation LocateRoms(std::span<const std::uint8_t> image) noexcept {
  RomLocation out{};
  if (image.size() < 0x1Au || !ValidRomSig(U16(image, 0u))) {
    out.status = ParseStatus::InvalidRomSignature;
    return out;
  }

  std::uint64_t current = 0u;
  std::uint64_t extRomOffset = 0u;
  std::uint64_t baseRomSize = 0u;
  std::uint64_t lastEnd = 0u;

  for (std::uint32_t imageIndex = 0u; imageIndex < 64u; ++imageIndex) {
    if (!Fits(image.size(), current, 0x1Au)) { out.status = ParseStatus::PciDataOutOfRange; return out; }
    if (!ValidRomSig(U16(image, static_cast<std::size_t>(current)))) { out.status = ParseStatus::InvalidRomSignature; return out; }

    const std::uint16_t pcirRel = U16(image, static_cast<std::size_t>(current + 0x18u));
    const std::uint64_t pcir = current + pcirRel;
    if (!Fits(image.size(), pcir, 0x18u)) { out.status = ParseStatus::PciDataOutOfRange; return out; }
    if (!ValidPcirSig(U32(image, static_cast<std::size_t>(pcir)))) { out.status = ParseStatus::InvalidPciDataSignature; return out; }

    std::uint64_t subImageBlocks = U16(image, static_cast<std::size_t>(pcir + 0x10u));
    if (subImageBlocks == 0u) { out.status = ParseStatus::InvalidImageLength; return out; }
    bool last = (U8(image, static_cast<std::size_t>(pcir + 0x15u)) & kLastImage) != 0u;

    const std::uint16_t pcirLen = U16(image, static_cast<std::size_t>(pcir + 0x0Au));
    const std::uint64_t extAt = (pcir + pcirLen + 0xFu) & ~0xFull;
    if (Fits(image.size(), extAt, 12u) && U32(image, static_cast<std::size_t>(extAt)) == kNvPciExtSig) {
      const std::uint16_t rev = U16(image, static_cast<std::size_t>(extAt + 4u));
      if (rev == kNvPciExtRev10 || rev == kNvPciExtRev11) {
        const std::uint16_t extLen = U16(image, static_cast<std::size_t>(extAt + 6u));
        const std::uint16_t extBlocks = U16(image, static_cast<std::size_t>(extAt + 8u));
        if (extBlocks != 0u) subImageBlocks = extBlocks;
        if (extLen > 10u) last = (U8(image, static_cast<std::size_t>(extAt + 10u)) & kLastImage) != 0u;
      }
    }

    if (subImageBlocks > std::numeric_limits<std::uint64_t>::max() / kImageBlockBytes) {
      out.status = ParseStatus::InvalidImageLength; return out;
    }
    const std::uint64_t blockBytes = subImageBlocks * kImageBlockBytes;
    if (!Fits(image.size(), current, blockBytes)) { out.status = ParseStatus::ImageOutOfRange; return out; }

    const std::uint8_t codeType = U8(image, static_cast<std::size_t>(pcir + 0x14u));
    if (extRomOffset == 0u && codeType == kCodeTypeExt) extRomOffset = current;
    else if (baseRomSize == 0u && codeType == kCodeTypeBase) baseRomSize = blockBytes;

    lastEnd = current + blockBytes;
    if (last) break;
    current += blockBytes;
    if (current >= image.size()) { out.status = ParseStatus::ImageOutOfRange; return out; }
    if (imageIndex == 63u) { out.status = ParseStatus::InvalidImageLength; return out; }
  }

  if (lastEnd > std::numeric_limits<std::uint32_t>::max()) { out.status = ParseStatus::ImageOutOfRange; return out; }
  out.biosSize = static_cast<std::uint32_t>(lastEnd);
  if (extRomOffset > 0u && baseRomSize > 0u && extRomOffset >= baseRomSize) {
    out.expansionRomOffset = static_cast<std::uint32_t>(extRomOffset - baseRomSize);
  }
  return out;
}

std::uint32_t FindBit(std::span<const std::uint8_t> image, std::uint32_t biosSize) noexcept {
  if (biosSize < 12u) return std::numeric_limits<std::uint32_t>::max();
  for (std::uint32_t off = 0u; off + 12u <= biosSize; ++off) {
    if (U16(image, off) != kBitId || U32(image, off + 2u) != kBitSignature) continue;
    const std::uint8_t headerSize = U8(image, off + 8u);
    if (headerSize < 12u || !Fits(biosSize, off, headerSize)) continue;
    std::uint32_t sum = 0u;
    for (std::uint32_t i = 0u; i < headerSize; ++i) sum += U8(image, off + i);
    if ((sum & 0xFFu) == 0u) return off;
  }
  return std::numeric_limits<std::uint32_t>::max();
}

} // namespace

FwsecInfo ParseProductionFwsec(std::span<const std::uint8_t> image) noexcept {
  FwsecInfo out{};
  if (image.size() < 0x40u) return out;

  const auto rom = LocateRoms(image);
  if (rom.status != ParseStatus::Ok) { out.status = rom.status; return out; }
  out.biosSize = rom.biosSize;
  out.expansionRomOffset = rom.expansionRomOffset;

  const std::uint32_t bit = FindBit(image, out.biosSize);
  if (bit == std::numeric_limits<std::uint32_t>::max()) { out.status = ParseStatus::BitHeaderNotFound; return out; }
  out.bitOffset = bit;

  const std::uint8_t headerSize = U8(image, bit + 8u);
  const std::uint8_t tokenSize = U8(image, bit + 9u);
  const std::uint8_t tokenCount = U8(image, bit + 10u);
  if (tokenSize < 6u) { out.status = ParseStatus::InvalidBitHeader; return out; }
  if (!Fits(out.biosSize, static_cast<std::uint64_t>(bit) + headerSize,
            static_cast<std::uint64_t>(tokenSize) * tokenCount)) {
    out.status = ParseStatus::TokenTableOutOfRange; return out;
  }

  for (std::uint32_t t = 0u; t < tokenCount; ++t) {
    const std::uint32_t tok = bit + headerSize + t * tokenSize;
    const std::uint8_t tokenId = U8(image, tok + 0u);
    const std::uint8_t dataVersion = U8(image, tok + 1u);
    const std::uint16_t dataSize = U16(image, tok + 2u);
    const std::uint32_t dataPtr = tokenSize >= 8u ? U32(image, tok + 4u) : U16(image, tok + 4u);
    if (tokenId != kFalconToken || dataVersion != kFalconDataVersion || dataSize < 4u) continue;
    if (!Fits(out.biosSize, dataPtr, 4u)) { out.status = ParseStatus::FalconDataOutOfRange; return out; }

    const std::uint32_t tableRel = U32(image, dataPtr);
    const std::uint64_t table64 = static_cast<std::uint64_t>(out.expansionRomOffset) + tableRel;
    if (!Fits(out.biosSize, table64, 6u)) { out.status = ParseStatus::InvalidUcodeTable; return out; }
    const std::uint32_t table = static_cast<std::uint32_t>(table64);
    out.falconTableOffset = table;

    const std::uint8_t version = U8(image, table + 0u);
    const std::uint8_t tableHeaderSize = U8(image, table + 1u);
    const std::uint8_t entrySize = U8(image, table + 2u);
    const std::uint8_t entryCount = U8(image, table + 3u);
    if (version != kUcodeTableVersion1 || tableHeaderSize < 6u || entrySize < 6u) {
      out.status = ParseStatus::InvalidUcodeTable; return out;
    }
    if (!Fits(out.biosSize, static_cast<std::uint64_t>(table) + tableHeaderSize,
              static_cast<std::uint64_t>(entrySize) * entryCount)) {
      out.status = ParseStatus::UcodeEntryOutOfRange; return out;
    }

    for (std::uint32_t e = 0u; e < entryCount; ++e) {
      const std::uint32_t ent = table + tableHeaderSize + e * entrySize;
      if (U8(image, ent) != kFwsecProdAppId) continue;
      const std::uint32_t descRel = U32(image, ent + 2u);
      const std::uint64_t desc64 = static_cast<std::uint64_t>(out.expansionRomOffset) + descRel;
      if (!Fits(out.biosSize, desc64, 4u)) { out.status = ParseStatus::DescriptorOutOfRange; return out; }
      const std::uint32_t desc = static_cast<std::uint32_t>(desc64);
      const std::uint32_t vDesc = U32(image, desc);
      if ((vDesc & 1u) == 0u) { out.status = ParseStatus::UnsupportedDescriptor; return out; }
      out.descriptorVersion = static_cast<std::uint8_t>((vDesc >> 8u) & 0xFFu);
      out.descriptorSize = (vDesc >> 16u) & 0xFFFFu;
      out.descriptorOffset = desc;
      if (out.descriptorVersion != 3u || out.descriptorSize < kDescriptorV3PackedBytes) {
        out.status = ParseStatus::UnsupportedDescriptor; return out;
      }
      if (!Fits(out.biosSize, desc, out.descriptorSize)) { out.status = ParseStatus::DescriptorOutOfRange; return out; }

      out.v3.storedSize = U32(image, desc + 4u);
      out.v3.pkcDataOffset = U32(image, desc + 8u);
      out.v3.interfaceOffset = U32(image, desc + 12u);
      out.v3.imemPhysBase = U32(image, desc + 16u);
      out.v3.imemLoadSize = U32(image, desc + 20u);
      out.v3.imemVirtBase = U32(image, desc + 24u);
      out.v3.dmemPhysBase = U32(image, desc + 28u);
      out.v3.dmemLoadSize = U32(image, desc + 32u);
      out.v3.engineIdMask = U16(image, desc + 36u);
      out.v3.ucodeId = U8(image, desc + 38u);
      out.v3.signatureCount = U8(image, desc + 39u);
      out.v3.signatureVersions = U16(image, desc + 40u);

      const std::uint64_t imageStart = static_cast<std::uint64_t>(desc) + out.descriptorSize;
      const std::uint64_t roundedStored = (static_cast<std::uint64_t>(out.v3.storedSize) + 255u) & ~255ull;
      if (!Fits(out.biosSize, imageStart, roundedStored)) { out.status = ParseStatus::StoredImageOutOfRange; return out; }
      if (static_cast<std::uint64_t>(out.v3.imemLoadSize) + out.v3.interfaceOffset > out.v3.storedSize) {
        out.status = ParseStatus::StoredImageOutOfRange; return out;
      }

      out.status = ParseStatus::Ok;
      return out;
    }
  }

  out.status = ParseStatus::FwsecProductionNotFound;
  return out;
}

const char* ParseStatusName(ParseStatus status) noexcept {
  switch (status) {
    case ParseStatus::Ok: return "ok";
    case ParseStatus::TooSmall: return "too-small";
    case ParseStatus::InvalidRomSignature: return "invalid-rom-signature";
    case ParseStatus::PciDataOutOfRange: return "pci-data-out-of-range";
    case ParseStatus::InvalidPciDataSignature: return "invalid-pcir-signature";
    case ParseStatus::InvalidImageLength: return "invalid-image-length";
    case ParseStatus::ImageOutOfRange: return "image-out-of-range";
    case ParseStatus::BitHeaderNotFound: return "bit-header-not-found";
    case ParseStatus::InvalidBitHeader: return "invalid-bit-header";
    case ParseStatus::TokenTableOutOfRange: return "token-table-out-of-range";
    case ParseStatus::FalconDataOutOfRange: return "falcon-data-out-of-range";
    case ParseStatus::InvalidUcodeTable: return "invalid-ucode-table";
    case ParseStatus::UcodeEntryOutOfRange: return "ucode-entry-out-of-range";
    case ParseStatus::FwsecProductionNotFound: return "fwsec-production-not-found";
    case ParseStatus::DescriptorOutOfRange: return "descriptor-out-of-range";
    case ParseStatus::UnsupportedDescriptor: return "unsupported-descriptor";
    case ParseStatus::StoredImageOutOfRange: return "stored-image-out-of-range";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::vbios
