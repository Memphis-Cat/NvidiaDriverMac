#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rtxmac::nvidia::falcon {

enum class Engine : std::uint8_t { Gsp, Sec2 };

enum class ActionKind : std::uint8_t {
  Write32,
  MaskedWrite,
  PollMaskEqual,
  DelayMilliseconds,
  StartCpuRespectAlias,
  IfMaskEqualBegin,
  EndIf,
};

struct Action {
  ActionKind kind{};
  std::uint32_t address{};
  std::uint32_t value{};
  std::uint32_t mask{};
  std::uint32_t timeoutMs{};
};

struct HsParameters {
  Engine engine{Engine::Gsp};
  std::uint64_t imagePhysicalAddress{};
  std::uint32_t codeOffset{};
  std::uint32_t dataOffset{};
  std::uint32_t imemPhysicalBase{};
  std::uint32_t imemVirtualBase{};
  std::uint32_t imemBytes{};
  std::uint32_t dmemPhysicalBase{};
  std::uint32_t dmemVirtualBase{};
  std::uint32_t dmemBytes{};
  std::uint32_t pkcOffset{};
  std::uint32_t engineIdMask{};
  std::uint8_t ucodeId{};
  std::optional<std::uint64_t> mailbox{};
};

struct Plan {
  bool valid{};
  std::vector<Action> actions;
};

[[nodiscard]] constexpr std::uint32_t EngineBase(Engine engine) noexcept {
  return engine == Engine::Gsp ? 0x00110000u : 0x00840000u;
}

[[nodiscard]] Plan PlanReset(Engine engine, bool bootRiscv, std::uint32_t chipId);
[[nodiscard]] Plan PlanAuthenticatedExecution(const HsParameters& params);

} // namespace rtxmac::nvidia::falcon
