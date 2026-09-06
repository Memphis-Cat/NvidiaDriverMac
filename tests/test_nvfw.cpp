#include "rtxmac/nvfw.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
void Put32(std::vector<std::uint8_t>& v, std::size_t o, std::uint32_t x) {
  for (int i=0;i<4;++i) v[o+i] = static_cast<std::uint8_t>(x >> (i*8));
}
}

int main() {
  using namespace rtxmac::nvidia::fw;

  // Minimal structurally valid HS booter container with two production
  // signatures. patch_sig selects the second 0x20-byte signature and patch_loc
  // places it 0x40 bytes into the staged data payload.
  std::vector<std::uint8_t> hs(0x300u, 0u);
  Put32(hs,0,0x10de); Put32(hs,4,1); Put32(hs,8,0x300); Put32(hs,12,0x40); Put32(hs,16,0x100); Put32(hs,20,0x100);
  Put32(hs,0x40+0,0x200); Put32(hs,0x40+4,0x40); Put32(hs,0x40+8,0x30); Put32(hs,0x40+12,0x34);
  Put32(hs,0x40+16,0); Put32(hs,0x40+20,0); Put32(hs,0x40+24,0x38); Put32(hs,0x40+28,0x70); Put32(hs,0x40+32,0x20);
  Put32(hs,0x30,0x40); Put32(hs,0x34,0x20); Put32(hs,0x38,2);
  Put32(hs,0x70+0,0); Put32(hs,0x70+4,0x20); Put32(hs,0x70+8,0x40); Put32(hs,0x70+12,0x20); Put32(hs,0x70+16,1);
  Put32(hs,0x84+0,0); Put32(hs,0x84+4,0x20); Put32(hs,0x84+8,0x20); Put32(hs,0x84+12,0x10);
  for (std::size_t i=0;i<0x100u;++i) hs[0x100u+i]=0xAAu;
  for (std::size_t i=0;i<0x20u;++i) {
    hs[0x200u+i]=static_cast<std::uint8_t>(0x10u+i);
    hs[0x220u+i]=static_cast<std::uint8_t>(0x80u+i);
  }

  const auto hsi = ParseBooterImage(hs);
  assert(hsi.status == ParseStatus::Ok);
  const auto patched = PatchBooterProductionSignature(hs, hsi);
  assert(patched.status == BooterPatchStatus::Ok);
  assert(patched.signatureCount == 2u);
  assert(patched.signatureBytes == 0x20u);
  assert(patched.selectedSignatureOffset == 0x20u);
  assert(patched.patchOffset == 0x40u);
  assert(patched.bytes.size() == 0x100u);
  assert(patched.bytes[0] == 0xAAu && patched.bytes.back() == 0xAAu);
  for (std::size_t i=0;i<0x20u;++i)
    assert(patched.bytes[0x40u+i] == static_cast<std::uint8_t>(0x80u+i));
  // Source container remains untouched.
  assert(hs[0x100u+0x40u] == 0xAAu);

  auto zeroSigs=hs; Put32(zeroSigs,0x38,0);
  assert(PatchBooterProductionSignature(zeroSigs,ParseBooterImage(zeroSigs)).status==BooterPatchStatus::ZeroSignatures);
  auto badSelection=hs; Put32(badSelection,0x34,0x30);
  assert(PatchBooterProductionSignature(badSelection,ParseBooterImage(badSelection)).status==BooterPatchStatus::SignatureSelectionOutOfRange);
  auto badTarget=hs; Put32(badTarget,0x30,0xF0);
  assert(PatchBooterProductionSignature(badTarget,ParseBooterImage(badTarget)).status==BooterPatchStatus::PatchTargetOutOfRange);
  auto badCountPtrInfo=hsi; badCountPtrInfo.hs.numSig=0x2FFu;
  assert(PatchBooterProductionSignature(hs,badCountPtrInfo).status==BooterPatchStatus::NumSignaturesPointerOutOfRange);

  // Current 92-byte RM_RISCV_UCODE_DESC followed by a 0x180-byte payload.
  std::vector<std::uint8_t> r(0x300u, 0u);
  Put32(r,0,0x10de); Put32(r,4,1); Put32(r,8,0x300); Put32(r,12,0x40); Put32(r,16,0xA0); Put32(r,20,0x180);
  Put32(r,0x40+0,5);
  Put32(r,0x40+4,0x00); Put32(r,0x40+8,0x20);
  Put32(r,0x40+12,0x20); Put32(r,0x40+16,0x10);
  Put32(r,0x40+20,0x30); Put32(r,0x40+24,0x20);
  Put32(r,0x40+28,0x10203040u);
  Put32(r,0x40+32,0x50); Put32(r,0x40+36,0x10);
  Put32(r,0x40+40,0x60); Put32(r,0x40+44,0x20);
  Put32(r,0x40+48,0x80); Put32(r,0x40+52,0x30);
  Put32(r,0x40+60,0xB0); Put32(r,0x40+64,0x10);
  Put32(r,0x40+68,0xC0); Put32(r,0x40+72,0x10);
  Put32(r,0x40+76,0x180); Put32(r,0x40+80,1); Put32(r,0x40+84,1); Put32(r,0x40+88,1);
  const auto ri = ParseRiscvBootloader(r);
  assert(ri.status == ParseStatus::Ok);
  assert(ri.descriptor.hasExtendedFields);
  assert(ri.descriptor.appVersion == 0x10203040u);
  assert(ri.descriptor.monitorCodeOffset == 0x80u && ri.descriptor.monitorCodeSize == 0x30u);
  assert(ri.descriptor.isSmp == 1u && ri.descriptor.isPlicEnabled == 1u);

  auto bad = r; Put32(bad,0x40+48,0x170); Put32(bad,0x40+52,0x30);
  assert(ParseRiscvBootloader(bad).status == ParseStatus::RiscvPayloadOutOfRange);

  // 84-byte legacy descriptor: data begins immediately after 84 bytes.
  std::vector<std::uint8_t> legacy(0x200u, 0u);
  Put32(legacy,0,0x10de); Put32(legacy,4,1); Put32(legacy,8,0x200); Put32(legacy,12,0x40); Put32(legacy,16,0x94); Put32(legacy,20,0x100);
  Put32(legacy,0x40+0,4); Put32(legacy,0x40+48,0x20); Put32(legacy,0x40+52,0x20);
  const auto old = ParseRiscvBootloader(legacy);
  assert(old.status == ParseStatus::Ok && !old.descriptor.hasExtendedFields);

  std::cout << "rtxmac NVIDIA firmware parser/signature patch tests passed\n";
}
