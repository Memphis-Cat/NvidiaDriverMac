#include "rtxmac/ga10x_prototype.hpp"

#include <limits>

namespace rtxmac::nvidia::prototype {
namespace {

std::uint64_t SectionBytes(const package::PackageView& view,
                           package::SectionKind kind) noexcept {
  for (const auto& section : view.sections) {
    if (section.kind == kind) return section.size;
  }
  return 0u;
}

bool FitsU32(std::uint64_t value) noexcept {
  return value <= std::numeric_limits<std::uint32_t>::max();
}

} // namespace

Profile BuildGa10xPrototypeProfile(
    const package::PackageView& packageView) noexcept {
  Profile out{};
  if (packageView.status != package::ParseStatus::Ok) {
    out.status = ProfileStatus::PackageNotVerified;
    return out;
  }
  if (!rtxmac::IsKnownRtx3060Ti(packageView.metadata.pci)) {
    out.status = ProfileStatus::UnsupportedTarget;
    return out;
  }

  const std::uint64_t fbSize = packageView.metadata.vramBytes;
  if (fbSize <= kVgaWorkspaceBytes + kFrtsBytes + kRequestedWprHeapBytes +
                    kNonWprHeapBytes + 0x400000ull) {
    out.status = ProfileStatus::InvalidVram;
    return out;
  }

  const std::uint64_t gspImage =
      SectionBytes(packageView, package::SectionKind::GspFirmwareImage);
  const std::uint64_t gspSignature =
      SectionBytes(packageView, package::SectionKind::GspFirmwareSignature);
  const std::uint64_t gspBootloader =
      SectionBytes(packageView, package::SectionKind::GspBootloader);
  const std::uint64_t fwsecImage =
      SectionBytes(packageView, package::SectionKind::FrtsFwsecImage);
  const std::uint64_t sec2Image =
      SectionBytes(packageView, package::SectionKind::Sec2BooterImage);
  if (!gspImage || !gspSignature || !gspBootloader || !fwsecImage || !sec2Image) {
    out.status = ProfileStatus::MissingSection;
    return out;
  }
  if (!FitsU32(gspBootloader) || !FitsU32(sec2Image)) {
    out.status = ProfileStatus::SectionTooLarge;
    return out;
  }

  const auto& meta = packageView.metadata;
  if (!meta.gspAppVersion || !meta.fwsecImemLoadSize || !meta.fwsecDmemLoadSize ||
      !meta.sec2CodeSize || !meta.sec2DataSize) {
    out.status = ProfileStatus::InvalidMetadata;
    return out;
  }

  out.manifestInputs = {
      .fbSize = fbSize,
      .vgaWorkspaceOffset = fbSize - kVgaWorkspaceBytes,
      .vbiosReservedOffset = fbSize - kVgaWorkspaceBytes,
      .wprEndMargin = kWprEndMarginBytes,
      .frtsSize = kFrtsBytes,
      .nonWprHeapSize = kNonWprHeapBytes,
      .requestedWprHeapSize = kRequestedWprHeapBytes,
      .gspFirmwareImageBytes = gspImage,
      .gspSignatureBytes = gspSignature,
      .gspBootloaderBytes = gspBootloader,
      .frtsFwsecImageBytes = fwsecImage,
      .sec2BooterImageBytes = sec2Image,
  };
  out.manifest = gsp::PlanBootManifest(out.manifestInputs);
  if (!out.manifest.valid) {
    out.status = ProfileStatus::ManifestRejected;
    return out;
  }

  out.gspBootloader.status = fw::ParseStatus::Ok;
  out.gspBootloader.bin.dataSize = static_cast<std::uint32_t>(gspBootloader);
  out.gspBootloader.descriptor.appVersion = meta.gspAppVersion;
  out.gspBootloader.descriptor.monitorCodeOffset = meta.gspMonitorCodeOffset;
  out.gspBootloader.descriptor.monitorDataOffset = meta.gspMonitorDataOffset;
  out.gspBootloader.descriptor.manifestOffset = meta.gspManifestOffset;

  out.fwsec.pkcDataOffset = meta.fwsecPkcDataOffset;
  out.fwsec.imemPhysBase = meta.fwsecImemPhysBase;
  out.fwsec.imemLoadSize = meta.fwsecImemLoadSize;
  out.fwsec.imemVirtBase = meta.fwsecImemVirtBase;
  out.fwsec.dmemPhysBase = meta.fwsecDmemPhysBase;
  out.fwsec.dmemLoadSize = meta.fwsecDmemLoadSize;
  out.fwsec.engineIdMask = meta.fwsecEngineIdMask;
  out.fwsec.ucodeId = meta.fwsecUcodeId;

  out.sec2Booter.status = fw::ParseStatus::Ok;
  out.sec2Booter.bin.dataSize = static_cast<std::uint32_t>(sec2Image);
  out.sec2Booter.firstApp.offset = meta.sec2CodeOffset;
  out.sec2Booter.firstApp.size = meta.sec2CodeSize;
  out.sec2Booter.load.osDataOffset = meta.sec2DataOffset;
  out.sec2Booter.load.osDataSize = meta.sec2DataSize;

  out.status = ProfileStatus::Ok;
  return out;
}

const char* ProfileStatusName(ProfileStatus status) noexcept {
  switch (status) {
    case ProfileStatus::Ok: return "ok";
    case ProfileStatus::PackageNotVerified: return "package-not-verified";
    case ProfileStatus::UnsupportedTarget: return "unsupported-target";
    case ProfileStatus::InvalidVram: return "invalid-vram";
    case ProfileStatus::MissingSection: return "missing-section";
    case ProfileStatus::SectionTooLarge: return "section-too-large";
    case ProfileStatus::InvalidMetadata: return "invalid-metadata";
    case ProfileStatus::ManifestRejected: return "manifest-rejected";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::prototype
