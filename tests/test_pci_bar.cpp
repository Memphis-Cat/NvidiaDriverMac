#include "rtxmac/pci_bar.hpp"

#include <cassert>
#include <iostream>

int main() {
  using namespace rtxmac::pci;

  // 32-bit non-prefetchable BAR at 0xF6000000, 16 MiB.
  const auto bar32 = DecodeMemoryBar(0u, 0xF6000000u, 0u, false, 0x01000000ull);
  assert(bar32.status == BarStatus::Ok);
  assert(bar32.base == 0xF6000000ull);
  assert(bar32.size == 0x01000000ull);
  assert(!bar32.is64Bit);
  assert(!bar32.prefetchable);

  // 64-bit prefetchable BAR at 0x0000008400000000, 8 GiB.
  const auto bar64 = DecodeMemoryBar(
      1u, 0x0000000Cu, 0x00000084u, true, 0x200000000ull);
  assert(bar64.status == BarStatus::Ok);
  assert(bar64.base == 0x0000008400000000ull);
  assert(bar64.size == 0x200000000ull);
  assert(bar64.is64Bit);
  assert(bar64.prefetchable);

  // A 64-bit BAR requires the following config dword.
  assert(DecodeMemoryBar(1u, 0x00000004u, 0u, false, 0x10000000ull).status ==
         BarStatus::MissingHighDword);
  assert(DecodeMemoryBar(5u, 0x00000004u, 0u, true, 0x10000000ull).status ==
         BarStatus::MissingHighDword);

  // We never accept I/O BARs, reserved memory types, zero bases, or bad sizes.
  assert(DecodeMemoryBar(0u, 0x0000C001u, 0u, false, 0x1000u).status ==
         BarStatus::IoSpaceUnsupported);
  assert(DecodeMemoryBar(0u, 0xF0000002u, 0u, false, 0x10000000u).status ==
         BarStatus::ReservedMemoryType);
  assert(DecodeMemoryBar(0u, 0u, 0u, false, 0x1000u).status ==
         BarStatus::ZeroBase);
  assert(DecodeMemoryBar(0u, 0xF0000000u, 0u, false, 0x3000u).status ==
         BarStatus::InvalidSize);
  assert(DecodeMemoryBar(6u, 0xF0000000u, 0u, false, 0x1000u).status ==
         BarStatus::InvalidIndex);

  // Base must be aligned to the already-discovered resource size.
  assert(DecodeMemoryBar(0u, 0xF6800000u, 0u, false, 0x01000000u).status ==
         BarStatus::BaseSizeMisaligned);

  std::cout << "rtxmac read-only PCI BAR decoder tests passed\n";
}
