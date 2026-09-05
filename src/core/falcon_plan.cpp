#include "rtxmac/falcon_plan.hpp"

#include <limits>

namespace rtxmac::nvidia::falcon {
namespace {

constexpr std::uint32_t kEngineReset = 0x3C0u;
constexpr std::uint32_t kMailbox0 = 0x040u;
constexpr std::uint32_t kMailbox1 = 0x044u;
constexpr std::uint32_t kHwcfg2 = 0x0F4u;
constexpr std::uint32_t kCpuCtl = 0x100u;
constexpr std::uint32_t kBootVec = 0x104u;
constexpr std::uint32_t kDmaCtl = 0x10Cu;
constexpr std::uint32_t kDmaBase = 0x110u;
constexpr std::uint32_t kDmaMemOffset = 0x114u;
constexpr std::uint32_t kDmaCmd = 0x118u;
constexpr std::uint32_t kDmaFbOffset = 0x11Cu;
constexpr std::uint32_t kDmaBase1 = 0x128u;
constexpr std::uint32_t kFalconRm = 0x084u;
constexpr std::uint32_t kRiscvBcrCtrl = 0x1668u; // RISC-V PRI base 0x1000 + 0x668
constexpr std::uint32_t kFbifTransCfg0 = 0x600u;
constexpr std::uint32_t kFbifCtl = 0x624u;
constexpr std::uint32_t kBromModSel = 0x1180u;
constexpr std::uint32_t kBromUcodeId = 0x1198u;
constexpr std::uint32_t kBromEngineIdMask = 0x119Cu;
constexpr std::uint32_t kBromParaAddr0 = 0x1210u;

constexpr std::uint32_t kHwcfg2RiscvMask = 1u << 10u;
constexpr std::uint32_t kHwcfg2ScrubbingMask = 1u << 12u;
constexpr std::uint32_t kCpuCtlHaltedMask = 1u << 4u;
constexpr std::uint32_t kDmaCmdFullMask = 1u << 0u;
constexpr std::uint32_t kDmaCmdIdleMask = 1u << 1u;
constexpr std::uint32_t kFbifAllowPhysicalNoContextMask = 1u << 7u;
constexpr std::uint32_t kFbifTransCfgMask = 0x7u;
constexpr std::uint32_t kFbifPhysicalFramebuffer = 1u << 2u; // target=FB(0), mem_type=physical(1)
constexpr std::uint32_t kRiscvBcrMask = (1u << 0u) | (1u << 4u) | (1u << 8u);
constexpr std::uint32_t kRiscvBootValue = (1u << 4u) | (1u << 8u); // core_select=RISCV, valid=0, brfetch=1

constexpr std::uint32_t kImemDmaCommand = 0x614u; // size=256B, IMEM=1, SEC=1, write=0, ctxdma=0
constexpr std::uint32_t kDmemDmaCommand = 0x600u; // size=256B, DMEM, write=0, ctxdma=0
constexpr std::uint32_t kTransferBytes = 256u;
constexpr std::uint32_t kDefaultPollTimeoutMs = 10000u;

void AddPoll(Plan& p, std::uint32_t address, std::uint32_t mask, std::uint32_t value) {
  p.actions.push_back({ActionKind::PollMaskEqual, address, value, mask, kDefaultPollTimeoutMs});
}

bool AddDma(Plan& p, std::uint32_t base, std::uint64_t source,
            std::uint32_t dest, std::uint32_t memoryOffset,
            std::uint32_t bytes, std::uint32_t command) {
  if ((bytes % kTransferBytes) != 0u) return false;
  if ((source & 0xFFull) != 0ull) return false;

  const std::uint64_t shifted = source >> 8u;
  if ((shifted >> 41u) != 0ull) return false; // BASE32 + BASE1[8:0]

  AddPoll(p, base + kDmaCmd, kDmaCmdFullMask, 0u);
  p.actions.push_back({ActionKind::Write32, base + kDmaBase, static_cast<std::uint32_t>(shifted), 0xFFFFFFFFu, 0u});
  p.actions.push_back({ActionKind::Write32, base + kDmaBase1, static_cast<std::uint32_t>((shifted >> 32u) & 0x1FFu), 0x1FFu, 0u});

  for (std::uint32_t done = 0u; done < bytes; done += kTransferBytes) {
    if (dest > std::numeric_limits<std::uint32_t>::max() - done ||
        memoryOffset > std::numeric_limits<std::uint32_t>::max() - done) return false;
    AddPoll(p, base + kDmaCmd, kDmaCmdFullMask, 0u);
    p.actions.push_back({ActionKind::Write32, base + kDmaMemOffset, dest + done, 0x00FFFFFFu, 0u});
    p.actions.push_back({ActionKind::Write32, base + kDmaFbOffset, memoryOffset + done, 0xFFFFFFFFu, 0u});
    p.actions.push_back({ActionKind::Write32, base + kDmaCmd, command, 0x0001773Fu, 0u});
  }
  AddPoll(p, base + kDmaCmd, kDmaCmdIdleMask, kDmaCmdIdleMask);
  return true;
}

} // namespace

Plan PlanReset(Engine engine, bool bootRiscv, std::uint32_t chipId) {
  Plan out{true, {}};
  const std::uint32_t base = EngineBase(engine);

  out.actions.push_back({ActionKind::MaskedWrite, base + kEngineReset, 1u, 1u, 0u});
  out.actions.push_back({ActionKind::DelayMilliseconds, 0u, 100u, 0u, 0u});
  out.actions.push_back({ActionKind::MaskedWrite, base + kEngineReset, 0u, 1u, 0u});
  AddPoll(out, base + kHwcfg2, kHwcfg2ScrubbingMask, 0u);

  if (bootRiscv) {
    out.actions.push_back({ActionKind::MaskedWrite, base + kRiscvBcrCtrl, kRiscvBootValue, kRiscvBcrMask, 0u});
  } else {
    // The current Ampere path performs these only when HWCFG2.riscv == 1.
    out.actions.push_back({ActionKind::IfMaskEqualBegin, base + kHwcfg2, kHwcfg2RiscvMask, kHwcfg2RiscvMask, 0u});
    out.actions.push_back({ActionKind::MaskedWrite, base + kRiscvBcrCtrl, 0u, 1u << 4u, 0u});
    AddPoll(out, base + kRiscvBcrCtrl, 1u, 1u);
    out.actions.push_back({ActionKind::Write32, base + kFalconRm, chipId, 0xFFFFFFFFu, 0u});
    out.actions.push_back({ActionKind::EndIf, 0u, 0u, 0u, 0u});
  }
  return out;
}

Plan PlanAuthenticatedExecution(const HsParameters& params) {
  Plan out{false, {}};
  const std::uint32_t base = EngineBase(params.engine);

  if ((params.imemBytes % kTransferBytes) != 0u || (params.dmemBytes % kTransferBytes) != 0u) return out;
  if (params.codeOffset < params.imemVirtualBase || params.dataOffset < params.dmemVirtualBase) return out;
  if (params.imagePhysicalAddress > std::numeric_limits<std::uint64_t>::max() - params.codeOffset ||
      params.imagePhysicalAddress > std::numeric_limits<std::uint64_t>::max() - params.dataOffset) return out;

  const std::uint64_t imemSource = params.imagePhysicalAddress + params.codeOffset - params.imemVirtualBase;
  const std::uint64_t dmemSource = params.imagePhysicalAddress + params.dataOffset - params.dmemVirtualBase;

  // disable_ctx_req(base)
  out.actions.push_back({ActionKind::MaskedWrite, base + kFbifCtl,
                         kFbifAllowPhysicalNoContextMask, kFbifAllowPhysicalNoContextMask, 0u});
  out.actions.push_back({ActionKind::Write32, base + kDmaCtl, 0u, 0x7u, 0u});
  out.actions.push_back({ActionKind::MaskedWrite, base + kFbifTransCfg0,
                         kFbifPhysicalFramebuffer, kFbifTransCfgMask, 0u});

  if (!AddDma(out, base, imemSource, params.imemPhysicalBase, params.imemVirtualBase,
              params.imemBytes, kImemDmaCommand)) return {false, {}};
  if (!AddDma(out, base, dmemSource, params.dmemPhysicalBase, params.dmemVirtualBase,
              params.dmemBytes, kDmemDmaCommand)) return {false, {}};

  out.actions.push_back({ActionKind::Write32, base + kBromParaAddr0, params.pkcOffset, 0xFFFFFFFFu, 0u});
  out.actions.push_back({ActionKind::Write32, base + kBromEngineIdMask, params.engineIdMask, 0xFFFFFFFFu, 0u});
  out.actions.push_back({ActionKind::Write32, base + kBromUcodeId, params.ucodeId, 0xFFu, 0u});
  out.actions.push_back({ActionKind::MaskedWrite, base + kBromModSel, 1u, 0xFFu, 0u}); // RSA3K
  out.actions.push_back({ActionKind::Write32, base + kBootVec, params.imemVirtualBase, 0xFFFFFFFFu, 0u});

  if (params.mailbox.has_value()) {
    out.actions.push_back({ActionKind::Write32, base + kMailbox0, static_cast<std::uint32_t>(*params.mailbox), 0xFFFFFFFFu, 0u});
    out.actions.push_back({ActionKind::Write32, base + kMailbox1, static_cast<std::uint32_t>(*params.mailbox >> 32u), 0xFFFFFFFFu, 0u});
  }

  // start_cpu(base) reads CPUCTL.alias_en and either writes 0x2 to the alias
  // register or sets CPUCTL.startcpu. Keep that branch semantic until live
  // CPUCTL state is supplied by the executor.
  out.actions.push_back({ActionKind::StartCpuRespectAlias, base + kCpuCtl, 0u, 0u, 0u});
  AddPoll(out, base + kCpuCtl, kCpuCtlHaltedMask, kCpuCtlHaltedMask);
  out.valid = true;
  return out;
}

} // namespace rtxmac::nvidia::falcon
