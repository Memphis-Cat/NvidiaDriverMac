#include "rtxmac/dma_plan.hpp"
#include "rtxmac/gsp_boot.hpp"
#include "rtxmac/nvfw.hpp"
#include "rtxmac/pci_identity.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

int ParsePci(std::string_view text) {
  const auto id = rtxmac::ParseWindowsPciHardwareId(text);
  if (!id) {
    std::cerr << "Could not parse PCI hardware ID.\n";
    return 1;
  }
  std::cout << rtxmac::Describe(*id) << '\n';
  std::cout << "nvidia=" << (rtxmac::IsNvidia(*id) ? "yes" : "no") << '\n';
  std::cout << "known_rtx3060ti=" << (rtxmac::IsKnownRtx3060Ti(*id) ? "yes" : "no") << '\n';
  return 0;
}

int PrintGspLayout() {
  const auto layout = rtxmac::nvidia::gsp::PlanQueueMemory();
  if (!layout) return 1;
  std::cout << std::hex << std::showbase;
  std::cout << "queue_bytes=" << layout->queueBytes << '\n';
  std::cout << "pte_count=" << layout->pageTableEntryCount << '\n';
  std::cout << "page_table_bytes=" << layout->pageTableBytes << '\n';
  std::cout << "command_queue_offset=" << layout->commandQueueOffset << '\n';
  std::cout << "status_queue_offset=" << layout->statusQueueOffset << '\n';
  std::cout << "total_bytes=" << layout->totalBytes << '\n';

  const auto dma = rtxmac::PlanConservativeDmaChunks(layout->totalBytes);
  std::cout << "dma_chunks=" << std::dec << dma.chunks.size() << '\n';
  for (std::size_t i = 0; i < dma.chunks.size(); ++i) {
    std::cout << "  [" << i << "] offset=" << std::hex << std::showbase << dma.chunks[i].offset
              << " length=" << dma.chunks[i].length << '\n';
  }
  return 0;
}

int FirmwareInfo(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::cerr << "Could not open firmware file.\n";
    return 1;
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const auto info = rtxmac::nvidia::fw::ParseBooterImage(bytes);
  std::cout << "status=" << rtxmac::nvidia::fw::ParseStatusName(info.status) << '\n';
  std::cout << std::hex << std::showbase;
  std::cout << "bin_magic=" << info.bin.magic << '\n';
  std::cout << "bin_version=" << info.bin.version << '\n';
  std::cout << "bin_size=" << info.bin.binSize << '\n';
  std::cout << "header_offset=" << info.bin.headerOffset << '\n';
  std::cout << "data_offset=" << info.bin.dataOffset << '\n';
  std::cout << "data_size=" << info.bin.dataSize << '\n';
  std::cout << "signature_offset=" << info.hs.sigProdOffset << '\n';
  std::cout << "signature_size=" << info.hs.sigProdSize << '\n';
  std::cout << "num_signatures=" << std::dec << info.hs.numSig << '\n';
  std::cout << "num_apps=" << info.load.numApps << '\n';
  std::cout << std::hex << std::showbase;
  std::cout << "first_app_offset=" << info.firstApp.offset << '\n';
  std::cout << "first_app_size=" << info.firstApp.size << '\n';
  return info.status == rtxmac::nvidia::fw::ParseStatus::Ok ? 0 : 1;
}

void Usage() {
  std::cerr << "usage:\n"
            << "  rtxmac-offline parse \"PCI\\\\VEN_10DE&DEV_2489&SUBSYS_...\"\n"
            << "  rtxmac-offline gsp-layout\n"
            << "  rtxmac-offline firmware-info <booter firmware.bin>\n";
}

} // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string_view(argv[1]) == "gsp-layout" && argc == 2) return PrintGspLayout();
  if (argc == 3 && std::string_view(argv[1]) == "parse") return ParsePci(argv[2]);
  if (argc == 3 && std::string_view(argv[1]) == "firmware-info") return FirmwareInfo(argv[2]);
  Usage();
  return 2;
}
