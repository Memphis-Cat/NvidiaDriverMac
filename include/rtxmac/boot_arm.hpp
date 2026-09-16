#pragma once

#include "rtxmac/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace rtxmac::nvidia::gsp {

inline constexpr std::uint32_t kBootArmContractVersion = 1u;
inline constexpr std::size_t kBootArmNonceBytes = 32u;
inline constexpr std::uint32_t kGa102BootPhaseCount = 12u;

// Deliberately awkward second-factor value. This is an accident-prevention
// acknowledgement, not a secret or an authentication mechanism.
inline constexpr std::uint64_t kExperimentalBootArmConfirmation =
    0x5254584D41434152ull; // "RTXMACAR"

enum class BootArmFailure : std::uint8_t {
  None = 0,
  UnsupportedVersion,
  PackageDigestMissing,
  PackageDigestMismatch,
  PciIdentityMissing,
  PciIdentityMismatch,
  LiveBoundaryNotBound,
  BootSessionNotReady,
  AddressGraphInvalid,
  WrongBootPhaseCount,
  SequencePolicyRejected,
  RecoveryPolicyRejected,
  NonceMissing,
  ExperimentalWritesNotRequested,
  ConfirmationMismatch,
  ChallengeMismatch,
};

// Exact cold-session facts covered by the arming challenge. Package and staged
// digests are duplicated intentionally so the contract independently proves
// that the retained DMA state belongs to the bytes the operator selected.
struct BootArmContext {
  std::uint32_t version{kBootArmContractVersion};
  rtxmac::Sha256Digest packageDigest{};
  rtxmac::Sha256Digest stagedPackageDigest{};
  std::uint64_t livePciIdentity{};
  std::uint64_t packagePciIdentity{};
  std::uint64_t effectiveBoundary{};
  std::uint64_t queueBacking{};
  std::uint64_t radix3FirmwareRoot{};
  std::uint32_t bootPhaseCount{};
  bool liveBoundaryBound{};
  bool bootSessionReady{};
  bool sequencePolicyValid{};
  bool recoveryPolicyValid{};
};

struct BootArmChallenge {
  bool valid{};
  BootArmFailure failure{BootArmFailure::None};
  rtxmac::Sha256Digest digest{};
};

// Builds a domain-separated SHA-256 over the exact session context and a
// caller-provided non-zero 256-bit nonce. No hardware access or mutation.
[[nodiscard]] BootArmChallenge BuildBootArmChallenge(
    const BootArmContext& context,
    std::span<const std::uint8_t> nonce) noexcept;

struct BootArmRequest {
  BootArmContext context{};
  std::array<std::uint8_t, kBootArmNonceBytes> nonce{};
  rtxmac::Sha256Digest challengeResponse{};
  std::uint64_t confirmation{};
  bool enableExperimentalWrites{};
};

struct BootArmReport {
  bool accepted{};
  BootArmFailure failure{BootArmFailure::None};
  rtxmac::Sha256Digest expectedChallenge{};
};

// Requires all context proofs, an exact challenge response, the explicit
// confirmation constant, and a separate write-enable request. This function
// remains portable and performs no hardware access.
[[nodiscard]] BootArmReport CheckBootArmRequest(
    const BootArmRequest& request) noexcept;

[[nodiscard]] const char* BootArmFailureName(BootArmFailure failure) noexcept;

} // namespace rtxmac::nvidia::gsp
