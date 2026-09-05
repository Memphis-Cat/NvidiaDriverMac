#include "rtxmac/vbios.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void S16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
  b[o] = static_cast<std::uint8_t>(v); b[o + 1] = static_cast<std::uint8_t>(v >> 8u);
}
void S32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
  b[o] = static_cast<std::uint8_t>(v); b[o + 1] = static_cast<std::uint8_t>(v >> 8u);
  b[o + 2] = static_cast<std::uint8_t>(v >> 16u); b[o + 3] = static_cast<std::uint8_t>(v >> 24u);
}

std::vector<std::uint8_t> MakeVbios() {
  std::vector<std::uint8_t> b(0x1000u, 0u);
  S16(b, 0x00, 0xAA55u);
  S16(b, 0x18, 0x20u);
  S32(b, 0x20, 0x52494350u); // PCIR
  S16(b, 0x20 + 0x0A, 0x18u);
  S16(b, 0x20 + 0x10, 8u); // 8 * 512 = 4096
  b[0x20 + 0x14] = 0x00u;
  b[0x20 + 0x15] = 0x80u;

  const std::size_t bit = 0x100u;
  S16(b, bit + 0, 0xB8FFu);
  S32(b, bit + 2, 0x00544942u);
  S16(b, bit + 6, 0x0100u);
  b[bit + 8] = 12u;
  b[bit + 9] = 8u;
  b[bit + 10] = 1u;
  b[bit + 11] = 0u;
  std::uint32_t sum = 0u;
  for (std::size_t i = 0; i < 11u; ++i) sum += b[bit + i];
  b[bit + 11] = static_cast<std::uint8_t>(0u - (sum & 0xFFu));

  const std::size_t tok = bit + 12u;
  b[tok + 0] = 0x70u;
  b[tok + 1] = 2u;
  S16(b, tok + 2, 4u);
  S32(b, tok + 4, 0x120u);
  S32(b, 0x120u, 0x200u);

  b[0x200u] = 1u; // table version
  b[0x201u] = 6u;
  b[0x202u] = 6u;
  b[0x203u] = 1u;
  b[0x204u] = 3u;
  b[0x205u] = 44u;
  b[0x206u] = 0x85u;
  b[0x207u] = 0u;
  S32(b, 0x208u, 0x300u);

  const std::uint32_t vdesc = (44u << 16u) | (3u << 8u) | 1u;
  S32(b, 0x300u + 0u, vdesc);
  S32(b, 0x300u + 4u, 0x100u); // stored size
  S32(b, 0x300u + 8u, 0x80u);  // PKC offset
  S32(b, 0x300u + 12u, 0x10u); // interface offset
  S32(b, 0x300u + 16u, 0u);
  S32(b, 0x300u + 20u, 0x80u); // IMEM load
  S32(b, 0x300u + 24u, 0u);
  S32(b, 0x300u + 28u, 0u);
  S32(b, 0x300u + 32u, 0x40u); // DMEM load
  S16(b, 0x300u + 36u, 1u);
  b[0x300u + 38u] = 3u;
  b[0x300u + 39u] = 1u;
  S16(b, 0x300u + 40u, 1u);
  return b;
}

} // namespace

int main() {
  using namespace rtxmac::nvidia::vbios;

  auto image = MakeVbios();
  const auto info = ParseProductionFwsec(image);
  assert(info.status == ParseStatus::Ok);
  assert(info.biosSize == 0x1000u);
  assert(info.expansionRomOffset == 0u);
  assert(info.bitOffset == 0x100u);
  assert(info.falconTableOffset == 0x200u);
  assert(info.descriptorOffset == 0x300u);
  assert(info.descriptorVersion == 3u);
  assert(info.descriptorSize == 44u);
  assert(info.v3.storedSize == 0x100u);
  assert(info.v3.imemLoadSize == 0x80u);
  assert(info.v3.dmemLoadSize == 0x40u);
  assert(info.v3.ucodeId == 3u);

  image[0] = 0u;
  assert(ParseProductionFwsec(image).status == ParseStatus::InvalidRomSignature);

  image = MakeVbios();
  image[0x206u] = 0x45u; // debug, not production
  assert(ParseProductionFwsec(image).status == ParseStatus::FwsecProductionNotFound);

  image = MakeVbios();
  S32(image, 0x300u + 4u, 0xFFFFu);
  assert(ParseProductionFwsec(image).status == ParseStatus::StoredImageOutOfRange);

  std::cout << "rtxmac VBIOS/FWSEC parser tests passed\n";
  return 0;
}
