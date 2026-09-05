#include "rtxmac/elf.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
void Put16(std::vector<std::uint8_t>& v, std::size_t o, std::uint16_t x) {
  v[o] = static_cast<std::uint8_t>(x); v[o+1] = static_cast<std::uint8_t>(x >> 8u);
}
void Put32(std::vector<std::uint8_t>& v, std::size_t o, std::uint32_t x) {
  for (int i=0;i<4;++i) v[o+i] = static_cast<std::uint8_t>(x >> (i*8));
}
void Put64(std::vector<std::uint8_t>& v, std::size_t o, std::uint64_t x) {
  for (int i=0;i<8;++i) v[o+i] = static_cast<std::uint8_t>(x >> (i*8));
}
}

int main() {
  // ELF header at 0, 4 section headers at 0x100, shstrtab at 0x240,
  // fwimage at 0x300 and signature at 0x340.
  std::vector<std::uint8_t> elf(0x380u, 0u);
  elf[0]=0x7f; elf[1]='E'; elf[2]='L'; elf[3]='F'; elf[4]=2; elf[5]=1;
  Put64(elf, 0x28, 0x100); Put16(elf, 0x3A, 64); Put16(elf, 0x3C, 4); Put16(elf, 0x3E, 1);
  const char names[] = "\0.shstrtab\0.fwimage\0.fwsignature_ga10x\0";
  for (std::size_t i=0;i<sizeof(names);++i) elf[0x240+i]=static_cast<std::uint8_t>(names[i]);
  // section 1 string table
  Put32(elf,0x140+0,1); Put32(elf,0x140+4,3); Put64(elf,0x140+24,0x240); Put64(elf,0x140+32,sizeof(names)); Put64(elf,0x140+48,1);
  // section 2 fwimage, name offset 11
  Put32(elf,0x180+0,11); Put32(elf,0x180+4,1); Put64(elf,0x180+24,0x300); Put64(elf,0x180+32,0x20); Put64(elf,0x180+48,0x1000);
  // section 3 signature, name offset 20
  Put32(elf,0x1C0+0,20); Put32(elf,0x1C0+4,1); Put64(elf,0x1C0+24,0x340); Put64(elf,0x1C0+32,0x30); Put64(elf,0x1C0+48,4);

  const auto fw = rtxmac::elf::FindSection(elf, ".fwimage");
  assert(fw.status == rtxmac::elf::Status::Ok && fw.offset == 0x300u && fw.size == 0x20u);
  const auto sig = rtxmac::elf::FindSection(elf, ".fwsignature_ga10x");
  assert(sig.status == rtxmac::elf::Status::Ok && sig.offset == 0x340u && sig.size == 0x30u);
  assert(rtxmac::elf::FindSection(elf, ".missing").status == rtxmac::elf::Status::NotFound);

  auto bad = elf; bad[4] = 1;
  assert(rtxmac::elf::FindSection(bad, ".fwimage").status == rtxmac::elf::Status::UnsupportedClass);
  bad = elf; Put64(bad,0x180+32,0x1000);
  assert(rtxmac::elf::FindSection(bad, ".fwimage").status == rtxmac::elf::Status::BadSectionRange);
  std::cout << "rtxmac ELF section parser tests passed\n";
}
