#include "rtxmac/elf.hpp"

#include <cstddef>
#include <limits>

namespace rtxmac::elf {
namespace {

constexpr std::size_t kElf64HeaderBytes = 64u;
constexpr std::size_t kElf64SectionBytes = 64u;

std::uint16_t LoadLe16(std::span<const std::uint8_t> d, std::size_t o) noexcept {
  return static_cast<std::uint16_t>(d[o]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(d[o + 1]) << 8u);
}

std::uint32_t LoadLe32(std::span<const std::uint8_t> d, std::size_t o) noexcept {
  return static_cast<std::uint32_t>(d[o]) |
      (static_cast<std::uint32_t>(d[o + 1]) << 8u) |
      (static_cast<std::uint32_t>(d[o + 2]) << 16u) |
      (static_cast<std::uint32_t>(d[o + 3]) << 24u);
}

std::uint64_t LoadLe64(std::span<const std::uint8_t> d, std::size_t o) noexcept {
  std::uint64_t out = 0u;
  for (std::size_t i = 0; i < 8u; ++i) out |= static_cast<std::uint64_t>(d[o + i]) << (i * 8u);
  return out;
}

bool RangeFits(std::size_t total, std::uint64_t off, std::uint64_t size) noexcept {
  return off <= total && size <= static_cast<std::uint64_t>(total) - off;
}

struct RawSection {
  std::uint32_t name{};
  std::uint32_t type{};
  std::uint64_t flags{};
  std::uint64_t offset{};
  std::uint64_t size{};
  std::uint64_t alignment{};
};

RawSection ReadSection(std::span<const std::uint8_t> image, std::size_t off) noexcept {
  return {
    .name = LoadLe32(image, off + 0u),
    .type = LoadLe32(image, off + 4u),
    .flags = LoadLe64(image, off + 8u),
    .offset = LoadLe64(image, off + 24u),
    .size = LoadLe64(image, off + 32u),
    .alignment = LoadLe64(image, off + 48u),
  };
}

} // namespace

Section FindSection(std::span<const std::uint8_t> image, std::string_view name) noexcept {
  Section out{};
  if (image.size() < kElf64HeaderBytes) return out;
  if (image[0] != 0x7Fu || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') {
    out.status = Status::BadMagic;
    return out;
  }
  if (image[4] != 2u) {
    out.status = Status::UnsupportedClass;
    return out;
  }
  if (image[5] != 1u) {
    out.status = Status::UnsupportedEndian;
    return out;
  }

  const std::uint64_t shoff = LoadLe64(image, 0x28u);
  const std::uint16_t shentsize = LoadLe16(image, 0x3Au);
  const std::uint16_t shnum = LoadLe16(image, 0x3Cu);
  const std::uint16_t shstrndx = LoadLe16(image, 0x3Eu);
  if (shnum == 0u || shstrndx == 0xFFFFu) {
    out.status = Status::UnsupportedExtendedIndex;
    return out;
  }
  if (shentsize < kElf64SectionBytes || shstrndx >= shnum) {
    out.status = Status::BadSectionTable;
    return out;
  }
  if (static_cast<std::uint64_t>(shnum) > std::numeric_limits<std::uint64_t>::max() / shentsize ||
      !RangeFits(image.size(), shoff, static_cast<std::uint64_t>(shnum) * shentsize)) {
    out.status = Status::BadSectionTable;
    return out;
  }

  const std::uint64_t strHdrOff64 = shoff + static_cast<std::uint64_t>(shstrndx) * shentsize;
  if (strHdrOff64 > std::numeric_limits<std::size_t>::max()) {
    out.status = Status::BadSectionTable;
    return out;
  }
  const RawSection strings = ReadSection(image, static_cast<std::size_t>(strHdrOff64));
  if (!RangeFits(image.size(), strings.offset, strings.size)) {
    out.status = Status::BadStringTable;
    return out;
  }

  for (std::uint16_t i = 0u; i < shnum; ++i) {
    const std::uint64_t hdrOff64 = shoff + static_cast<std::uint64_t>(i) * shentsize;
    const RawSection s = ReadSection(image, static_cast<std::size_t>(hdrOff64));
    if (!RangeFits(image.size(), s.offset, s.size)) {
      // SHT_NOBITS (8) intentionally has no file payload.
      if (s.type != 8u) {
        out.status = Status::BadSectionRange;
        return out;
      }
    }
    if (s.name >= strings.size) {
      out.status = Status::BadSectionName;
      return out;
    }

    const std::uint64_t nameOff = strings.offset + s.name;
    const std::uint64_t strEnd = strings.offset + strings.size;
    std::uint64_t end = nameOff;
    while (end < strEnd && image[static_cast<std::size_t>(end)] != 0u) ++end;
    if (end == strEnd) {
      out.status = Status::BadSectionName;
      return out;
    }
    const auto* ptr = reinterpret_cast<const char*>(image.data() + static_cast<std::size_t>(nameOff));
    const std::string_view sectionName(ptr, static_cast<std::size_t>(end - nameOff));
    if (sectionName == name) {
      if (s.type == 8u) {
        out.status = Status::BadSectionRange;
        return out;
      }
      out.status = Status::Ok;
      out.offset = s.offset;
      out.size = s.size;
      out.flags = s.flags;
      out.type = s.type;
      out.alignment = s.alignment;
      return out;
    }
  }

  out.status = Status::NotFound;
  return out;
}

const char* StatusName(Status status) noexcept {
  switch (status) {
    case Status::Ok: return "ok";
    case Status::TooSmall: return "too-small";
    case Status::BadMagic: return "bad-magic";
    case Status::UnsupportedClass: return "unsupported-class";
    case Status::UnsupportedEndian: return "unsupported-endian";
    case Status::BadSectionTable: return "bad-section-table";
    case Status::UnsupportedExtendedIndex: return "unsupported-extended-section-index";
    case Status::BadStringTable: return "bad-string-table";
    case Status::BadSectionRange: return "bad-section-range";
    case Status::BadSectionName: return "bad-section-name";
    case Status::NotFound: return "not-found";
  }
  return "unknown";
}

} // namespace rtxmac::elf
