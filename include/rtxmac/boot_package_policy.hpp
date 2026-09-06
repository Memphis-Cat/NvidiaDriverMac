#pragma once

#include "rtxmac/boot_package.hpp"

#include <cstdint>
#include <span>

namespace rtxmac::nvidia::package {

enum class SemanticFailure : std::uint8_t {
  None = 0,
  PackageNotVerified,
  MissingSection,
  GspSignatureWrongSize,
  GspBootloaderMetadataOutOfRange,
  FwsecMetadataOutOfRange,
  Sec2MetadataOutOfRange,
};

struct SemanticReport {
  bool valid{};
  SemanticFailure failure{SemanticFailure::PackageNotVerified};
};

// Second-stage validation after ParseAndVerify(). This checks assumptions that
// belong to the GA104/GA10x boot protocol rather than to the generic container:
// exact GSP signature size and every metadata offset/size against the prepared
// section it will address during Falcon/GSP boot.
[[nodiscard]] SemanticReport CheckGa10xPackageSemantics(
    std::span<const std::uint8_t> bytes,
    const PackageView& view) noexcept;

[[nodiscard]] const char* SemanticFailureName(SemanticFailure failure) noexcept;

} // namespace rtxmac::nvidia::package
