#pragma once

#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOReturn.h>

#include "../../include/rtxmac/pramin.hpp"

#include <cstdint>

// Cold execution primitive for GA102 PRAMIN staging. This is intentionally
// separate from RTXMacDriver::Start_Impl: merely attaching the DEXT never calls
// it. The caller must provide an already-copied BAR0 memory descriptor and a
// planner-produced PraminStagePlan.
[[nodiscard]] kern_return_t RTXMacStagePramin(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    const void* source,
    std::uint64_t sourceBytes) noexcept;
