#include "rtxmac/boot_arm.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <string_view>

namespace {

rtxmac::nvidia::gsp::BootArmRequest MakeRequest() {
  using namespace rtxmac::nvidia::gsp;
  BootArmRequest request{};
  for (std::size_t i = 0u; i < request.context.packageDigest.size(); ++i) {
    request.context.packageDigest[i] = static_cast<std::uint8_t>(i + 1u);
  }
  request.context.stagedPackageDigest = request.context.packageDigest;
  request.context.livePciIdentity = 0x153810DE248910DEull;
  request.context.packagePciIdentity = request.context.livePciIdentity;
  request.context.effectiveBoundary = 0x1FF000000ull;
  request.context.queueBacking = 0x10000000ull;
  request.context.radix3FirmwareRoot = 0x20000000ull;
  request.context.bootPhaseCount = kGa102BootPhaseCount;
  request.context.liveBoundaryBound = true;
  request.context.bootSessionReady = true;
  request.context.sequencePolicyValid = true;
  request.context.recoveryPolicyValid = true;
  for (std::size_t i = 0u; i < request.nonce.size(); ++i) {
    request.nonce[i] = static_cast<std::uint8_t>(0xA0u + i);
  }
  const auto challenge = BuildBootArmChallenge(request.context, request.nonce);
  assert(challenge.valid);
  request.challengeResponse = challenge.digest;
  request.confirmation = kExperimentalBootArmConfirmation;
  request.enableExperimentalWrites = true;
  return request;
}

} // namespace

int main() {
  using namespace rtxmac::nvidia::gsp;

  const BootArmRequest ready = MakeRequest();
  const auto accepted = CheckBootArmRequest(ready);
  assert(accepted.accepted);
  assert(accepted.failure == BootArmFailure::None);

  auto noWriteRequest = ready;
  noWriteRequest.enableExperimentalWrites = false;
  assert(CheckBootArmRequest(noWriteRequest).failure ==
         BootArmFailure::ExperimentalWritesNotRequested);

  auto wrongConfirmation = ready;
  wrongConfirmation.confirmation ^= 1u;
  assert(CheckBootArmRequest(wrongConfirmation).failure ==
         BootArmFailure::ConfirmationMismatch);

  auto wrongResponse = ready;
  wrongResponse.challengeResponse[0] ^= 1u;
  assert(CheckBootArmRequest(wrongResponse).failure ==
         BootArmFailure::ChallengeMismatch);

  auto mismatchedPackage = ready;
  mismatchedPackage.context.stagedPackageDigest[0] ^= 1u;
  assert(CheckBootArmRequest(mismatchedPackage).failure ==
         BootArmFailure::PackageDigestMismatch);

  auto wrongGpu = ready;
  wrongGpu.context.livePciIdentity ^= 1u;
  assert(CheckBootArmRequest(wrongGpu).failure ==
         BootArmFailure::PciIdentityMismatch);

  auto staleBoundary = ready;
  staleBoundary.context.liveBoundaryBound = false;
  assert(CheckBootArmRequest(staleBoundary).failure ==
         BootArmFailure::LiveBoundaryNotBound);

  auto notReady = ready;
  notReady.context.bootSessionReady = false;
  assert(CheckBootArmRequest(notReady).failure ==
         BootArmFailure::BootSessionNotReady);

  auto badAddress = ready;
  badAddress.context.radix3FirmwareRoot += 1u;
  assert(CheckBootArmRequest(badAddress).failure ==
         BootArmFailure::AddressGraphInvalid);

  auto wrongPhases = ready;
  --wrongPhases.context.bootPhaseCount;
  assert(CheckBootArmRequest(wrongPhases).failure ==
         BootArmFailure::WrongBootPhaseCount);

  auto noPolicy = ready;
  noPolicy.context.sequencePolicyValid = false;
  assert(CheckBootArmRequest(noPolicy).failure ==
         BootArmFailure::SequencePolicyRejected);

  auto noRecovery = ready;
  noRecovery.context.recoveryPolicyValid = false;
  assert(CheckBootArmRequest(noRecovery).failure ==
         BootArmFailure::RecoveryPolicyRejected);

  auto zeroNonce = ready;
  zeroNonce.nonce.fill(0u);
  assert(CheckBootArmRequest(zeroNonce).failure ==
         BootArmFailure::NonceMissing);

  auto changedNonce = ready;
  changedNonce.nonce[0] ^= 1u;
  assert(CheckBootArmRequest(changedNonce).failure ==
         BootArmFailure::ChallengeMismatch);

  auto challengeContext = ready.context;
  challengeContext.version = 2u;
  assert(BuildBootArmChallenge(challengeContext, ready.nonce).failure ==
         BootArmFailure::UnsupportedVersion);

  assert(std::string_view(BootArmFailureName(BootArmFailure::ChallengeMismatch)) ==
         "challenge-mismatch");
  std::cout << "rtxmac explicit experimental boot-arm contract tests passed\n";
}
