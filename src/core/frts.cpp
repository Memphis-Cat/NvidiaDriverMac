#include "rtxmac/frts.hpp"

#include <algorithm>
#include <limits>

namespace rtxmac::nvidia::frts {
namespace {

void StoreLe32(std::span<std::uint8_t> out, std::size_t off, std::uint32_t value) noexcept {
  out[off + 0] = static_cast<std::uint8_t>(value >> 0u);
  out[off + 1] = static_cast<std::uint8_t>(value >> 8u);
  out[off + 2] = static_cast<std::uint8_t>(value >> 16u);
  out[off + 3] = static_cast<std::uint8_t>(value >> 24u);
}

void StoreLe64(std::span<std::uint8_t> out, std::size_t off, std::uint64_t value) noexcept {
  for (std::size_t i = 0; i < 8u; ++i) out[off + i] = static_cast<std::uint8_t>(value >> (i * 8u));
}

std::uint32_t LoadLe32(std::span<const std::uint8_t> in, std::size_t off) noexcept {
  return static_cast<std::uint32_t>(in[off + 0]) |
      (static_cast<std::uint32_t>(in[off + 1]) << 8u) |
      (static_cast<std::uint32_t>(in[off + 2]) << 16u) |
      (static_cast<std::uint32_t>(in[off + 3]) << 24u);
}

bool AddFits(std::size_t base, std::size_t add, std::size_t limit, std::size_t* out) noexcept {
  if (base > limit || add > limit - base) return false;
  *out = base + add;
  return true;
}

std::size_t RoundUp256(std::size_t value) noexcept {
  if (value > std::numeric_limits<std::size_t>::max() - 255u) return 0u;
  return (value + 255u) & ~std::size_t{255u};
}

} // namespace

std::optional<FrtsCommand> BuildFrtsCommand(std::uint64_t vramBytes) noexcept {
  if (vramBytes < kFrtsReserveFromVramEnd) return std::nullopt;
  const std::uint64_t region = vramBytes - kFrtsReserveFromVramEnd;
  if ((region & 0xFFFull) != 0ull) return std::nullopt;
  const std::uint64_t region4k = region >> 12u;
  if (region4k > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;

  FrtsCommand out{};
  out.regionOffsetBytes = region;
  out.regionOffset4K = static_cast<std::uint32_t>(region4k);
  auto b = std::span<std::uint8_t>(out.bytes);

  // FWSECLIC_READ_VBIOS_DESC, packed size 24.
  StoreLe32(b, 0u, 1u); // version
  StoreLe32(b, 4u, static_cast<std::uint32_t>(kReadVbiosDescBytes));
  StoreLe64(b, 8u, 0ull); // gfwImageOffset, unused by this command
  StoreLe32(b, 16u, 0u);  // gfwImageSize
  StoreLe32(b, 20u, kFrtsReadVbiosFlags);

  // FWSECLIC_FRTS_REGION_DESC, packed size 20.
  StoreLe32(b, 24u, 1u);
  StoreLe32(b, 28u, static_cast<std::uint32_t>(kFrtsRegionDescBytes));
  StoreLe32(b, 32u, out.regionOffset4K);
  StoreLe32(b, 36u, kFrtsRegionPages4K);
  StoreLe32(b, 40u, kFrtsRegionMediaFramebuffer);
  return out;
}

PatchPlan PlanFwsecFrtsPatch(
    const vbios::DescriptorV3& descriptor,
    std::span<const std::uint8_t> storedImage,
    std::size_t signatureSourceBytes) noexcept {
  PatchPlan out{};
  if (descriptor.storedSize == 0u) return out;
  const std::size_t rounded = RoundUp256(static_cast<std::size_t>(descriptor.storedSize));
  if (rounded == 0u || storedImage.size() < rounded) return out;
  out.logicalImageBytes = rounded;

  std::size_t interfaceOffset = 0u;
  if (!AddFits(static_cast<std::size_t>(descriptor.imemLoadSize),
               static_cast<std::size_t>(descriptor.interfaceOffset), rounded, &interfaceOffset) ||
      interfaceOffset + 4u > rounded) {
    out.status = PatchStatus::InterfaceHeaderOutOfRange;
    return out;
  }
  out.interfaceHeaderOffset = interfaceOffset;

  const std::uint8_t headerSize = storedImage[interfaceOffset + 1u];
  const std::uint8_t entrySize = storedImage[interfaceOffset + 2u];
  const std::uint8_t entryCount = storedImage[interfaceOffset + 3u];
  if (headerSize < 4u || entrySize < 8u || entryCount == 0u) {
    out.status = PatchStatus::InvalidInterfaceHeader;
    return out;
  }

  std::size_t entriesOffset = 0u;
  if (!AddFits(interfaceOffset, headerSize, rounded, &entriesOffset)) {
    out.status = PatchStatus::InterfaceEntriesOutOfRange;
    return out;
  }
  const std::size_t tableBytes = static_cast<std::size_t>(entrySize) * entryCount;
  if (entriesOffset > rounded || tableBytes > rounded - entriesOffset) {
    out.status = PatchStatus::InterfaceEntriesOutOfRange;
    return out;
  }

  std::optional<std::uint32_t> dmemOffset;
  for (std::size_t i = 0; i < entryCount; ++i) {
    const std::size_t ent = entriesOffset + i * entrySize;
    if (LoadLe32(storedImage, ent) == kDmemMapperEntryId) {
      dmemOffset = LoadLe32(storedImage, ent + 4u);
      break;
    }
  }
  if (!dmemOffset.has_value()) {
    out.status = PatchStatus::DmemMapperNotFound;
    return out;
  }

  std::size_t mapper = 0u;
  if (!AddFits(static_cast<std::size_t>(descriptor.imemLoadSize), *dmemOffset, rounded, &mapper) ||
      mapper + 64u > rounded) {
    out.status = PatchStatus::DmemMapperOutOfRange;
    return out;
  }
  out.dmemMapperOffset = mapper;
  out.initCommandFieldOffset = mapper + 44u;

  const std::uint32_t cmdRel = LoadLe32(storedImage, mapper + 8u);
  const std::uint32_t cmdCapacity = LoadLe32(storedImage, mapper + 12u);
  std::size_t cmd = 0u;
  if (cmdCapacity < kFrtsCommandBytes ||
      !AddFits(static_cast<std::size_t>(descriptor.imemLoadSize), cmdRel, rounded, &cmd) ||
      cmd > rounded || kFrtsCommandBytes > rounded - cmd) {
    out.status = PatchStatus::CommandBufferOutOfRange;
    return out;
  }
  out.commandBufferOffset = cmd;
  out.commandBufferCapacity = cmdCapacity;

  std::size_t sigDest = 0u;
  if (!AddFits(static_cast<std::size_t>(descriptor.imemLoadSize),
               static_cast<std::size_t>(descriptor.pkcDataOffset), rounded, &sigDest) ||
      sigDest > rounded || kRsa3072SignatureBytes > rounded - sigDest) {
    out.status = PatchStatus::SignatureDestinationOutOfRange;
    return out;
  }
  out.signatureDestinationOffset = sigDest;

  if (signatureSourceBytes < kRsa3072SignatureBytes) {
    out.status = PatchStatus::SignatureSourceTooSmall;
    return out;
  }

  out.status = PatchStatus::Ok;
  return out;
}

std::optional<std::vector<std::uint8_t>> ApplyFwsecFrtsPatch(
    std::span<const std::uint8_t> storedImage,
    const PatchPlan& plan,
    const FrtsCommand& command,
    std::span<const std::uint8_t> productionSignature) {
  if (plan.status != PatchStatus::Ok || plan.logicalImageBytes == 0u ||
      storedImage.size() < plan.logicalImageBytes ||
      productionSignature.size() < plan.signatureBytes) {
    return std::nullopt;
  }
  if (plan.initCommandFieldOffset + 4u > plan.logicalImageBytes ||
      plan.commandBufferOffset + command.bytes.size() > plan.logicalImageBytes ||
      plan.signatureDestinationOffset + plan.signatureBytes > plan.logicalImageBytes) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> out(storedImage.begin(), storedImage.begin() + plan.logicalImageBytes);
  auto bytes = std::span<std::uint8_t>(out);
  StoreLe32(bytes, plan.initCommandFieldOffset, kFrtsCommandId);
  std::copy(command.bytes.begin(), command.bytes.end(), out.begin() + plan.commandBufferOffset);
  const auto sigTail = productionSignature.last(plan.signatureBytes);
  std::copy(sigTail.begin(), sigTail.end(), out.begin() + plan.signatureDestinationOffset);
  return out;
}

const char* PatchStatusName(PatchStatus status) noexcept {
  switch (status) {
    case PatchStatus::Ok: return "ok";
    case PatchStatus::InvalidStoredImage: return "invalid-stored-image";
    case PatchStatus::InterfaceHeaderOutOfRange: return "interface-header-out-of-range";
    case PatchStatus::InvalidInterfaceHeader: return "invalid-interface-header";
    case PatchStatus::InterfaceEntriesOutOfRange: return "interface-entries-out-of-range";
    case PatchStatus::DmemMapperNotFound: return "dmem-mapper-not-found";
    case PatchStatus::DmemMapperOutOfRange: return "dmem-mapper-out-of-range";
    case PatchStatus::CommandBufferOutOfRange: return "command-buffer-out-of-range";
    case PatchStatus::SignatureDestinationOutOfRange: return "signature-destination-out-of-range";
    case PatchStatus::SignatureSourceTooSmall: return "signature-source-too-small";
  }
  return "unknown";
}

} // namespace rtxmac::nvidia::frts
