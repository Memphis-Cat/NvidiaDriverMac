#include "rtxmac/gsp_rpc.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

std::uint32_t LoadLe32(const std::vector<std::uint8_t>& v, std::size_t off) {
  return static_cast<std::uint32_t>(v[off + 0]) |
      (static_cast<std::uint32_t>(v[off + 1]) << 8u) |
      (static_cast<std::uint32_t>(v[off + 2]) << 16u) |
      (static_cast<std::uint32_t>(v[off + 3]) << 24u);
}

} // namespace

int main() {
  using namespace rtxmac::nvidia::gsp;

  const std::array<std::uint8_t, 5> payload{{1u, 2u, 3u, 4u, 5u}};
  const auto built = BuildRpcRecord(0x12345678u, payload, 7u, 0x100u);
  assert(built.has_value());
  assert(built->bytes.size() == 0x100u);
  assert(built->elementCount == 1u);
  assert(built->checksum == 0x56670609u); // deterministic known vector
  assert(LoadLe32(built->bytes, 32u) == 0x56670609u);
  assert(LoadLe32(built->bytes, 36u) == 7u);
  assert(LoadLe32(built->bytes, 40u) == 1u);
  assert(LoadLe32(built->bytes, 48u) == kRpcHeaderVersion3);
  assert(LoadLe32(built->bytes, 52u) == kRpcSignatureValid);
  assert(LoadLe32(built->bytes, 56u) == 37u);
  assert(LoadLe32(built->bytes, 60u) == 0x12345678u);
  assert(ValidateRpcRecord(built->bytes, 0x100u));

  auto corrupt = built->bytes;
  corrupt[80] ^= 0x80u;
  assert(!ValidateRpcRecord(corrupt, 0x100u));

  // 0x100-byte queue elements with a 16-element ceiling can carry 4016
  // payload bytes per RPC record (4096 total - 80 bytes protocol overhead).
  const auto fragments = PlanRpcFragments(9000u, 0x100u, 16u);
  assert(fragments.size() == 3u);
  assert(fragments[0].payloadOffset == 0u && fragments[0].payloadBytes == 4016u && !fragments[0].continuation);
  assert(fragments[1].payloadOffset == 4016u && fragments[1].payloadBytes == 4016u && fragments[1].continuation);
  assert(fragments[2].payloadOffset == 8032u && fragments[2].payloadBytes == 968u && fragments[2].continuation);

  std::vector<std::uint8_t> tooLarge(4017u, 0xAAu);
  assert(!BuildRpcRecord(1u, tooLarge, 0u, 0x100u, 16u).has_value());

  std::cout << "rtxmac GSP RPC transport tests passed\n";
  return 0;
}
