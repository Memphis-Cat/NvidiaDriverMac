#include "rtxmac/gsp_bootstrap.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {
std::uint32_t L32(const auto& v, std::size_t o) {
  return static_cast<std::uint32_t>(v[o]) | (static_cast<std::uint32_t>(v[o+1])<<8u) |
      (static_cast<std::uint32_t>(v[o+2])<<16u) | (static_cast<std::uint32_t>(v[o+3])<<24u);
}
std::uint64_t L64(const auto& v, std::size_t o) {
  std::uint64_t x=0; for(std::size_t i=0;i<8;++i)x|=static_cast<std::uint64_t>(v[o+i])<<(i*8u); return x;
}
}

int main() {
  using namespace rtxmac::nvidia::gsp;
  const auto bdf = EncodePciBdf(1, 0, 0);
  assert(bdf.has_value() && *bdf == 0x100u);
  assert(!EncodePciBdf(0, 32, 0).has_value());

  GspSystemInfoInputs s{
    .bar0Physical = 0x800000000ull,
    .bar1Physical = 0x900000000ull,
    .bar3Physical = 0xA00000000ull,
    .domainBusDeviceFunction = *bdf,
    .pciDeviceIdDword = 0x248910DEu,
    .pciSubDeviceIdDword = 0x123410DEu,
    .pciRevisionId = 0xA1u,
  };
  const auto sys = BuildGspSystemInfo570(s);
  static_assert(sys.size() == 928u);
  assert(L64(sys,0)==s.bar0Physical && L64(sys,8)==s.bar1Physical && L64(sys,16)==s.bar3Physical);
  assert(L64(sys,32)==0x100u && L64(sys,72)==0x7ffffffff000ull);
  assert(L32(sys,80)==0x88000u && L32(sys,84)==0x1000u);
  assert(L32(sys,88)==0x248910DEu && L32(sys,92)==0x123410DEu && L32(sys,96)==0xA1u);
  assert(sys[840]==1u);

  const auto defaults = DefaultBootstrapRegistry();
  const auto reg = BuildRegistryTable(defaults);
  assert(reg.has_value());
  assert(L32(*reg,0)==reg->size() && L32(*reg,4)==2u);
  assert(L32(*reg,8)==40u); // header 8 + two 16-byte entries
  assert((*reg)[12]==1u && L32(*reg,16)==1u && L32(*reg,20)==4u);
  assert(L32(*reg,24)==40u + 22u); // first name incl NUL = 22 bytes

  const auto layout = PlanQueueMemory();
  assert(layout.has_value());
  const auto q = BuildBootstrapCommandQueue(*layout, s, defaults);
  assert(q.has_value());
  assert(q->bytes.size()==0x40000u);
  assert(L32(q->bytes,0)==0u && L32(q->bytes,4)==0x40000u);
  assert(L32(q->bytes,8)==0x1000u && L32(q->bytes,12)==63u);
  assert(L32(q->bytes,16)==2u && L32(q->bytes,20)==1u);
  assert(L32(q->bytes,24)==32u && L32(q->bytes,28)==0x1000u);
  assert(q->records[0].function==72u && q->records[0].sequence==0u);
  assert(q->records[1].function==73u && q->records[1].sequence==1u);
  assert(ValidateRpcRecord(q->records[0].bytes,0x1000u));
  assert(ValidateRpcRecord(q->records[1].bytes,0x1000u));
  assert(q->bytes[0x1000u] == q->records[0].bytes[0]);
  assert(q->bytes[0x2000u] == q->records[1].bytes[0]);

  std::cout << "rtxmac GSP bootstrap queue tests passed\n";
}
