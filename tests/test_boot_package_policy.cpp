#include "rtxmac/boot_package_policy.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
void Put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
  b[o]=static_cast<std::uint8_t>(v); b[o+1]=static_cast<std::uint8_t>(v>>8u);
}
void Put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
  for (std::size_t i=0;i<4u;++i) b[o+i]=static_cast<std::uint8_t>(v>>(i*8u));
}
void Put64(std::vector<std::uint8_t>& b, std::size_t o, std::uint64_t v) {
  for (std::size_t i=0;i<8u;++i) b[o+i]=static_cast<std::uint8_t>(v>>(i*8u));
}

std::vector<std::uint8_t> MakeVbios() {
  std::vector<std::uint8_t> b(0x2000u,0u);
  Put16(b,0x00,0xAA55u); Put16(b,0x18,0x20u);
  Put32(b,0x20,0x52494350u); Put16(b,0x2a,0x18u); Put16(b,0x30,16u);
  b[0x34]=0u; b[0x35]=0x80u;
  const std::size_t bit=0x100u;
  Put16(b,bit,0xB8FFu); Put32(b,bit+2,0x00544942u); Put16(b,bit+6,0x0100u);
  b[bit+8]=12u; b[bit+9]=8u; b[bit+10]=1u;
  std::uint32_t sum=0u; for(std::size_t i=0;i<11u;++i) sum+=b[bit+i];
  b[bit+11]=static_cast<std::uint8_t>(0u-(sum&0xffu));
  const std::size_t tok=bit+12u; b[tok]=0x70u; b[tok+1]=2u; Put16(b,tok+2,4u); Put32(b,tok+4,0x120u);
  Put32(b,0x120u,0x200u);
  b[0x200]=1u; b[0x201]=6u; b[0x202]=6u; b[0x203]=1u; b[0x204]=3u; b[0x205]=44u;
  b[0x206]=0x85u; Put32(b,0x208,0x300u);
  constexpr std::uint32_t descSize=44u+0x200u;
  Put32(b,0x300u,(descSize<<16u)|(3u<<8u)|1u);
  Put32(b,0x304u,0x600u); Put32(b,0x308u,0x300u); Put32(b,0x30cu,0x20u);
  Put32(b,0x310u,0x10u); Put32(b,0x314u,0x100u); Put32(b,0x318u,0x20u);
  Put32(b,0x31cu,0x30u); Put32(b,0x320u,0x100u); Put16(b,0x324u,1u); b[0x326u]=5u; b[0x327u]=1u; Put16(b,0x328u,1u);
  for(std::size_t i=0;i<0x200u;++i) b[0x32cu+i]=static_cast<std::uint8_t>((0x80u+i)&0xffu);
  const std::size_t image=0x300u+descSize;
  b[image+0x120u]=1u; b[image+0x121u]=4u; b[image+0x122u]=8u; b[image+0x123u]=1u;
  Put32(b,image+0x124u,4u); Put32(b,image+0x128u,0x80u);
  Put32(b,image+0x188u,0x140u); Put32(b,image+0x18cu,0x80u);
  return b;
}

std::vector<std::uint8_t> MakeElf(std::size_t sigBytes=0x1000u) {
  const std::size_t total=0x4000u+sigBytes;
  std::vector<std::uint8_t> e(total,0u);
  e[0]=0x7f; e[1]='E'; e[2]='L'; e[3]='F'; e[4]=2u; e[5]=1u;
  Put64(e,0x28,0x100u); Put16(e,0x3a,64u); Put16(e,0x3c,4u); Put16(e,0x3e,1u);
  const char names[]="\0.shstrtab\0.fwimage\0.fwsignature_ga10x\0";
  for(std::size_t i=0;i<sizeof(names);++i) e[0x240u+i]=static_cast<std::uint8_t>(names[i]);
  Put32(e,0x140u,1u); Put32(e,0x144u,3u); Put64(e,0x158u,0x240u); Put64(e,0x160u,sizeof(names)); Put64(e,0x170u,1u);
  Put32(e,0x180u,11u); Put32(e,0x184u,1u); Put64(e,0x198u,0x1000u); Put64(e,0x1a0u,0x2000u); Put64(e,0x1b0u,0x1000u);
  Put32(e,0x1c0u,20u); Put32(e,0x1c4u,1u); Put64(e,0x1d8u,0x4000u); Put64(e,0x1e0u,sigBytes); Put64(e,0x1f0u,0x1000u);
  for(std::size_t i=0;i<0x2000u;++i) e[0x1000u+i]=static_cast<std::uint8_t>((i+7u)&0xffu);
  for(std::size_t i=0;i<sigBytes;++i) e[0x4000u+i]=static_cast<std::uint8_t>((i+11u)&0xffu);
  return e;
}

std::vector<std::uint8_t> MakeGspBoot() {
  std::vector<std::uint8_t> b(0x400u,0u);
  Put32(b,0,0x10deu); Put32(b,4,1u); Put32(b,8,0x400u); Put32(b,12,0x40u); Put32(b,16,0xa0u); Put32(b,20,0x200u);
  Put32(b,0x40u,5u); Put32(b,0x44u,0u); Put32(b,0x48u,0x20u); Put32(b,0x4cu,0x20u); Put32(b,0x50u,0x10u);
  Put32(b,0x54u,0x30u); Put32(b,0x58u,0x20u); Put32(b,0x5cu,0x55667788u); Put32(b,0x60u,0x50u); Put32(b,0x64u,0x10u);
  Put32(b,0x68u,0x60u); Put32(b,0x6cu,0x20u); Put32(b,0x70u,0x80u); Put32(b,0x74u,0x30u);
  Put32(b,0x7cu,0xb0u); Put32(b,0x80u,0x10u); Put32(b,0x84u,0xc0u); Put32(b,0x88u,0x10u);
  Put32(b,0x8cu,0x180u); Put32(b,0x90u,1u); Put32(b,0x94u,1u); Put32(b,0x98u,1u);
  return b;
}

std::vector<std::uint8_t> MakeSec2() {
  std::vector<std::uint8_t> b(0x400u,0u);
  Put32(b,0,0x10deu); Put32(b,4,1u); Put32(b,8,0x400u); Put32(b,12,0x40u); Put32(b,16,0x180u); Put32(b,20,0x100u);
  Put32(b,0x40u,0x100u); Put32(b,0x44u,0x40u); Put32(b,0x48u,0x30u); Put32(b,0x4cu,0x34u); Put32(b,0x58u,0x38u); Put32(b,0x5cu,0x70u); Put32(b,0x60u,0x20u);
  Put32(b,0x30u,0x20u); Put32(b,0x34u,0x20u); Put32(b,0x38u,2u);
  Put32(b,0x70u,0u); Put32(b,0x74u,0x20u); Put32(b,0x78u,0x40u); Put32(b,0x7cu,0x20u); Put32(b,0x80u,1u);
  Put32(b,0x84u,0u); Put32(b,0x88u,0x20u); Put32(b,0x8cu,0x20u); Put32(b,0x90u,0x10u);
  for(std::size_t i=0;i<0x40u;++i) b[0x100u+i]=static_cast<std::uint8_t>(0xa0u+i);
  return b;
}

rtxmac::nvidia::package::BuildResult Build(std::size_t sigBytes=0x1000u) {
  const auto vb=MakeVbios(); const auto el=MakeElf(sigBytes); const auto gb=MakeGspBoot(); const auto s=MakeSec2();
  return rtxmac::nvidia::package::BuildGa10xPackage({
      .pci={.vendor=0x10deu,.device=0x2489u,.subsystemVendor=0x1462u,.subsystemDevice=0x3970u},
      .vramBytes=8ull<<30u,.vbios=vb,.gspElf=el,.gspBootloaderContainer=gb,.sec2BooterContainer=s});
}
}

int main() {
  using namespace rtxmac::nvidia::package;
  const auto built=Build(); assert(built.status==BuildStatus::Ok);
  auto view=ParseAndVerify(built.bytes); assert(view.status==ParseStatus::Ok);
  auto good=CheckGa10xPackageSemantics(built.bytes,view); assert(good.valid && good.failure==SemanticFailure::None);

  const auto wrongSig=Build(0x800u); assert(wrongSig.status==BuildStatus::Ok);
  auto wrongSigView=ParseAndVerify(wrongSig.bytes); assert(wrongSigView.status==ParseStatus::Ok);
  assert(CheckGa10xPackageSemantics(wrongSig.bytes,wrongSigView).failure==SemanticFailure::GspSignatureWrongSize);

  auto badBootView=view; badBootView.metadata.gspMonitorCodeOffset=0x200u;
  assert(CheckGa10xPackageSemantics(built.bytes,badBootView).failure==SemanticFailure::GspBootloaderMetadataOutOfRange);
  auto badFwsec=view; badFwsec.metadata.fwsecDmemLoadSize=0xffffu;
  assert(CheckGa10xPackageSemantics(built.bytes,badFwsec).failure==SemanticFailure::FwsecMetadataOutOfRange);
  auto badSec2=view; badSec2.metadata.sec2CodeSize=0xffffu;
  assert(CheckGa10xPackageSemantics(built.bytes,badSec2).failure==SemanticFailure::Sec2MetadataOutOfRange);

  PackageView unverified=view; unverified.status=ParseStatus::HashMismatch;
  assert(CheckGa10xPackageSemantics(built.bytes,unverified).failure==SemanticFailure::PackageNotVerified);
  std::cout<<"rtxmac GA10x boot-package semantic policy tests passed\n";
}
