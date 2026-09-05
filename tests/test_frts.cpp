#include "rtxmac/frts.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void S32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
  b[o + 0] = static_cast<std::uint8_t>(v >> 0u);
  b[o + 1] = static_cast<std::uint8_t>(v >> 8u);
  b[o + 2] = static_cast<std::uint8_t>(v >> 16u);
  b[o + 3] = static_cast<std::uint8_t>(v >> 24u);
}

std::uint32_t L32(const auto& b, std::size_t o) {
  return static_cast<std::uint32_t>(b[o + 0]) |
      (static_cast<std::uint32_t>(b[o + 1]) << 8u) |
      (static_cast<std::uint32_t>(b[o + 2]) << 16u) |
      (static_cast<std::uint32_t>(b[o + 3]) << 24u);
}

} // namespace

int main() {
  using namespace rtxmac::nvidia;
  using namespace rtxmac::nvidia::frts;

  const auto cmd = BuildFrtsCommand(8ull << 30u);
  assert(cmd.has_value());
  assert(cmd->regionOffsetBytes == 0x1FFE00000ull);
  assert(cmd->regionOffset4K == 0x1FFE00u);
  assert(L32(cmd->bytes, 0u) == 1u);
  assert(L32(cmd->bytes, 4u) == 24u);
  assert(L32(cmd->bytes, 20u) == 2u);
  assert(L32(cmd->bytes, 24u) == 1u);
  assert(L32(cmd->bytes, 28u) == 20u);
  assert(L32(cmd->bytes, 32u) == 0x1FFE00u);
  assert(L32(cmd->bytes, 36u) == 0x100u);
  assert(L32(cmd->bytes, 40u) == 2u);
  assert(!BuildFrtsCommand(0x100000ull).has_value());

  vbios::DescriptorV3 desc{};
  desc.storedSize = 0x600u;
  desc.pkcDataOffset = 0x300u; // destination -> 0x400, separate from command buffer
  desc.interfaceOffset = 0x20u;
  desc.imemLoadSize = 0x100u;

  std::vector<std::uint8_t> image(0x600u, 0u);
  // Application interface header at IMEMLoadSize + InterfaceOffset = 0x120.
  image[0x120u + 0] = 1u;
  image[0x120u + 1] = 4u;
  image[0x120u + 2] = 8u;
  image[0x120u + 3] = 1u;
  S32(image, 0x124u + 0, 4u);    // DMEMMAPPER entry
  S32(image, 0x124u + 4, 0x80u); // dmem offset

  // Mapper at IMEMLoadSize + 0x80 = 0x180.
  S32(image, 0x180u + 8, 0x140u); // command buffer rel -> 0x240
  S32(image, 0x180u + 12, 0x80u); // command buffer capacity

  std::vector<std::uint8_t> signature(0x200u, 0u);
  for (std::size_t i = 0; i < signature.size(); ++i) signature[i] = static_cast<std::uint8_t>(i & 0xFFu);

  const auto plan = PlanFwsecFrtsPatch(desc, image, signature.size());
  assert(plan.status == PatchStatus::Ok);
  assert(plan.interfaceHeaderOffset == 0x120u);
  assert(plan.dmemMapperOffset == 0x180u);
  assert(plan.initCommandFieldOffset == 0x1ACu);
  assert(plan.commandBufferOffset == 0x240u);
  assert(plan.signatureDestinationOffset == 0x400u);

  const auto patched = ApplyFwsecFrtsPatch(image, plan, *cmd, signature);
  assert(patched.has_value());
  assert(L32(*patched, 0x1ACu) == 0x15u);
  for (std::size_t i = 0; i < cmd->bytes.size(); ++i) assert((*patched)[0x240u + i] == cmd->bytes[i]);
  for (std::size_t i = 0; i < 0x180u; ++i) assert((*patched)[0x400u + i] == signature[0x80u + i]);

  auto noMapper = image;
  S32(noMapper, 0x124u, 3u);
  assert(PlanFwsecFrtsPatch(desc, noMapper, signature.size()).status == PatchStatus::DmemMapperNotFound);
  assert(PlanFwsecFrtsPatch(desc, image, 0x100u).status == PatchStatus::SignatureSourceTooSmall);

  std::cout << "rtxmac offline FRTS planning tests passed\n";
  return 0;
}
