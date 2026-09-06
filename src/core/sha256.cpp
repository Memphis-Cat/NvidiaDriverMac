#include "rtxmac/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rtxmac {
namespace {
constexpr std::array<std::uint32_t, 64> kK{
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};

constexpr std::uint32_t Ror(std::uint32_t v, unsigned n) noexcept {
  return (v >> n) | (v << (32u - n));
}

void Transform(const std::uint8_t* block, std::array<std::uint32_t, 8>& h) noexcept {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t i=0;i<16;++i) {
    const std::size_t o=i*4u;
    w[i]=(static_cast<std::uint32_t>(block[o])<<24u)|
         (static_cast<std::uint32_t>(block[o+1])<<16u)|
         (static_cast<std::uint32_t>(block[o+2])<<8u)|
         static_cast<std::uint32_t>(block[o+3]);
  }
  for (std::size_t i=16;i<64;++i) {
    const std::uint32_t s0=Ror(w[i-15],7)^Ror(w[i-15],18)^(w[i-15]>>3u);
    const std::uint32_t s1=Ror(w[i-2],17)^Ror(w[i-2],19)^(w[i-2]>>10u);
    w[i]=w[i-16]+s0+w[i-7]+s1;
  }

  std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
  for (std::size_t i=0;i<64;++i) {
    const std::uint32_t s1=Ror(e,6)^Ror(e,11)^Ror(e,25);
    const std::uint32_t ch=(e&f)^((~e)&g);
    const std::uint32_t t1=hh+s1+ch+kK[i]+w[i];
    const std::uint32_t s0=Ror(a,2)^Ror(a,13)^Ror(a,22);
    const std::uint32_t maj=(a&b)^(a&c)^(b&c);
    const std::uint32_t t2=s0+maj;
    hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
  }
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}
} // namespace

Sha256Digest Sha256(std::span<const std::uint8_t> data) noexcept {
  std::array<std::uint32_t,8> h{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                                0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
  std::size_t off=0;
  while (data.size()-off>=64u) { Transform(data.data()+off,h); off+=64u; }

  std::array<std::uint8_t,128> tail{};
  const std::size_t rem=data.size()-off;
  for(std::size_t i=0;i<rem;++i) tail[i]=data[off+i];
  tail[rem]=0x80u;
  const std::size_t totalTail=rem<56u?64u:128u;
  const std::uint64_t bitLength=static_cast<std::uint64_t>(data.size())*8ull;
  for(std::size_t i=0;i<8;++i)
    tail[totalTail-1u-i]=static_cast<std::uint8_t>(bitLength>>(i*8u));
  Transform(tail.data(),h);
  if(totalTail==128u) Transform(tail.data()+64u,h);

  Sha256Digest out{};
  for(std::size_t i=0;i<h.size();++i) {
    out[i*4u+0]=static_cast<std::uint8_t>(h[i]>>24u);
    out[i*4u+1]=static_cast<std::uint8_t>(h[i]>>16u);
    out[i*4u+2]=static_cast<std::uint8_t>(h[i]>>8u);
    out[i*4u+3]=static_cast<std::uint8_t>(h[i]);
  }
  return out;
}

bool Sha256Equal(const Sha256Digest& a,const Sha256Digest& b) noexcept {
  std::uint8_t diff=0u;
  for(std::size_t i=0;i<a.size();++i) diff|=static_cast<std::uint8_t>(a[i]^b[i]);
  return diff==0u;
}

} // namespace rtxmac
