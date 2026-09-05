#include "rtxmac/nvidia_snapshot.hpp"

namespace rtxmac::nvidia {

const SnapshotSample* DiagnosticSnapshot::Find(SnapshotRegisterId id) const noexcept {
  for (const auto& sample : samples) {
    if (sample.spec.id == id) return &sample;
  }
  return nullptr;
}

bool DiagnosticSnapshot::Complete() const noexcept {
  for (const auto& sample : samples) {
    if (!sample.Ok()) return false;
  }
  return true;
}

DiagnosticSnapshot CaptureDiagnosticSnapshot(ReadOnlyMmio& mmio) {
  DiagnosticSnapshot out{};
  for (std::size_t i = 0; i < kDiagnosticRegisters.size(); ++i) {
    const auto& spec = kDiagnosticRegisters[i];
    const auto read = mmio.Read32(spec.offset);
    out.samples[i] = {
      .spec = spec,
      .status = read.status,
      .value = read.value,
    };
  }
  return out;
}

std::optional<std::uint64_t> VramSizeBytes(const DiagnosticSnapshot& snapshot) noexcept {
  const auto* sample = snapshot.Find(SnapshotRegisterId::VramSizeMb);
  if (!sample || !sample->Ok() || sample->value == 0u || sample->value == 0xFFFFFFFFu) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(sample->value) << 20u;
}

std::optional<GspCpuCtlState> GspCpuCtl(const DiagnosticSnapshot& snapshot) noexcept {
  const auto* sample = snapshot.Find(SnapshotRegisterId::GspRiscvCpuCtl);
  if (!sample || !sample->Ok()) return std::nullopt;
  return DecodeGspCpuCtl(sample->value);
}

} // namespace rtxmac::nvidia
