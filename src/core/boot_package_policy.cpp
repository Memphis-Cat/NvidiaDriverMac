#include "rtxmac/boot_package_policy.hpp"

#include "rtxmac/frts.hpp"

#include <limits>

namespace rtxmac::nvidia::package {
namespace {
constexpr std::uint64_t kGa10xGspSignatureBytes = 0x1000ull;

bool RangeFits(std::uint64_t total, std::uint64_t off, std::uint64_t size) noexcept {
  return off <= total && size <= total - off;
}

bool OffsetInside(std::uint64_t total, std::uint64_t off) noexcept {
  return total != 0u && off < total;
}
} // namespace

SemanticReport CheckGa10xPackageSemantics(
    std::span<const std::uint8_t> bytes,
    const PackageView& view) noexcept {
  SemanticReport out{};
  if (view.status != ParseStatus::Ok || view.packageBytes != bytes.size()) {
    out.failure = SemanticFailure::PackageNotVerified;
    return out;
  }

  const auto gspFirmware = FindSection(bytes, view, SectionKind::GspFirmwareImage);
  const auto gspSignature = FindSection(bytes, view, SectionKind::GspFirmwareSignature);
  const auto gspBootloader = FindSection(bytes, view, SectionKind::GspBootloader);
  const auto fwsec = FindSection(bytes, view, SectionKind::FrtsFwsecImage);
  const auto sec2 = FindSection(bytes, view, SectionKind::Sec2BooterImage);
  if (gspFirmware.empty() || gspSignature.empty() || gspBootloader.empty() ||
      fwsec.empty() || sec2.empty()) {
    out.failure = SemanticFailure::MissingSection;
    return out;
  }

  if (gspSignature.size() != kGa10xGspSignatureBytes) {
    out.failure = SemanticFailure::GspSignatureWrongSize;
    return out;
  }

  const auto bootBytes = static_cast<std::uint64_t>(gspBootloader.size());
  if (!OffsetInside(bootBytes, view.metadata.gspMonitorCodeOffset) ||
      !OffsetInside(bootBytes, view.metadata.gspMonitorDataOffset) ||
      !OffsetInside(bootBytes, view.metadata.gspManifestOffset)) {
    out.failure = SemanticFailure::GspBootloaderMetadataOutOfRange;
    return out;
  }

  const auto fwsecBytes = static_cast<std::uint64_t>(fwsec.size());
  const std::uint64_t imem = view.metadata.fwsecImemLoadSize;
  const std::uint64_t dmem = view.metadata.fwsecDmemLoadSize;
  const std::uint64_t pkc = view.metadata.fwsecPkcDataOffset;
  if (imem == 0u || dmem == 0u ||
      !RangeFits(fwsecBytes, 0u, imem) ||
      !RangeFits(fwsecBytes, imem, dmem) ||
      pkc > std::numeric_limits<std::uint64_t>::max() - imem ||
      !RangeFits(fwsecBytes, imem + pkc, frts::kRsa3072SignatureBytes) ||
      view.metadata.fwsecEngineIdMask == 0u || view.metadata.fwsecUcodeId == 0u) {
    out.failure = SemanticFailure::FwsecMetadataOutOfRange;
    return out;
  }

  const auto sec2Bytes = static_cast<std::uint64_t>(sec2.size());
  if (view.metadata.sec2CodeSize == 0u || view.metadata.sec2DataSize == 0u ||
      !RangeFits(sec2Bytes, view.metadata.sec2CodeOffset, view.metadata.sec2CodeSize) ||
      !RangeFits(sec2Bytes, view.metadata.sec2DataOffset, view.metadata.sec2DataSize)) {
    out.failure = SemanticFailure::Sec2MetadataOutOfRange;
    return out;
  }

  out.valid = true;
  out.failure = SemanticFailure::None;
  return out;
}

const char* SemanticFailureName(SemanticFailure failure) noexcept {
  switch (failure) {
    case SemanticFailure::None: return "ok";
    case SemanticFailure::PackageNotVerified: return "package-not-verified";
    case SemanticFailure::MissingSection: return "missing-section";
    case SemanticFailure::GspSignatureWrongSize: return "gsp-signature-wrong-size";
    case SemanticFailure::GspBootloaderMetadataOutOfRange: return "gsp-bootloader-metadata-out-of-range";
    case SemanticFailure::FwsecMetadataOutOfRange: return "fwsec-metadata-out-of-range";
    case SemanticFailure::Sec2MetadataOutOfRange: return "sec2-metadata-out-of-range";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::package
