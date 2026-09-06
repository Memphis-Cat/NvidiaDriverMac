#include "rtxmac/boot_orchestrator.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>

namespace {
using namespace rtxmac::nvidia;
using namespace rtxmac::nvidia::gsp;

struct Fixture {
  BootManifest manifest;
  BootSequence sequence;
  BootSequencePolicyExpectations expectations;
  BootCommitPrerequisites preflight;
};

Fixture MakeFixture() {
  ManifestInputs in{
      .fbSize = 0x200000000ull,
      .vgaWorkspaceOffset = 0x1FFF00000ull,
      .vbiosReservedOffset = 0x1FFF00000ull,
      .wprEndMargin = 0u,
      .frtsSize = 0x100000ull,
      .nonWprHeapSize = 0x100000ull,
      .requestedWprHeapSize = 0x8100000ull,
      .gspFirmwareImageBytes = 0x200000ull,
      .gspSignatureBytes = 0x1000ull,
      .gspBootloaderBytes = 0x18000ull,
      .frtsFwsecImageBytes = 0x20000ull,
      .sec2BooterImageBytes = 0x10000ull,
  };
  auto manifest = PlanBootManifest(in);
  assert(manifest.valid);

  ResolvedAddresses addresses{
      .queueBacking = 0x10000000ull,
      .cachedArguments = 0x10100000ull,
      .libosInitArguments = 0x10200000ull,
      .wprMetadata = 0x10300000ull,
      .radix3FirmwareRoot = 0x10400000ull,
      .firmwareSignature = 0x14000000ull,
      .gspBootloader = 0x14100000ull,
      .frtsFwsecImage = 0x1F0000000ull,
      .sec2BooterImage = 0x1F0100000ull,
  };

  fw::RiscvBootloaderInfo gspBootloader{};
  gspBootloader.status = fw::ParseStatus::Ok;
  gspBootloader.bin.dataSize = static_cast<std::uint32_t>(in.gspBootloaderBytes);
  gspBootloader.descriptor.appVersion = 0x10203040u;

  vbios::DescriptorV3 fwsec{};
  fwsec.pkcDataOffset = 0x100u;
  fwsec.imemLoadSize = 0x1000u;
  fwsec.dmemLoadSize = 0x1000u;
  fwsec.engineIdMask = 1u;
  fwsec.ucodeId = 5u;

  fw::BooterImageInfo sec2{};
  sec2.status = fw::ParseStatus::Ok;
  sec2.bin.dataSize = static_cast<std::uint32_t>(in.sec2BooterImageBytes);
  sec2.firstApp.offset = 0x1000u;
  sec2.firstApp.size = 0x1000u;
  sec2.load.osDataOffset = 0x3000u;
  sec2.load.osDataSize = 0x1000u;

  auto sequence = PlanBootSequence(
      manifest, addresses, gspBootloader, fwsec, sec2, 0x174u);
  assert(sequence.valid);
  const BootSequencePolicyExpectations expectations{
      .libosInitArguments = addresses.libosInitArguments,
      .gspAppVersion = gspBootloader.descriptor.appVersion,
  };
  assert(CheckGa102BootSequencePolicy(manifest, sequence, expectations).valid);

  BootCommitPrerequisites preflight{
      .executionGateEnabled = true,
      .pciMemorySpaceEnabled = true,
      .pciBusMasterEnabled = true,
      .sysmemFlushPageProgrammed = true,
      .allSystemDmaResolved = true,
      .resolvedArtifactsBuilt = true,
      .frtsFramebufferImageVerified = true,
      .sec2FramebufferImageVerified = true,
      .bootstrapRpcPrefilled = true,
      .falconPlansPolicyValid = true,
      .wpr2AddrHi = 0u,
  };
  assert(CheckBootCommitPreflight(preflight).ready);
  return {std::move(manifest), std::move(sequence), expectations, preflight};
}
}

int main() {
  using namespace rtxmac::nvidia::gsp;
  auto fx = MakeFixture();

  auto rejectedPrereq = fx.preflight;
  rejectedPrereq.executionGateEnabled = false;
  auto rejected = BeginBootAttempt(
      fx.manifest, fx.sequence, fx.expectations, rejectedPrereq);
  assert(rejected.state == BootAttemptState::Rejected);
  assert(BootAttemptTerminal(rejected));

  auto wrongExpectations = fx.expectations;
  wrongExpectations.gspAppVersion ^= 1u;
  auto rejectedDynamic = BeginBootAttempt(
      fx.manifest, fx.sequence, wrongExpectations, fx.preflight);
  assert(rejectedDynamic.state == BootAttemptState::Rejected);
  assert(!rejectedDynamic.sequencePolicy.valid);

  auto coldFailure = BeginBootAttempt(
      fx.manifest, fx.sequence, fx.expectations, fx.preflight);
  assert(coldFailure.state == BootAttemptState::Ready);
  assert(RecordBootPhaseResult(coldFailure, fx.sequence, 1u, true, true) ==
         BootAttemptEventStatus::WrongPhase);
  assert(coldFailure.nextPhaseIndex == 0u);
  assert(RecordBootPhaseResult(coldFailure, fx.sequence, 0u, true, false) ==
         BootAttemptEventStatus::Ok);
  assert(RecordBootPhaseResult(coldFailure, fx.sequence, 1u, false, false) ==
         BootAttemptEventStatus::Ok);
  assert(coldFailure.state == BootAttemptState::FailedColdReversible);
  assert(!coldFailure.recovery.fullGpuResetRequired);
  assert(coldFailure.recovery.mayRetryBootstrap);
  assert(RecordBootPhaseResult(coldFailure, fx.sequence, 1u, true, true) ==
         BootAttemptEventStatus::NotRunnable);

  auto committedFailure = BeginBootAttempt(
      fx.manifest, fx.sequence, fx.expectations, fx.preflight);
  assert(RecordBootPhaseResult(committedFailure, fx.sequence, 0u, true, false) ==
         BootAttemptEventStatus::Ok);
  assert(RecordBootPhaseResult(committedFailure, fx.sequence, 1u, false, true) ==
         BootAttemptEventStatus::Ok);
  assert(committedFailure.firstResetCommitCrossed);
  assert(committedFailure.state == BootAttemptState::FailedNeedsGpuReset);
  assert(committedFailure.recovery.fullGpuResetRequired);
  assert(committedFailure.recovery.keepDmaAndFlushPinned);

  ResetPostcheckReport recovered{};
  recovered.recovered = true;
  recovered.mayReleasePinnedBootBuffers = true;
  recovered.mayBeginColdRebuild = true;
  recovered.keepPinnedAndRequireReset = false;
  assert(RecordBootResetPostcheck(committedFailure, recovered) ==
         BootAttemptEventStatus::Ok);
  assert(committedFailure.state == BootAttemptState::ResetRecovered);
  assert(BootAttemptTerminal(committedFailure));

  auto unrecoveredFailure = BeginBootAttempt(
      fx.manifest, fx.sequence, fx.expectations, fx.preflight);
  assert(RecordBootPhaseResult(unrecoveredFailure, fx.sequence, 0u, true, false) ==
         BootAttemptEventStatus::Ok);
  assert(RecordBootPhaseResult(unrecoveredFailure, fx.sequence, 1u, false, true) ==
         BootAttemptEventStatus::Ok);
  ResetPostcheckReport stillBad{};
  stillBad.keepPinnedAndRequireReset = true;
  assert(RecordBootResetPostcheck(unrecoveredFailure, stillBad) ==
         BootAttemptEventStatus::Ok);
  assert(unrecoveredFailure.state == BootAttemptState::Unrecovered);

  auto complete = BeginBootAttempt(
      fx.manifest, fx.sequence, fx.expectations, fx.preflight);
  for (std::size_t i = 0; i < fx.sequence.phases.size(); ++i) {
    const bool actions = !fx.sequence.phases[i].actions.empty();
    assert(RecordBootPhaseResult(complete, fx.sequence, i, true, actions) ==
           BootAttemptEventStatus::Ok);
  }
  assert(complete.state == BootAttemptState::Succeeded);
  assert(complete.nextPhaseIndex == fx.sequence.phases.size());
  assert(complete.firstResetCommitCrossed);
  assert(BootAttemptTerminal(complete));

  std::cout << "rtxmac strict GSP boot-orchestrator tests passed\n";
  return 0;
}
