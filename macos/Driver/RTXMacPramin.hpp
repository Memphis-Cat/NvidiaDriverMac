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

// Read the complete planned VRAM staging range into caller-owned memory. This
// changes only the PRAMIN selector, therefore it uses the same explicit gate.
[[nodiscard]] kern_return_t RTXMacReadPramin(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    void* destination,
    std::uint64_t destinationBytes,
    bool writesEnabled = false) noexcept;

// Backup -> stage -> verify transaction. If staging or verification fails after
// the backup was captured, the original bytes are restored and verified before
// returning the original failure. If rollback itself fails, kIOReturnError is
// returned instead. The caller owns backupStorage and it must exactly match the
// planned transfer size. Nothing runs unless writesEnabled is explicitly true.
[[nodiscard]] kern_return_t RTXMacStagePraminTransactional(
    IOMemoryDescriptor* bar0,
    std::uint64_t bar0Size,
    const rtxmac::nvidia::PraminStagePlan& stage,
    const void* source,
    std::uint64_t sourceBytes,
    void* backupStorage,
    std::uint64_t backupBytes,
    bool writesEnabled = false) noexcept;
