#include "rtxmac/boot_package.hpp"

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
std::uint32_t Get32(std::span<const std::uint8_t> b, std::size_t o) {
  return static_cast<std::uint32_t>(b[o]) |
      static_cast<std::uint32_t>(b[o+1])<<8u |
      static_cast<std::uint32_t>(b[o+2])<<16u |
      static_cast<std::uint32_t>(b[o+3])<<24u;
}

std::vector<std::uint8_t> MakeVbios() {
  std::vector<std::uint8_t> b(0x2000u,0u);
  Put16(b,0x00,0xAA55u); Put16(b,0x18,0x20u);
  Put32(b,0x20,0x52494350u); Put16(b,0x20+0x0A,0x18u); Put16(b,0x20+0x10,16u);
  b[0x20+0x14]=0u; b[0x20+0x15]=0x80u;

  const std::size_t bit=0x100u;
  Put16(b,bit,0xB8FFu); Put32(b,bit+2,0x00544942u); Put16(b,bit+6,0x0100u);
  b[bit+8]=12u; b[bit+9]=8u; b[bit+10]=1u;
  std::uint32_t sum=0u; for(std::size_t i=0;i<11u;++i) sum+=b[bit+i];
  b[bit+11]=static_cast<std::uint8_t>(0u-(sum&0xffu));
  const std::size_t tok=bit+12u;
  b[tok]=0x70u; b[tok+1]=2u; Put16(b,tok+2,4u); Put32(b,tok+4,0x120u); Put32(b,0x120u,0x200u);
  b[0x200]=1u; b[0x201]=6u; b[0x202]=6u; b[0x203]=1u; b[0x204]=3u; b[0x205]=44u;
  b[0x206]=0x85u; Put32(b,0x208,0x300u);

  constexpr std::uint32_t descriptorSize=44u+0x200u;
  Put32(b,0x300u,(descriptorSize<<16u)|(3u<<8u)|1u);
  Put32(b,0x304u,0x600u); Put32(b,0x308u,0x300u); Put32(b,0x30cu,0x20u);
  Put32(b,0x310u,0x10u); Put32(b,0x314u,0x100u); Put32(b,0x318u,0x20u);
  Put32(b,0x31cu,0x30u); Put32(b,0x320u,0x100u); Put16(b,0x324u,1u); b[0x326u]=5u;
  b[0x327u]=1u; Put16(b,0x328u,1u);
  for(std::size_t i=0;i<0x200u;++i) b[0x300u+44u+i]=static_cast<std::uint8_t>((0x80u+i)&0xffu);

  const std::size_t image=0x300u+descriptorSize;
  b[image+0x120u]=1u; b[image+0x121u]=4u; b[image+0x122u]=8u; b[image+0x123u]=1u;
  Put32(b,image+0x124u,4u); Put32(b,image+0x128u,0x80u);
  Put32(b,image+0x188u,0x140u); Put32(b,image+0x18cu,0x80u);
  return b;
}

std::vector<std::uint8_t> MakeGspElf() {
  std::vector<std::uint8_t> elf(0x5000u,0u);
  elf[0]=0x7f; elf[1]='E'; elf[2]='L'; elf[3]='F'; elf[4]=2u; elf[5]=1u;
  Put64(elf,0x28,0x100u); Put16(elf,0x3a,64u); Put16(elf,0x3c,4u); Put16(elf,0x3e,1u);
  const char names[]="\0.shstrtab\0.fwimage\0.fwsignature_ga10x\0";
  for(std::size_t i=0;i<sizeof(names);++i) elf[0x240u+i]=static_cast<std::uint8_t>(names[i]);
  Put32(elf,0x140u,1u); Put32(elf,0x144u,3u); Put64(elf,0x158u,0x240u); Put64(elf,0x160u,sizeof(names)); Put64(elf,0x170u,1u);
  Put32(elf,0x180u,11u); Put32(elf,0x184u,1u); Put64(elf,0x198u,0x1000u); Put64(elf,0x1a0u,0x2500u); Put64(elf,0x1b0u,0x1000u);
  Put32(elf,0x1c0u,20u); Put32(elf,0x1c4u,1u); Put64(elf,0x1d8u,0x4000u); Put64(elf,0x1e0u,0x1000u); Put64(elf,0x1f0u,0x1000u);
  for(std::size_t i=0;i<0x2500u;++i) elf[0x1000u+i]=static_cast<std::uint8_t>((i*3u+7u)&0xffu);
  for(std::size_t i=0;i<0x1000u;++i) elf[0x4000u+i]=static_cast<std::uint8_t>((i*5u+11u)&0xffu);
  return elf;
}

std::vector<std::uint8_t> MakeGspBootloader() {
  std::vector<std::uint8_t> b(0x400u,0u);
  Put32(b,0,0x10deu); Put32(b,4,1u); Put32(b,8,0x400u); Put32(b,12,0x40u); Put32(b,16,0xa0u); Put32(b,20,0x200u);
  Put32(b,0x40u,5u); Put32(b,0x44u,0u); Put32(b,0x48u,0x20u);
  Put32(b,0x4cu,0x20u); Put32(b,0x50u,0x10u); Put32(b,0x54u,0x30u); Put32(b,0x58u,0x20u);
  Put32(b,0x5cu,0x10203040u); Put32(b,0x60u,0x50u); Put32(b,0x64u,0x10u);
  Put32(b,0x68u,0x60u); Put32(b,0x6cu,0x20u); Put32(b,0x70u,0x80u); Put32(b,0x74u,0x30u);
  Put32(b,0x7cu,0xb0u); Put32(b,0x80u,0x10u); Put32(b,0x84u,0xc0u); Put32(b,0x88u,0x10u);
  Put32(b,0x8cu,0x180u); Put32(b,0x90u,1u); Put32(b,0x94u,1u); Put32(b,0x98u,1u);
  for(std::size_t i=0;i<0x200u;++i) b[0xa0u+i]=static_cast<std::uint8_t>((i+0x31u)&0xffu);
  return b;
}

std::vector<std::uint8_t> MakeSec2Booter() {
  std::vector<std::uint8_t> b(0x400u,0u);
  Put32(b,0,0x10deu); Put32(b,4,1u); Put32(b,8,0x400u); Put32(b,12,0x40u); Put32(b,16,0x180u); Put32(b,20,0x100u);
  Put32(b,0x40u+0,0x100u); Put32(b,0x40u+4,0x40u); Put32(b,0x40u+8,0x30u); Put32(b,0x40u+12,0x34u);
  Put32(b,0x40u+24,0x38u); Put32(b,0x40u+28,0x70u); Put32(b,0x40u+32,0x20u);
  Put32(b,0x30u,0x20u); Put32(b,0x34u,0x20u); Put32(b,0x38u,2u);
  Put32(b,0x70u+0,0u); Put32(b,0x70u+4,0x20u); Put32(b,0x70u+8,0x40u); Put32(b,0x70u+12,0x20u); Put32(b,0x70u+16,1u);
  Put32(b,0x84u+0,0u); Put32(b,0x84u+4,0x20u); Put32(b,0x84u+8,0x20u); Put32(b,0x84u+12,0x10u);
  for(std::size_t i=0;i<0x40u;++i) b[0x100u+i]=static_cast<std::uint8_t>(0xa0u+i);
  for(std::size_t i=0;i<0x100u;++i) b[0x180u+i]=static_cast<std::uint8_t>(i);
  return b;
}
}

int main() {
  using namespace rtxmac::nvidia::package;
  const auto vbios=MakeVbios(); const auto elf=MakeGspElf();
  const auto gspBoot=MakeGspBootloader(); const auto sec2=MakeSec2Booter();
  const SourceInputs in{
      .pci={.vendor=0x10deu,.device=0x2489u,.subsystemVendor=0x1462u,.subsystemDevice=0x3970u},
      .vramBytes=8ull<<30u,
      .vbios=vbios,.gspElf=elf,.gspBootloaderContainer=gspBoot,.sec2BooterContainer=sec2};

  const auto built=BuildGa10xPackage(in);
  assert(built.status==BuildStatus::Ok); assert(!built.bytes.empty());
  const auto view=ParseAndVerify(built.bytes);
  assert(view.status==ParseStatus::Ok); assert(view.packageBytes==built.bytes.size());
  assert(view.metadata.pci.device==0x2489u); assert(view.metadata.gspAppVersion==0x10203040u);
  assert(view.metadata.gspMonitorCodeOffset==0x80u); assert(view.metadata.gspMonitorDataOffset==0x60u);
  assert(view.metadata.gspManifestOffset==0x50u); assert(view.metadata.fwsecUcodeId==5u);
  assert(view.metadata.sec2CodeSize==0x20u && view.metadata.sec2DataOffset==0x40u);

  const auto fw=FindSection(built.bytes,view,SectionKind::GspFirmwareImage);
  const auto sig=FindSection(built.bytes,view,SectionKind::GspFirmwareSignature);
  const auto boot=FindSection(built.bytes,view,SectionKind::GspBootloader);
  const auto frts=FindSection(built.bytes,view,SectionKind::FrtsFwsecImage);
  const auto sec=FindSection(built.bytes,view,SectionKind::Sec2BooterImage);
  assert(fw.size()==0x2500u && fw[0]==7u); assert(sig.size()==0x1000u && sig[0]==11u);
  assert(boot.size()==0x200u && boot[0]==0x31u); assert(frts.size()==0x600u);
  assert(Get32(frts,0x1acu)==0x15u);
  assert(sec.size()==0x100u);
  for(std::size_t i=0;i<0x20u;++i) assert(sec[0x20u+i]==static_cast<std::uint8_t>(0xc0u+i));

  auto corrupt=built.bytes; corrupt[static_cast<std::size_t>(built.sections[0].offset)]^=1u;
  assert(ParseAndVerify(corrupt).status==ParseStatus::HashMismatch);
  auto badMagic=built.bytes; badMagic[0]^=1u; assert(ParseAndVerify(badMagic).status==ParseStatus::BadMagic);
  auto duplicate=built.bytes; Put32(duplicate,160u+56u,1u);
  assert(ParseAndVerify(duplicate).status==ParseStatus::DuplicateOrUnknownSection);
  auto reserved=built.bytes; reserved[116u]=1u; assert(ParseAndVerify(reserved).status==ParseStatus::BadHeader);

  auto badTarget=in; badTarget.pci.device=0x1234u;
  assert(BuildGa10xPackage(badTarget).status==BuildStatus::InvalidTarget);
  std::cout<<"rtxmac validated boot-package tests passed\n";
}
