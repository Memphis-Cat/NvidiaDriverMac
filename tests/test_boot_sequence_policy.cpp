#include "rtxmac/boot_sequence_policy.hpp"

#include <cassert>
#include <iostream>
#include <utility>

int main() {
  using namespace rtxmac::nvidia;
  using namespace rtxmac::nvidia::gsp;

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
  const auto manifest = PlanBootManifest(in);
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
  gspBootloader.descriptor.appVersion = 0xA1B2C3D4u;

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

  const auto sequence = PlanBootSequence(
      manifest, addresses, gspBootloader, fwsec, sec2, 0x174u);
  assert(sequence.valid);
  const BootSequencePolicyExpectations expectations{
      .libosInitArguments = addresses.libosInitArguments,
      .gspAppVersion = gspBootloader.descriptor.appVersion,
  };
  const auto report = CheckGa102BootSequencePolicy(manifest, sequence, expectations);
  assert(report.valid);
  assert(report.failure == BootSequencePolicyFailure::None);
  assert(report.checkCount == 4u);
  assert(report.actionCount > 0u);

  // A register-safe but semantically wrong appVersion is rejected.
  auto wrongAppVersion = sequence;
  wrongAppVersion.phases[9].actions[0].value ^= 1u;
  const auto wrongAppReport = CheckGa102BootSequencePolicy(
      manifest, wrongAppVersion, expectations);
  assert(!wrongAppReport.valid);
  assert(wrongAppReport.failure == BootSequencePolicyFailure::UnexpectedAction);
  assert(wrongAppReport.phaseIndex == 9u);

  // The two libOS mailbox words must encode the exact independently supplied
  // pointer, not merely target allowed mailbox registers.
  auto wrongMailbox = sequence;
  wrongMailbox.phases[5].actions[0].value += 0x1000u;
  const auto wrongMailboxReport = CheckGa102BootSequencePolicy(
      manifest, wrongMailbox, expectations);
  assert(!wrongMailboxReport.valid);
  assert(wrongMailboxReport.failure == BootSequencePolicyFailure::UnexpectedAction);
  assert(wrongMailboxReport.phaseIndex == 5u);

  // Generic Falcon phases still use the independent static address/mask policy.
  auto deniedFalcon = sequence;
  deniedFalcon.phases[1].actions.insert(
      deniedFalcon.phases[1].actions.begin(),
      {falcon::ActionKind::Write32, 0x00110500u, 1u, 0xFFFFFFFFu, 0u});
  const auto deniedReport = CheckGa102BootSequencePolicy(
      manifest, deniedFalcon, expectations);
  assert(!deniedReport.valid);
  assert(deniedReport.failure == BootSequencePolicyFailure::FalconActionDenied);
  assert(deniedReport.phaseIndex == 1u);

  // Check-only phases cannot smuggle in even an otherwise-allowed write.
  auto unexpectedWrite = sequence;
  unexpectedWrite.phases[3].actions.push_back(
      {falcon::ActionKind::Write32, 0x00110040u, 0u, 0xFFFFFFFFu, 0u});
  const auto unexpectedWriteReport = CheckGa102BootSequencePolicy(
      manifest, unexpectedWrite, expectations);
  assert(!unexpectedWriteReport.valid);
  assert(unexpectedWriteReport.failure == BootSequencePolicyFailure::UnexpectedAction);
  assert(unexpectedWriteReport.phaseIndex == 3u);

  auto badCheck = sequence;
  badCheck.phases[3].checks[0].addressOrOffset ^= 4u;
  const auto badCheckReport = CheckGa102BootSequencePolicy(
      manifest, badCheck, expectations);
  assert(!badCheckReport.valid);
  assert(badCheckReport.failure == BootSequencePolicyFailure::UnexpectedCheck);
  assert(badCheckReport.phaseIndex == 3u);

  auto wrongOrder = sequence;
  std::swap(wrongOrder.phases[4], wrongOrder.phases[5]);
  const auto wrongOrderReport = CheckGa102BootSequencePolicy(
      manifest, wrongOrder, expectations);
  assert(!wrongOrderReport.valid);
  assert(wrongOrderReport.failure == BootSequencePolicyFailure::WrongPhaseOrder);

  auto missingStatusCheck = sequence;
  missingStatusCheck.phases[11].checks.clear();
  const auto missingReport = CheckGa102BootSequencePolicy(
      manifest, missingStatusCheck, expectations);
  assert(!missingReport.valid);
  assert(missingReport.failure == BootSequencePolicyFailure::MissingCheck);

  auto badExpectations = expectations;
  badExpectations.libosInitArguments += 1u;
  const auto badExpectationReport = CheckGa102BootSequencePolicy(
      manifest, sequence, badExpectations);
  assert(!badExpectationReport.valid);
  assert(badExpectationReport.failure == BootSequencePolicyFailure::InvalidExpectations);

  std::cout << "rtxmac exact full GA102 boot-sequence policy tests passed\n";
  return 0;
}
