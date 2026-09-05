#include "rtxmac/nvfw.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void Store32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
  b[o + 0] = static_cast<std::uint8_t>(v >> 0u);
  b[o + 1] = static_cast<std::uint8_t>(v >> 8u);
  b[o + 2] = static_cast<std::uint8_t>(v >> 16u);
  b[o + 3] = static_cast<std::uint8_t>(v >> 24u);
}

std::vector<std::uint8_t> MakeValidImage() {
  std::vector<std::uint8_t> b(256u, 0u);

  // nvfw_bin_hdr (24 bytes)
  Store32(b, 0, 0x10DEu);
  Store32(b, 4, 2u);
  Store32(b, 8, 256u);
  Store32(b, 12, 24u);   // HS header
  Store32(b, 16, 128u);  // firmware data
  Store32(b, 20, 128u);

  // nvfw_hs_header_v2 (36 bytes) @ 24
  Store32(b, 24 + 0, 108u); // signatures
  Store32(b, 24 + 4, 16u);
  Store32(b, 24 + 8, 100u); // patch_loc contains a u32
  Store32(b, 24 + 12, 104u); // patch_sig contains a u32
  Store32(b, 24 + 16, 0u);
  Store32(b, 24 + 20, 0u);
  Store32(b, 24 + 24, 1u);
  Store32(b, 24 + 28, 64u); // load header
  Store32(b, 24 + 32, 36u);

  // nvfw_hs_load_header_v2 @ 64
  Store32(b, 64 + 0, 0u);   // os code
  Store32(b, 64 + 4, 16u);
  Store32(b, 64 + 8, 32u);  // os data
  Store32(b, 64 + 12, 16u);
  Store32(b, 64 + 16, 1u);  // one app

  // first app @ 84
  Store32(b, 84 + 0, 48u);
  Store32(b, 84 + 4, 32u);
  Store32(b, 84 + 8, 80u);
  Store32(b, 84 + 12, 16u);
  return b;
}

} // namespace

int main() {
  using namespace rtxmac::nvidia::fw;

  auto image = MakeValidImage();
  const auto parsed = ParseBooterImage(image);
  assert(parsed.status == ParseStatus::Ok);
  assert(parsed.bin.binSize == 256u);
  assert(parsed.bin.headerOffset == 24u);
  assert(parsed.bin.dataOffset == 128u);
  assert(parsed.load.numApps == 1u);
  assert(parsed.firstApp.offset == 48u);
  assert(parsed.firstApp.size == 32u);

  image[8] = 0xFFu;
  image[9] = 0xFFu;
  image[10] = 0xFFu;
  image[11] = 0x7Fu;
  assert(ParseBooterImage(image).status == ParseStatus::BinSizeOutOfRange);

  image = MakeValidImage();
  Store32(image, 64 + 16, 0u);
  assert(ParseBooterImage(image).status == ParseStatus::NoApplications);

  image = MakeValidImage();
  Store32(image, 84 + 0, 120u);
  Store32(image, 84 + 4, 32u);
  assert(ParseBooterImage(image).status == ParseStatus::AppOutOfRange);

  std::cout << "rtxmac NVIDIA firmware parser tests passed\n";
  return 0;
}
