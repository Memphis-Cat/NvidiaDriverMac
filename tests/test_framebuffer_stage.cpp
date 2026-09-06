#include "rtxmac/gsp_manifest.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  using namespace rtxmac::nvidia;
  using namespace rtxmac::nvidia::gsp;

  FramebufferStageImage plan{};
  plan.kind = AllocationKind::FrtsFwsecImage;
  plan.vramOffset = 0x1F0000000ull;
  plan.logicalBytes = 0x1003ull;
  plan.allocationBytes = 0x2000ull;
  plan.pramin = PlanPraminStage(plan.vramOffset, plan.allocationBytes, 0x200000000ull);
  assert(plan.pramin.valid);

  std::vector<std::uint8_t> firmware(static_cast<std::size_t>(plan.logicalBytes), 0x5Au);
  firmware.front() = 0x11u;
  firmware.back() = 0xA5u;

  const auto artifact = BuildFramebufferStageArtifact(plan, firmware);
  assert(artifact.has_value());
  assert(artifact->kind == plan.kind);
  assert(artifact->vramOffset == plan.vramOffset);
  assert(artifact->logicalBytes == plan.logicalBytes);
  assert(artifact->allocationBytes == plan.allocationBytes);
  assert(artifact->bytes.size() == 0x2000u);
  assert(artifact->bytes.front() == 0x11u);
  assert(artifact->bytes[0x1002u] == 0xA5u);
  for (std::size_t i = 0x1003u; i < artifact->bytes.size(); ++i) {
    assert(artifact->bytes[i] == 0u);
  }

  auto shortFirmware = firmware;
  shortFirmware.pop_back();
  assert(!BuildFramebufferStageArtifact(plan, shortFirmware).has_value());

  auto badBytes = plan;
  badBytes.allocationBytes = 0x1003ull;
  assert(!BuildFramebufferStageArtifact(badBytes, firmware).has_value());

  auto badPramin = plan;
  badPramin.pramin.totalBytes = 0x1000ull;
  assert(!BuildFramebufferStageArtifact(badPramin, firmware).has_value());

  auto unaligned = plan;
  unaligned.vramOffset += 1u;
  assert(!BuildFramebufferStageArtifact(unaligned, firmware).has_value());

  std::cout << "rtxmac framebuffer staging artifact tests passed\n";
  return 0;
}
