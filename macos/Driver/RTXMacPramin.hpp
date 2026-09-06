#pragma once

#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOReturn.h>

#include "rtxmac/pramin.hpp"

#include <cstdint>

// Cold execution primitive for GA102 PRAMIN staging. This is intentionally
// separate from RTXMacDriver::Start_Impl: merely attaching the DEXT never calls
// it. The caller must provide an already-copied BAR0 memory descriptor and a
// planner-produced PraminStagePlan. writesEnabled defaults false so an
// accidental call cannot perform MMIO writes.
[[nodiscard]] kern_return_t RTXMacStagePramin(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    const void* source,
    std::uint64_t sourceBytes,
    bool writesEnabled = false) noexcept;

// Cold readback verification primitive. It must write the PRAMIN selector to
// inspect each window, so it uses the same default-off write gate.
[[nodiscard]] kern_return_t RTXMacVerifyPramin(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    const void* expected,
    std::uint64_t expectedBytes,
    bool writesEnabled = false) noexcept;
