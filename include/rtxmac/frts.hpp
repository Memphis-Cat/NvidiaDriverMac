#pragma once

#include "rtxmac/vbios.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rtxmac::nvidia::frts {

inline constexpr std::size_t kReadVbiosDescBytes = 24u;
inline constexpr std::size_t kFrtsRegionDescBytes = 20u;
inline constexpr std::size_t kFrtsCommandBytes = 44u;
inline constexpr std::uint32_t kFrtsCommandId = 0x15u;
inline constexpr std::uint32_t kFrtsReadVbiosFlags = 2u;
inline constexpr std::uint32_t kFrtsRegionMediaFramebuffer = 2u;
inline constexpr std::uint32_t kFrtsRegionPages4K = 0x100u;
inline constexpr std::uint64_t kFrtsReserveFromVramEnd = 0x200000ull;
inline constexpr std::size_t kRsa3072SignatureBytes = 0x180u;
inline constexpr std::uint32_t kDmemMapperEntryId = 4u;

struct FrtsCommand {
  std::array<std::uint8_t, kFrtsCommandBytes> bytes{};
  std::uint64_t regionOffsetBytes{};
  std::uint32_t regionOffset4K{};
};

// Matches the current Ampere FRTS placement used by the open NVIDIA/tinygrad
// bring-up path: reserve 2 MiB at the end of VRAM and use the first 1 MiB of
// that reservation as the FRTS region.
[[nodiscard]] std::optional<FrtsCommand> BuildFrtsCommand(std::uint64_t vramBytes) noexcept;

enum class PatchStatus : std::uint8_t {
  Ok = 0,
  InvalidStoredImage,
  InterfaceHeaderOutOfRange,
  InvalidInterfaceHeader,
  InterfaceEntriesOutOfRange,
  DmemMapperNotFound,
  DmemMapperOutOfRange,
  CommandBufferOutOfRange,
  SignatureDestinationOutOfRange,
  SignatureSourceTooSmall,
};

struct PatchPlan {
  PatchStatus status{PatchStatus::InvalidStoredImage};
  std::size_t logicalImageBytes{};
  std::size_t interfaceHeaderOffset{};
  std::size_t dmemMapperOffset{};
  std::size_t initCommandFieldOffset{};
  std::size_t commandBufferOffset{};
  std::size_t commandBufferCapacity{};
  std::size_t signatureDestinationOffset{};
  std::size_t signatureBytes{kRsa3072SignatureBytes};
};

[[nodiscard]] PatchPlan PlanFwsecFrtsPatch(
    const vbios::DescriptorV3& descriptor,
    std::span<const std::uint8_t> storedImage,
    std::size_t signatureSourceBytes) noexcept;

// Pure offline transformation. The source image is copied; no device memory is
// touched. Uses the last 384 bytes of the production signature blob, matching
// the RSA-3072 signature placement used by the Ampere FWSEC path.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> ApplyFwsecFrtsPatch(
    std::span<const std::uint8_t> storedImage,
    const PatchPlan& plan,
    const FrtsCommand& command,
    std::span<const std::uint8_t> productionSignature);

[[nodiscard]] const char* PatchStatusName(PatchStatus status) noexcept;

} // namespace rtxmac::nvidia::frts
