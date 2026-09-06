#include "rtxmac/sha256.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
std::uint8_t Hex(char c) {
  if(c>='0'&&c<='9') return static_cast<std::uint8_t>(c-'0');
  if(c>='a'&&c<='f') return static_cast<std::uint8_t>(10+c-'a');
  return static_cast<std::uint8_t>(10+c-'A');
}
rtxmac::Sha256Digest Digest(std::string_view hex) {
  assert(hex.size()==64u); rtxmac::Sha256Digest out{};
  for(std::size_t i=0;i<out.size();++i) out[i]=static_cast<std::uint8_t>((Hex(hex[i*2])<<4u)|Hex(hex[i*2+1]));
  return out;
}
}

int main(){
  const std::vector<std::uint8_t> empty{};
  assert(rtxmac::Sha256Equal(rtxmac::Sha256(empty),Digest("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")));
  const std::string_view abc="abc";
  const std::span<const std::uint8_t> abcBytes(reinterpret_cast<const std::uint8_t*>(abc.data()),abc.size());
  assert(rtxmac::Sha256Equal(rtxmac::Sha256(abcBytes),Digest("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
  std::vector<std::uint8_t> longMsg(1000u,static_cast<std::uint8_t>('a'));
  assert(rtxmac::Sha256Equal(rtxmac::Sha256(longMsg),Digest("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3")));
  auto changed=rtxmac::Sha256(longMsg); changed[0]^=1u;
  assert(!rtxmac::Sha256Equal(rtxmac::Sha256(longMsg),changed));
  std::cout<<"rtxmac SHA-256 tests passed\n";
}
