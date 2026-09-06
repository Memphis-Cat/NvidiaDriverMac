#pragma once

#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOReturn.h>

#include "rtxmac/sysmem_flush.hpp"

#include <cstdint>

// Cold/default-off GA102 sysmem-flush page programmer. This must succeed before
// any live Falcon reset is attempted. It is intentionally disconnected from
// Start_Impl and requires an already-validated planner result.
[[nodiscard]] kern_return_t RTXMacProgramSysmemFlushPage(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::SysmemFlushPagePlan& plan,
    bool writesEnabled = false) noexcept;

// Restore the exact register state captured by the plan. Rollback is refused if
// the live registers no longer contain plan.newLo/newHi, avoiding clobbering a
// concurrent owner that changed the flush page after programming.
[[nodiscard]] kern_return_t RTXMacRollbackSysmemFlushPage(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::SysmemFlushPagePlan& plan,
    bool writesEnabled = false) noexcept;
