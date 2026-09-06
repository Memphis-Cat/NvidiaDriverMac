#include "rtxmac/boot_package.hpp"
#include "rtxmac/dma_plan.hpp"
#include "rtxmac/gsp_boot.hpp"
#include "rtxmac/nvfw.hpp"
#include "rtxmac/pci_identity.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::optional<std::vector<std::uint8_t>> ReadFile(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool WriteFile(const char* path, std::span<const std::uint8_t> bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return f.good();
}

std::optional<std::uint64_t> ParseU64(std::string_view text) {
  int base = 10;
  if (text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2u);
    base = 16;
  }
  if (text.empty()) return std::nullopt;
  std::uint64_t value = 0u;
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (ec != std::errc{} || ptr != text.data() + text.size()) return std::nullopt;
  return value;
}

std::string HexDigest(const rtxmac::Sha256Digest& digest) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(digest.size() * 2u, '0');
  for (std::size_t i = 0u; i < digest.size(); ++i) {
    out[i * 2u] = kHex[digest[i] >> 4u];
    out[i * 2u + 1u] = kHex[digest[i] & 0x0fu];
  }
  return out;
}

const char* SectionName(rtxmac::nvidia::package::SectionKind kind) {
  using K = rtxmac::nvidia::package::SectionKind;
  switch (kind) {
    case K::GspFirmwareImage: return "gsp-fwimage";
    case K::GspFirmwareSignature: return "gsp-fwsignature-ga10x";
    case K::GspBootloader: return "gsp-bootloader";
    case K::FrtsFwsecImage: return "frts-fwsec";
    case K::Sec2BooterImage: return "sec2-booter";
  }
  return "unknown";
}

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
  const auto bytes = ReadFile(path);
  if (!bytes) {
    std::cerr << "Could not open firmware file.\n";
    return 1;
  }
  const auto info = rtxmac::nvidia::fw::ParseBooterImage(*bytes);
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
  std::cout << "num_signatures_ptr=" << info.hs.numSig << '\n';
  std::cout << std::dec << "num_apps=" << info.load.numApps << '\n';
  std::cout << std::hex << std::showbase;
  std::cout << "first_app_offset=" << info.firstApp.offset << '\n';
  std::cout << "first_app_size=" << info.firstApp.size << '\n';
  return info.status == rtxmac::nvidia::fw::ParseStatus::Ok ? 0 : 1;
}

int PackageInfoBytes(std::span<const std::uint8_t> bytes) {
  using namespace rtxmac::nvidia::package;
  const auto view = ParseAndVerify(bytes);
  std::cout << "status=" << ParseStatusName(view.status) << '\n';
  if (view.status != ParseStatus::Ok) return 1;
  std::cout << rtxmac::Describe(view.metadata.pci) << '\n';
  std::cout << std::hex << std::showbase;
  std::cout << "package_bytes=" << view.packageBytes << '\n';
  std::cout << "vram_bytes=" << view.metadata.vramBytes << '\n';
  std::cout << "gsp_app_version=" << view.metadata.gspAppVersion << '\n';
  std::cout << "gsp_monitor_code_offset=" << view.metadata.gspMonitorCodeOffset << '\n';
  std::cout << "gsp_monitor_data_offset=" << view.metadata.gspMonitorDataOffset << '\n';
  std::cout << "gsp_manifest_offset=" << view.metadata.gspManifestOffset << '\n';
  std::cout << "fwsec_pkc_offset=" << view.metadata.fwsecPkcDataOffset << '\n';
  std::cout << "fwsec_imem_size=" << view.metadata.fwsecImemLoadSize << '\n';
  std::cout << "fwsec_dmem_size=" << view.metadata.fwsecDmemLoadSize << '\n';
  std::cout << "sec2_code_offset=" << view.metadata.sec2CodeOffset << '\n';
  std::cout << "sec2_code_size=" << view.metadata.sec2CodeSize << '\n';
  std::cout << "sec2_data_offset=" << view.metadata.sec2DataOffset << '\n';
  std::cout << "sec2_data_size=" << view.metadata.sec2DataSize << '\n';
  for (const auto& s : view.sections) {
    std::cout << SectionName(s.kind) << ": offset=" << s.offset << " size=" << s.size
              << " sha256=" << HexDigest(s.sha256) << '\n';
  }
  return 0;
}

int PackageInfo(const char* path) {
  const auto bytes = ReadFile(path);
  if (!bytes) {
    std::cerr << "Could not open package file.\n";
    return 1;
  }
  return PackageInfoBytes(*bytes);
}

int BuildPackage(int argc, char** argv) {
  using namespace rtxmac::nvidia::package;
  if (argc != 9) return 2;
  const auto pci = rtxmac::ParseWindowsPciHardwareId(argv[2]);
  const auto vram = ParseU64(argv[3]);
  if (!pci || !vram) {
    std::cerr << "Invalid PCI hardware ID or VRAM byte count.\n";
    return 1;
  }
  const auto vbios = ReadFile(argv[4]);
  const auto gsp = ReadFile(argv[5]);
  const auto bootloader = ReadFile(argv[6]);
  const auto sec2 = ReadFile(argv[7]);
  if (!vbios || !gsp || !bootloader || !sec2) {
    std::cerr << "Could not read one or more source files.\n";
    return 1;
  }

  const SourceInputs inputs{
      .pci = *pci,
      .vramBytes = *vram,
      .vbios = *vbios,
      .gspElf = *gsp,
      .gspBootloaderContainer = *bootloader,
      .sec2BooterContainer = *sec2,
  };
  const auto built = BuildGa10xPackage(inputs);
  std::cout << "build_status=" << BuildStatusName(built.status) << '\n';
  if (built.status != BuildStatus::Ok) return 1;

  const auto verified = ParseAndVerify(built.bytes);
  std::cout << "self_verify=" << ParseStatusName(verified.status) << '\n';
  if (verified.status != ParseStatus::Ok) return 1;
  if (!WriteFile(argv[8], built.bytes)) {
    std::cerr << "Could not write package file.\n";
    return 1;
  }
  std::cout << "wrote=" << argv[8] << '\n';
  return PackageInfoBytes(built.bytes);
}

void Usage() {
  std::cerr << "usage:\n"
            << "  rtxmac-offline parse \"PCI\\\\VEN_10DE&DEV_2489&SUBSYS_...\"\n"
            << "  rtxmac-offline gsp-layout\n"
            << "  rtxmac-offline firmware-info <booter firmware.bin>\n"
            << "  rtxmac-offline package-info <RTXMacBoot.rtxpkg>\n"
            << "  rtxmac-offline build-package <PCI\\\\VEN_...> <vram-bytes|0xHEX> <vbios.rom> <gsp.bin> <bootloader.bin> <booter_load.bin> <output.rtxpkg>\n";
}
} // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string_view(argv[1]) == "gsp-layout" && argc == 2) return PrintGspLayout();
  if (argc == 3 && std::string_view(argv[1]) == "parse") return ParsePci(argv[2]);
  if (argc == 3 && std::string_view(argv[1]) == "firmware-info") return FirmwareInfo(argv[2]);
  if (argc == 3 && std::string_view(argv[1]) == "package-info") return PackageInfo(argv[2]);
  if (argc >= 2 && std::string_view(argv[1]) == "build-package") {
    const int result = BuildPackage(argc, argv);
    if (result != 2) return result;
  }
  Usage();
  return 2;
}
