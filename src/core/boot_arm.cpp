#include "rtxmac/boot_arm.hpp"

#include <algorithm>
#include <array>

namespace rtxmac::nvidia::gsp {
namespace {

constexpr std::array<std::uint8_t, 30u> kDomain{{
    'R', 'T', 'X', 'M', 'A', 'C', '-', 'E', 'X', 'P', 'E', 'R', 'I', 'M', 'E',
    'N', 'T', 'A', 'L', '-', 'B', 'O', 'O', 'T', '-', 'A', 'R', 'M', '-', '1',
}};
constexpr std::uint64_t kPageBytes = 0x1000ull;

bool DigestPresent(const rtxmac::Sha256Digest& digest) noexcept {
  return std::any_of(digest.begin(), digest.end(),
                     [](std::uint8_t byte) { return byte != 0u; });
}

BootArmFailure CheckContext(const BootArmContext& context) noexcept {
  if (context.version != kBootArmContractVersion) {
    return BootArmFailure::UnsupportedVersion;
  }
  if (!DigestPresent(context.packageDigest) ||
      !DigestPresent(context.stagedPackageDigest)) {
    return BootArmFailure::PackageDigestMissing;
  }
  if (!rtxmac::Sha256Equal(
          context.packageDigest, context.stagedPackageDigest)) {
    return BootArmFailure::PackageDigestMismatch;
  }
  if (context.livePciIdentity == 0u || context.packagePciIdentity == 0u) {
    return BootArmFailure::PciIdentityMissing;
  }
  if (context.livePciIdentity != context.packagePciIdentity) {
    return BootArmFailure::PciIdentityMismatch;
  }
  if (!context.liveBoundaryBound || context.effectiveBoundary == 0u ||
      (context.effectiveBoundary % kPageBytes) != 0u) {
    return BootArmFailure::LiveBoundaryNotBound;
  }
  if (!context.bootSessionReady) return BootArmFailure::BootSessionNotReady;
  if (context.queueBacking == 0u || context.radix3FirmwareRoot == 0u ||
      (context.queueBacking % kPageBytes) != 0u ||
      (context.radix3FirmwareRoot % kPageBytes) != 0u) {
    return BootArmFailure::AddressGraphInvalid;
  }
  if (context.bootPhaseCount != kGa102BootPhaseCount) {
    return BootArmFailure::WrongBootPhaseCount;
  }
  if (!context.sequencePolicyValid) {
    return BootArmFailure::SequencePolicyRejected;
  }
  if (!context.recoveryPolicyValid) {
    return BootArmFailure::RecoveryPolicyRejected;
  }
  return BootArmFailure::None;
}

template <std::size_t N>
void Append(std::array<std::uint8_t, N>& out,
            std::size_t& offset,
            std::span<const std::uint8_t> bytes) noexcept {
  for (const std::uint8_t byte : bytes) out[offset++] = byte;
}

template <std::size_t N>
void AppendLe32(std::array<std::uint8_t, N>& out,
                std::size_t& offset,
                std::uint32_t value) noexcept {
  for (std::size_t i = 0u; i < 4u; ++i) {
    out[offset++] = static_cast<std::uint8_t>(value >> (i * 8u));
  }
}

template <std::size_t N>
void AppendLe64(std::array<std::uint8_t, N>& out,
                std::size_t& offset,
                std::uint64_t value) noexcept {
  for (std::size_t i = 0u; i < 8u; ++i) {
    out[offset++] = static_cast<std::uint8_t>(value >> (i * 8u));
  }
}

bool NoncePresent(std::span<const std::uint8_t> nonce) noexcept {
  return nonce.size() == kBootArmNonceBytes &&
      std::any_of(nonce.begin(), nonce.end(),
                  [](std::uint8_t byte) { return byte != 0u; });
}

} // namespace

BootArmChallenge BuildBootArmChallenge(
    const BootArmContext& context,
    std::span<const std::uint8_t> nonce) noexcept {
  BootArmChallenge out{};
  out.failure = CheckContext(context);
  if (out.failure != BootArmFailure::None) return out;
  if (!NoncePresent(nonce)) {
    out.failure = BootArmFailure::NonceMissing;
    return out;
  }

  // 30 domain + 4 version + 32 digest + five u64 + 4 phase count + 1 proof
  // mask + 32 nonce = 143 bytes. Keep a fixed larger buffer so this path is
  // allocation-free and its serialized form remains explicit.
  std::array<std::uint8_t, 160u> serialized{};
  std::size_t offset = 0u;
  Append(serialized, offset, kDomain);
  AppendLe32(serialized, offset, context.version);
  Append(serialized, offset, context.packageDigest);
  AppendLe64(serialized, offset, context.livePciIdentity);
  AppendLe64(serialized, offset, context.packagePciIdentity);
  AppendLe64(serialized, offset, context.effectiveBoundary);
  AppendLe64(serialized, offset, context.queueBacking);
  AppendLe64(serialized, offset, context.radix3FirmwareRoot);
  AppendLe32(serialized, offset, context.bootPhaseCount);
  serialized[offset++] = 0x0Fu; // all four validated context proofs
  Append(serialized, offset, nonce);

  out.digest = rtxmac::Sha256(
      std::span<const std::uint8_t>(serialized.data(), offset));
  out.valid = true;
  out.failure = BootArmFailure::None;
  return out;
}

BootArmReport CheckBootArmRequest(const BootArmRequest& request) noexcept {
  BootArmReport out{};
  const BootArmChallenge expected =
      BuildBootArmChallenge(request.context, request.nonce);
  out.expectedChallenge = expected.digest;
  if (!expected.valid) {
    out.failure = expected.failure;
    return out;
  }
  if (!request.enableExperimentalWrites) {
    out.failure = BootArmFailure::ExperimentalWritesNotRequested;
    return out;
  }
  if (request.confirmation != kExperimentalBootArmConfirmation) {
    out.failure = BootArmFailure::ConfirmationMismatch;
    return out;
  }
  if (!rtxmac::Sha256Equal(
          request.challengeResponse, expected.digest)) {
    out.failure = BootArmFailure::ChallengeMismatch;
    return out;
  }

  out.accepted = true;
  out.failure = BootArmFailure::None;
  return out;
}

const char* BootArmFailureName(BootArmFailure failure) noexcept {
  switch (failure) {
    case BootArmFailure::None: return "none";
    case BootArmFailure::UnsupportedVersion: return "unsupported-version";
    case BootArmFailure::PackageDigestMissing: return "package-digest-missing";
    case BootArmFailure::PackageDigestMismatch: return "package-digest-mismatch";
    case BootArmFailure::PciIdentityMissing: return "pci-identity-missing";
    case BootArmFailure::PciIdentityMismatch: return "pci-identity-mismatch";
    case BootArmFailure::LiveBoundaryNotBound: return "live-boundary-not-bound";
    case BootArmFailure::BootSessionNotReady: return "boot-session-not-ready";
    case BootArmFailure::AddressGraphInvalid: return "address-graph-invalid";
    case BootArmFailure::WrongBootPhaseCount: return "wrong-boot-phase-count";
    case BootArmFailure::SequencePolicyRejected:
      return "sequence-policy-rejected";
    case BootArmFailure::RecoveryPolicyRejected:
      return "recovery-policy-rejected";
    case BootArmFailure::NonceMissing: return "nonce-missing";
    case BootArmFailure::ExperimentalWritesNotRequested:
      return "experimental-writes-not-requested";
    case BootArmFailure::ConfirmationMismatch: return "confirmation-mismatch";
    case BootArmFailure::ChallengeMismatch: return "challenge-mismatch";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::gsp
