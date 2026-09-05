#include "rtxmac/mmio_trace.hpp"
#include "rtxmac/nvidia_probe.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace {
class FakeMmio final : public rtxmac::ReadOnlyMmio {
public:
  std::unordered_map<std::uint32_t, std::uint32_t> regs;
  rtxmac::Read32Result Read32(std::uint32_t offset) override {
    const auto it = regs.find(offset);
    if (it == regs.end()) return {rtxmac::IoStatus::Unmapped, 0};
    return {rtxmac::IoStatus::Ok, it->second};
  }
};
}

int main() {
  using namespace rtxmac;
  using namespace rtxmac::nvidia;

  FakeMmio hardware;
  hardware.regs[kPmcBoot0Offset] = 0x174000A1u;
  hardware.regs[kPmcBoot42Offset] = 0x17412300u;

  RecordingMmio recorder(hardware);
  const auto first = ProbeIdentity(recorder);
  assert(first.status == ProbeStatus::Ok);
  assert(recorder.Events().size() == 2);

  const std::vector<MmioReadEvent> trace(recorder.Events().begin(), recorder.Events().end());
  ReplayMmio replay(trace);
  const auto second = ProbeIdentity(replay);
  assert(second.status == ProbeStatus::Ok);
  assert(second.boot0 == first.boot0);
  assert(second.boot42.raw == first.boot42.raw);
  assert(replay.Complete());

  ReplayMmio wrongOrder(trace);
  const auto mismatch = wrongOrder.Read32(kPmcBoot42Offset);
  assert(!mismatch);
  assert(wrongOrder.Mismatch());

  std::cout << "rtxmac MMIO record/replay tests passed\n";
  return 0;
}
