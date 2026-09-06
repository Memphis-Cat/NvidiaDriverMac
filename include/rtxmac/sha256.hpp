#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace rtxmac {

using Sha256Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] Sha256Digest Sha256(std::span<const std::uint8_t> data) noexcept;
[[nodiscard]] bool Sha256Equal(const Sha256Digest& a, const Sha256Digest& b) noexcept;

} // namespace rtxmac
