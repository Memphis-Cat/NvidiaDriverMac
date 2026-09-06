#include "rtxmac/boot_package.hpp"

#include "rtxmac/elf.hpp"
#include "rtxmac/frts.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace rtxmac::nvidia::package {
namespace {
constexpr std::size_t kVbiosDescriptorV3Bytes = 44u;
constexpr std::size_t kReservedHeaderBegin = 116u;
constexpr std::size_t kReservedHeaderEnd = kHeaderBytes;

void Store16(std::span<std::uint8_t> out, std::size_t off, std::uint16_t v) noexcept {
  out[off + 0u] = static_cast<std::uint8_t>(v);
  out[off + 1u] = static_cast<std::uint8_t>(v >> 8u);
}
void Store32(std::span<std::uint8_t> out, std::size_t off, std::uint32_t v) noexcept {
  for (std::size_t i = 0u; i < 4u; ++i) out[off + i] = static_cast<std::uint8_t>(v >> (i * 8u));
}
void Store64(std::span<std::uint8_t> out, std::size_t off, std::uint64_t v) noexcept {
  for (std::size_t i = 0u; i < 8u; ++i) out[off + i] = static_cast<std::uint8_t>(v >> (i * 8u));
}
std::uint16_t Load16(std::span<const std::uint8_t> in, std::size_t off) noexcept {
  return static_cast<std::uint16_t>(in[off]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[off + 1u]) << 8u);
}
std::uint32_t Load32(std::span<const std::uint8_t> in, std::size_t off) noexcept {
  return static_cast<std::uint32_t>(in[off + 0u]) |
      (static_cast<std::uint32_t>(in[off + 1u]) << 8u) |
      (static_cast<std::uint32_t>(in[off + 2u]) << 16u) |
      (static_cast<std::uint32_t>(in[off + 3u]) << 24u);
}
std::uint64_t Load64(std::span<const std::uint8_t> in, std::size_t off) noexcept {
  std::uint64_t out = 0u;
  for (std::size_t i = 0u; i < 8u; ++i) out |= static_cast<std::uint64_t>(in[off + i]) << (i * 8u);
  return out;
}

bool Fits(std::size_t total, std::uint64_t off, std::uint64_t size) noexcept {
  return off <= total && size <= static_cast<std::uint64_t>(total) - off;
}

std::uint64_t AlignUp(std::uint64_t v, std::uint64_t a) noexcept {
  if (a == 0u || (a & (a - 1u)) != 0u || v > std::numeric_limits<std::uint64_t>::max() - (a - 1u)) return 0u;
  return (v + a - 1u) & ~(a - 1u);
}

bool IsKnownKind(SectionKind kind) noexcept {
  const auto v = static_cast<std::uint32_t>(kind);
  return v >= static_cast<std::uint32_t>(SectionKind::GspFirmwareImage) &&
      v <= static_cast<std::uint32_t>(SectionKind::Sec2BooterImage);
}

std::span<const std::uint8_t> Slice(std::span<const std::uint8_t> bytes,
                                    std::uint64_t off,
                                    std::uint64_t size) noexcept {
  if (!Fits(bytes.size(), off, size) || off > std::numeric_limits<std::size_t>::max() ||
      size > std::numeric_limits<std::size_t>::max()) return {};
  return bytes.subspan(static_cast<std::size_t>(off), static_cast<std::size_t>(size));
}

void SerializeMetadata(std::span<std::uint8_t> out, const StaticMetadata& m) noexcept {
  Store16(out, 40u, m.pci.vendor);
  Store16(out, 42u, m.pci.device);
  Store16(out, 44u, m.pci.subsystemVendor);
  Store16(out, 46u, m.pci.subsystemDevice);
  Store64(out, 48u, m.vramBytes);
  Store32(out, 56u, m.gspAppVersion);
  Store32(out, 60u, m.gspMonitorCodeOffset);
  Store32(out, 64u, m.gspMonitorDataOffset);
  Store32(out, 68u, m.gspManifestOffset);
  Store32(out, 72u, m.fwsecPkcDataOffset);
  Store32(out, 76u, m.fwsecImemPhysBase);
  Store32(out, 80u, m.fwsecImemLoadSize);
  Store32(out, 84u, m.fwsecImemVirtBase);
  Store32(out, 88u, m.fwsecDmemPhysBase);
  Store32(out, 92u, m.fwsecDmemLoadSize);
  Store16(out, 96u, m.fwsecEngineIdMask);
  out[98u] = m.fwsecUcodeId;
  Store32(out, 100u, m.sec2CodeOffset);
  Store32(out, 104u, m.sec2CodeSize);
  Store32(out, 108u, m.sec2DataOffset);
  Store32(out, 112u, m.sec2DataSize);
}

StaticMetadata ParseMetadata(std::span<const std::uint8_t> in) noexcept {
  StaticMetadata m{};
  m.pci.vendor = Load16(in, 40u);
  m.pci.device = Load16(in, 42u);
  m.pci.subsystemVendor = Load16(in, 44u);
  m.pci.subsystemDevice = Load16(in, 46u);
  m.vramBytes = Load64(in, 48u);
  m.gspAppVersion = Load32(in, 56u);
  m.gspMonitorCodeOffset = Load32(in, 60u);
  m.gspMonitorDataOffset = Load32(in, 64u);
  m.gspManifestOffset = Load32(in, 68u);
  m.fwsecPkcDataOffset = Load32(in, 72u);
  m.fwsecImemPhysBase = Load32(in, 76u);
  m.fwsecImemLoadSize = Load32(in, 80u);
  m.fwsecImemVirtBase = Load32(in, 84u);
  m.fwsecDmemPhysBase = Load32(in, 88u);
  m.fwsecDmemLoadSize = Load32(in, 92u);
  m.fwsecEngineIdMask = Load16(in, 96u);
  m.fwsecUcodeId = in[98u];
  m.sec2CodeOffset = Load32(in, 100u);
  m.sec2CodeSize = Load32(in, 104u);
  m.sec2DataOffset = Load32(in, 108u);
  m.sec2DataSize = Load32(in, 112u);
  return m;
}

} // namespace

BuildResult BuildGa10xPackage(const SourceInputs& in) {
  BuildResult out{};
  if (!rtxmac::IsKnownRtx3060Ti(in.pci)) {
    out.status = BuildStatus::InvalidTarget;
    return out;
  }
  const auto frtsCommand = frts::BuildFrtsCommand(in.vramBytes);
  if (!frtsCommand || (in.vramBytes & (kPayloadAlignment - 1u)) != 0u) {
    out.status = BuildStatus::InvalidVram;
    return out;
  }

  const auto fwsec = vbios::ParseProductionFwsec(in.vbios);
  if (fwsec.status != vbios::ParseStatus::Ok) {
    out.status = BuildStatus::VbiosParseFailed;
    return out;
  }
  if (fwsec.descriptorSize <= kVbiosDescriptorV3Bytes) {
    out.status = BuildStatus::VbiosSignatureMissing;
    return out;
  }
  const std::uint64_t signatureOffset = static_cast<std::uint64_t>(fwsec.descriptorOffset) + kVbiosDescriptorV3Bytes;
  const std::uint64_t signatureBytes = fwsec.descriptorSize - kVbiosDescriptorV3Bytes;
  const std::uint64_t storedImageOffset = static_cast<std::uint64_t>(fwsec.descriptorOffset) + fwsec.descriptorSize;
  const std::uint64_t roundedStored = (static_cast<std::uint64_t>(fwsec.v3.storedSize) + 255u) & ~255ull;
  const auto vbiosSignature = Slice(in.vbios, signatureOffset, signatureBytes);
  const auto storedImage = Slice(in.vbios, storedImageOffset, roundedStored);
  if (vbiosSignature.empty() || storedImage.empty()) {
    out.status = BuildStatus::VbiosSignatureMissing;
    return out;
  }
  const auto frtsPlan = frts::PlanFwsecFrtsPatch(fwsec.v3, storedImage, vbiosSignature.size());
  const auto patchedFrts = frts::ApplyFwsecFrtsPatch(storedImage, frtsPlan, *frtsCommand, vbiosSignature);
  if (!patchedFrts) {
    out.status = BuildStatus::FrtsPatchFailed;
    return out;
  }

  const auto fwSection = rtxmac::elf::FindSection(in.gspElf, ".fwimage");
  if (fwSection.status != rtxmac::elf::Status::Ok || fwSection.size == 0u) {
    out.status = BuildStatus::GspFirmwareSectionMissing;
    return out;
  }
  const auto sigSection = rtxmac::elf::FindSection(in.gspElf, ".fwsignature_ga10x");
  if (sigSection.status != rtxmac::elf::Status::Ok || sigSection.size == 0u) {
    out.status = BuildStatus::GspSignatureSectionMissing;
    return out;
  }
  const auto gspFirmware = Slice(in.gspElf, fwSection.offset, fwSection.size);
  const auto gspSignature = Slice(in.gspElf, sigSection.offset, sigSection.size);
  if (gspFirmware.empty() || gspSignature.empty()) {
    out.status = BuildStatus::GspFirmwareSectionMissing;
    return out;
  }

  const auto gspBoot = fw::ParseRiscvBootloader(in.gspBootloaderContainer);
  if (gspBoot.status != fw::ParseStatus::Ok || gspBoot.bin.dataSize == 0u) {
    out.status = BuildStatus::GspBootloaderParseFailed;
    return out;
  }
  const auto gspBootPayload = Slice(in.gspBootloaderContainer, gspBoot.bin.dataOffset, gspBoot.bin.dataSize);
  if (gspBootPayload.empty()) {
    out.status = BuildStatus::GspBootloaderParseFailed;
    return out;
  }

  const auto sec2Info = fw::ParseBooterImage(in.sec2BooterContainer);
  if (sec2Info.status != fw::ParseStatus::Ok) {
    out.status = BuildStatus::Sec2BooterParseFailed;
    return out;
  }
  const auto patchedSec2 = fw::PatchBooterProductionSignature(in.sec2BooterContainer, sec2Info);
  if (patchedSec2.status != fw::BooterPatchStatus::Ok || patchedSec2.bytes.empty()) {
    out.status = BuildStatus::Sec2BooterPatchFailed;
    return out;
  }

  out.metadata = {
      .pci = in.pci,
      .vramBytes = in.vramBytes,
      .gspAppVersion = gspBoot.descriptor.appVersion,
      .gspMonitorCodeOffset = gspBoot.descriptor.monitorCodeOffset,
      .gspMonitorDataOffset = gspBoot.descriptor.monitorDataOffset,
      .gspManifestOffset = gspBoot.descriptor.manifestOffset,
      .fwsecPkcDataOffset = fwsec.v3.pkcDataOffset,
      .fwsecImemPhysBase = fwsec.v3.imemPhysBase,
      .fwsecImemLoadSize = fwsec.v3.imemLoadSize,
      .fwsecImemVirtBase = fwsec.v3.imemVirtBase,
      .fwsecDmemPhysBase = fwsec.v3.dmemPhysBase,
      .fwsecDmemLoadSize = fwsec.v3.dmemLoadSize,
      .fwsecEngineIdMask = fwsec.v3.engineIdMask,
      .fwsecUcodeId = fwsec.v3.ucodeId,
      .sec2CodeOffset = sec2Info.firstApp.offset,
      .sec2CodeSize = sec2Info.firstApp.size,
      .sec2DataOffset = sec2Info.load.osDataOffset,
      .sec2DataSize = sec2Info.load.osDataSize,
  };

  const std::array<std::span<const std::uint8_t>, kSectionCount> payloads{
      gspFirmware,
      gspSignature,
      gspBootPayload,
      std::span<const std::uint8_t>(*patchedFrts),
      std::span<const std::uint8_t>(patchedSec2.bytes),
  };
  const std::array kinds{
      SectionKind::GspFirmwareImage,
      SectionKind::GspFirmwareSignature,
      SectionKind::GspBootloader,
      SectionKind::FrtsFwsecImage,
      SectionKind::Sec2BooterImage,
  };

  const std::uint64_t tableEnd = kSectionTableOffset + static_cast<std::uint64_t>(kSectionCount) * kSectionRecordBytes;
  std::uint64_t cursor = AlignUp(tableEnd, kPayloadAlignment);
  if (cursor == 0u) {
    out.status = BuildStatus::SizeOverflow;
    return out;
  }
  for (std::size_t i = 0u; i < kSectionCount; ++i) {
    if (payloads[i].empty() || payloads[i].size() > std::numeric_limits<std::uint64_t>::max() - cursor) {
      out.status = BuildStatus::SizeOverflow;
      return out;
    }
    out.sections[i] = {
        .kind = kinds[i],
        .offset = cursor,
        .size = static_cast<std::uint64_t>(payloads[i].size()),
        .sha256 = rtxmac::Sha256(payloads[i]),
    };
    cursor += payloads[i].size();
    if (i + 1u != kSectionCount) {
      cursor = AlignUp(cursor, kPayloadAlignment);
      if (cursor == 0u) {
        out.status = BuildStatus::SizeOverflow;
        return out;
      }
    }
  }
  if (cursor > std::numeric_limits<std::size_t>::max()) {
    out.status = BuildStatus::SizeOverflow;
    return out;
  }

  out.bytes.assign(static_cast<std::size_t>(cursor), 0u);
  auto bytes = std::span<std::uint8_t>(out.bytes);
  std::copy(kMagic.begin(), kMagic.end(), out.bytes.begin());
  Store32(bytes, 8u, kVersion);
  Store32(bytes, 12u, kHeaderBytes);
  Store64(bytes, 16u, cursor);
  Store64(bytes, 24u, kSectionTableOffset);
  Store32(bytes, 32u, kSectionCount);
  Store32(bytes, 36u, kSectionRecordBytes);
  SerializeMetadata(bytes, out.metadata);

  for (std::size_t i = 0u; i < kSectionCount; ++i) {
    const std::size_t ro = static_cast<std::size_t>(kSectionTableOffset) + i * kSectionRecordBytes;
    Store32(bytes, ro + 0u, static_cast<std::uint32_t>(out.sections[i].kind));
    Store32(bytes, ro + 4u, 0u);
    Store64(bytes, ro + 8u, out.sections[i].offset);
    Store64(bytes, ro + 16u, out.sections[i].size);
    std::copy(out.sections[i].sha256.begin(), out.sections[i].sha256.end(), out.bytes.begin() + ro + 24u);
    std::copy(payloads[i].begin(), payloads[i].end(), out.bytes.begin() + static_cast<std::size_t>(out.sections[i].offset));
  }

  out.status = BuildStatus::Ok;
  return out;
}

PackageView ParseAndVerify(std::span<const std::uint8_t> bytes) noexcept {
  PackageView out{};
  const std::uint64_t tableBytes = static_cast<std::uint64_t>(kSectionCount) * kSectionRecordBytes;
  if (bytes.size() < kHeaderBytes || !Fits(bytes.size(), kSectionTableOffset, tableBytes)) return out;
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    out.status = ParseStatus::BadMagic;
    return out;
  }
  if (Load32(bytes, 8u) != kVersion) {
    out.status = ParseStatus::UnsupportedVersion;
    return out;
  }
  if (Load32(bytes, 12u) != kHeaderBytes || Load64(bytes, 24u) != kSectionTableOffset ||
      Load32(bytes, 32u) != kSectionCount || Load32(bytes, 36u) != kSectionRecordBytes || bytes[99u] != 0u) {
    out.status = ParseStatus::BadHeader;
    return out;
  }
  for (std::size_t i = kReservedHeaderBegin; i < kReservedHeaderEnd; ++i) {
    if (bytes[i] != 0u) {
      out.status = ParseStatus::BadHeader;
      return out;
    }
  }
  out.packageBytes = Load64(bytes, 16u);
  if (out.packageBytes != bytes.size()) {
    out.status = ParseStatus::BadPackageSize;
    return out;
  }
  out.metadata = ParseMetadata(bytes);
  if (!rtxmac::IsKnownRtx3060Ti(out.metadata.pci) ||
      !frts::BuildFrtsCommand(out.metadata.vramBytes).has_value() ||
      (out.metadata.vramBytes & (kPayloadAlignment - 1u)) != 0u) {
    out.status = ParseStatus::BadTarget;
    return out;
  }

  const std::uint64_t payloadBegin = AlignUp(kSectionTableOffset + tableBytes, kPayloadAlignment);
  if (payloadBegin == 0u) {
    out.status = ParseStatus::BadSectionTable;
    return out;
  }
  std::array<bool, kSectionCount> seen{};
  for (std::size_t i = 0u; i < kSectionCount; ++i) {
    const std::size_t ro = static_cast<std::size_t>(kSectionTableOffset) + i * kSectionRecordBytes;
    const auto kind = static_cast<SectionKind>(Load32(bytes, ro + 0u));
    if (Load32(bytes, ro + 4u) != 0u || !IsKnownKind(kind)) {
      out.status = ParseStatus::DuplicateOrUnknownSection;
      return out;
    }
    const std::size_t kindIndex = static_cast<std::size_t>(static_cast<std::uint32_t>(kind) - 1u);
    if (seen[kindIndex]) {
      out.status = ParseStatus::DuplicateOrUnknownSection;
      return out;
    }
    seen[kindIndex] = true;

    SectionRecord rec{};
    rec.kind = kind;
    rec.offset = Load64(bytes, ro + 8u);
    rec.size = Load64(bytes, ro + 16u);
    std::copy(bytes.begin() + ro + 24u, bytes.begin() + ro + 56u, rec.sha256.begin());
    if (rec.size == 0u || !Fits(bytes.size(), rec.offset, rec.size) || rec.offset < payloadBegin) {
      out.status = ParseStatus::BadSectionRange;
      return out;
    }
    if ((rec.offset & (kPayloadAlignment - 1u)) != 0u) {
      out.status = ParseStatus::BadSectionAlignment;
      return out;
    }
    out.sections[i] = rec;
  }

  for (std::size_t i = 0u; i < kSectionCount; ++i) {
    const std::uint64_t a0 = out.sections[i].offset;
    const std::uint64_t a1 = a0 + out.sections[i].size;
    for (std::size_t j = i + 1u; j < kSectionCount; ++j) {
      const std::uint64_t b0 = out.sections[j].offset;
      const std::uint64_t b1 = b0 + out.sections[j].size;
      if (a0 < b1 && b0 < a1) {
        out.status = ParseStatus::OverlappingSections;
        return out;
      }
    }
  }

  for (const auto& rec : out.sections) {
    const auto payload = Slice(bytes, rec.offset, rec.size);
    if (payload.empty() || !rtxmac::Sha256Equal(rtxmac::Sha256(payload), rec.sha256)) {
      out.status = ParseStatus::HashMismatch;
      return out;
    }
  }

  out.status = ParseStatus::Ok;
  return out;
}

std::span<const std::uint8_t> FindSection(
    std::span<const std::uint8_t> bytes,
    const PackageView& view,
    SectionKind kind) noexcept {
  if (view.status != ParseStatus::Ok || !IsKnownKind(kind) || view.packageBytes != bytes.size()) return {};
  for (const auto& rec : view.sections) {
    if (rec.kind == kind) return Slice(bytes, rec.offset, rec.size);
  }
  return {};
}

const char* BuildStatusName(BuildStatus status) noexcept {
  switch (status) {
    case BuildStatus::Ok: return "ok";
    case BuildStatus::InvalidTarget: return "invalid-target";
    case BuildStatus::InvalidVram: return "invalid-vram";
    case BuildStatus::VbiosParseFailed: return "vbios-parse-failed";
    case BuildStatus::VbiosSignatureMissing: return "vbios-signature-missing";
    case BuildStatus::FrtsPatchFailed: return "frts-patch-failed";
    case BuildStatus::GspFirmwareSectionMissing: return "gsp-fwimage-missing";
    case BuildStatus::GspSignatureSectionMissing: return "gsp-ga10x-signature-missing";
    case BuildStatus::GspBootloaderParseFailed: return "gsp-bootloader-parse-failed";
    case BuildStatus::Sec2BooterParseFailed: return "sec2-booter-parse-failed";
    case BuildStatus::Sec2BooterPatchFailed: return "sec2-booter-patch-failed";
    case BuildStatus::SizeOverflow: return "size-overflow";
  }
  return "unknown";
}

const char* ParseStatusName(ParseStatus status) noexcept {
  switch (status) {
    case ParseStatus::Ok: return "ok";
    case ParseStatus::TooSmall: return "too-small";
    case ParseStatus::BadMagic: return "bad-magic";
    case ParseStatus::UnsupportedVersion: return "unsupported-version";
    case ParseStatus::BadHeader: return "bad-header";
    case ParseStatus::BadPackageSize: return "bad-package-size";
    case ParseStatus::BadTarget: return "bad-target";
    case ParseStatus::BadSectionTable: return "bad-section-table";
    case ParseStatus::DuplicateOrUnknownSection: return "duplicate-or-unknown-section";
    case ParseStatus::BadSectionRange: return "bad-section-range";
    case ParseStatus::BadSectionAlignment: return "bad-section-alignment";
    case ParseStatus::OverlappingSections: return "overlapping-sections";
    case ParseStatus::HashMismatch: return "hash-mismatch";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::package
