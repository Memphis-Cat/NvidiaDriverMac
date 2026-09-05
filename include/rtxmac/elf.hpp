#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace rtxmac::elf {

enum class Status : std::uint8_t {
  Ok = 0,
  TooSmall,
  BadMagic,
  UnsupportedClass,
  UnsupportedEndian,
  BadSectionTable,
  UnsupportedExtendedIndex,
  BadStringTable,
  BadSectionRange,
  BadSectionName,
  NotFound,
};

struct Section {
  Status status{Status::TooSmall};
  std::uint64_t offset{};
  std::uint64_t size{};
  std::uint64_t flags{};
  std::uint32_t type{};
  std::uint64_t alignment{};
};

[[nodiscard]] Section FindSection(std::span<const std::uint8_t> image, std::string_view name) noexcept;
[[nodiscard]] const char* StatusName(Status status) noexcept;

} // namespace rtxmac::elf
