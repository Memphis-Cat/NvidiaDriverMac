#pragma once

#include "rtxmac/mmio.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rtxmac::nvidia {

enum class SnapshotRegisterId : std::uint8_t {
  PmcBoot0 = 0,
  PmcBoot42,
  Wpr2AddrHi,
  MmuLockPrivMask,
  MmuLockAddrLo,
  MmuLockAddrHi,
  GspMailbox0,
  GspMailbox1,
  GspRiscvCpuCtl,
  VramSizeMb,
  GfwBootProgress,
  BsiSecureScratch14,
  Sec2Mailbox0,
  Sec2Mailbox1,
  Count,
};

struct SnapshotRegisterSpec {
  SnapshotRegisterId id{};
  std::uint32_t offset{};
  std::string_view name{};
};

// Offsets cross-checked against NVIDIA's published GA100/GA102 Ampere headers
// and the corresponding tinygrad generated register tables.
inline constexpr std::array<SnapshotRegisterSpec,
    static_cast<std::size_t>(SnapshotRegisterId::Count)> kDiagnosticRegisters{{
  {SnapshotRegisterId::PmcBoot0,           0x00000000u, "NV_PMC_BOOT_0"},
  {SnapshotRegisterId::PmcBoot42,          0x00000A00u, "NV_PMC_BOOT_42"},
  {SnapshotRegisterId::Wpr2AddrHi,         0x001FA828u, "NV_PFB_PRI_MMU_WPR2_ADDR_HI"},
  {SnapshotRegisterId::MmuLockPrivMask,    0x001FA7C8u, "NV_PFB_PRI_MMU_LOCK_CFG_PRIV_LEVEL_MASK"},
  {SnapshotRegisterId::MmuLockAddrLo,      0x001FA82Cu, "NV_PFB_PRI_MMU_LOCK_ADDR_LO"},
  {SnapshotRegisterId::MmuLockAddrHi,      0x001FA830u, "NV_PFB_PRI_MMU_LOCK_ADDR_HI"},
  {SnapshotRegisterId::GspMailbox0,        0x00110040u, "NV_PGSP_FALCON_MAILBOX0"},
  {SnapshotRegisterId::GspMailbox1,        0x00110044u, "NV_PGSP_FALCON_MAILBOX1"},
  {SnapshotRegisterId::GspRiscvCpuCtl,     0x00111388u, "GSP NV_PRISCV_RISCV_CPUCTL"},
  {SnapshotRegisterId::VramSizeMb,         0x001183A4u, "NV_PGC6_AON_SECURE_SCRATCH_GROUP_42"},
  {SnapshotRegisterId::GfwBootProgress,    0x00118234u, "NV_PGC6_AON_SECURE_SCRATCH_GROUP_05(0)"},
  {SnapshotRegisterId::BsiSecureScratch14, 0x001180F8u, "NV_PGC6_BSI_SECURE_SCRATCH_14"},
  {SnapshotRegisterId::Sec2Mailbox0,       0x00840040u, "NV_PSEC_FALCON_MAILBOX0"},
  {SnapshotRegisterId::Sec2Mailbox1,       0x00840044u, "NV_PSEC_FALCON_MAILBOX1"},
}};

struct SnapshotSample {
  SnapshotRegisterSpec spec{};
  IoStatus status{IoStatus::BackendError};
  std::uint32_t value{};

  [[nodiscard]] constexpr bool Ok() const noexcept { return status == IoStatus::Ok; }
};

struct DiagnosticSnapshot {
  std::array<SnapshotSample, kDiagnosticRegisters.size()> samples{};

  [[nodiscard]] const SnapshotSample* Find(SnapshotRegisterId id) const noexcept;
  [[nodiscard]] bool Complete() const noexcept;
};

struct GspCpuCtlState {
  bool active{};
  bool halted{};
};

struct MmuLockState {
  bool readable{};
  bool valid{};
  std::uint64_t low{};
  std::uint64_t high{};
};

[[nodiscard]] constexpr GspCpuCtlState DecodeGspCpuCtl(std::uint32_t value) noexcept {
  return {
    .active = ((value >> 7u) & 1u) != 0u,
    .halted = ((value >> 4u) & 1u) != 0u,
  };
}

// GA100/GA10x MMU lock registers expose address bits 31:4 and define a 12-bit
// alignment. Equivalent reconstruction: (register & 0xfffffff0) << 8.
[[nodiscard]] constexpr MmuLockState DecodeMmuLock(
    std::uint32_t privilegeMask,
    std::uint32_t lowRegister,
    std::uint32_t highRegister) noexcept {
  const bool readable = (privilegeMask & 0x1u) != 0u;
  const std::uint64_t low = static_cast<std::uint64_t>(lowRegister & 0xFFFFFFF0u) << 8u;
  const std::uint64_t high = static_cast<std::uint64_t>(highRegister & 0xFFFFFFF0u) << 8u;
  return {
    .readable = readable,
    .valid = readable && high >= low,
    .low = low,
    .high = high,
  };
}

[[nodiscard]] DiagnosticSnapshot CaptureDiagnosticSnapshot(ReadOnlyMmio& mmio);
[[nodiscard]] std::optional<std::uint64_t> VramSizeBytes(const DiagnosticSnapshot& snapshot) noexcept;
[[nodiscard]] std::optional<GspCpuCtlState> GspCpuCtl(const DiagnosticSnapshot& snapshot) noexcept;
[[nodiscard]] std::optional<MmuLockState> MmuLock(const DiagnosticSnapshot& snapshot) noexcept;

} // namespace rtxmac::nvidia
