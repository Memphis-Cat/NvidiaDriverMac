#pragma once

#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOReturn.h>

#include "rtxmac/pramin.hpp"

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

// Cold readback verification primitive. It selects each planned PRAMIN window,
// reads the staged bytes back through the bounded aperture, and returns an I/O
// error on the first mismatch. It never runs from Start_Impl.
[[nodiscard]] kern_return_t RTXMacVerifyPramin(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    const void* expected,
    std::uint64_t expectedBytes) noexcept;
