#include "RTXMacDma.hpp"

#include <cstring>

namespace {
constexpr std::uint8_t kDmaAddressBits = 40u;

void ResetChunk(RTXMacPreparedDmaChunk* chunk) noexcept {
  if (!chunk) return;
  chunk->command = nullptr;
  chunk->segmentCount = 0u;
  chunk->flags = 0u;
  chunk->offset = 0u;
  chunk->length = 0u;
  for (std::uint32_t i = 0; i < kRTXMacMaxDmaSegments; ++i) {
    chunk->segments[i].address = 0u;
    chunk->segments[i].length = 0u;
  }
}

void ResetBuffer(RTXMacPreparedDmaBuffer* prepared) noexcept {
  if (!prepared) return;
  prepared->memory = nullptr;
  prepared->chunks = nullptr;
  prepared->chunkCount = 0u;
  prepared->length = 0u;
}

void CompleteChunk(RTXMacPreparedDmaChunk* chunk) noexcept {
  if (!chunk) return;
  if (chunk->command) {
    (void)chunk->command->CompleteDMA(kIODMACommandCompleteDMANoOptions);
    chunk->command->release();
  }
  ResetChunk(chunk);
}

kern_return_t PrepareChunk(IOPCIDevice* pci,
                           IOMemoryDescriptor* memory,
                           std::uint64_t offset,
                           std::uint64_t length,
                           RTXMacPreparedDmaChunk* out) noexcept {
  if (!pci || !memory || !out || length == 0u ||
      length > kRTXMacMaxDmaChunkBytes ||
      (offset % kRTXMacDmaPageBytes) != 0u ||
      (length % kRTXMacDmaPageBytes) != 0u ||
      offset > (~std::uint64_t{0}) - length) {
    return kIOReturnBadArgument;
  }

  ResetChunk(out);
  IODMACommandSpecification spec{};
  spec.options = kIODMACommandSpecificationNoOptions;
  spec.maxAddressBits = kDmaAddressBits;

  IODMACommand* command = nullptr;
  kern_return_t kr = IODMACommand::Create(
      pci, kIODMACommandCreateNoOptions, &spec, &command);
  if (kr != kIOReturnSuccess || !command) {
    return kr == kIOReturnSuccess ? kIOReturnError : kr;
  }

  std::uint64_t flags = kIOMemoryDirectionInOut;
  std::uint32_t segmentCount = kRTXMacMaxDmaSegments;
  IOAddressSegment segments[kRTXMacMaxDmaSegments]{};
  kr = command->PrepareForDMA(
      kIODMACommandPrepareForDMANoOptions,
      memory,
      offset,
      length,
      &flags,
      &segmentCount,
      segments);
  if (kr != kIOReturnSuccess) {
    command->release();
    return kr;
  }

  bool valid = segmentCount > 0u && segmentCount <= kRTXMacMaxDmaSegments;
  std::uint64_t covered = 0u;
  for (std::uint32_t i = 0u; valid && i < segmentCount; ++i) {
    const std::uint64_t address = segments[i].address;
    const std::uint64_t bytes = segments[i].length;
    if (bytes == 0u ||
        (address % kRTXMacDmaPageBytes) != 0u ||
        (bytes % kRTXMacDmaPageBytes) != 0u ||
        address > (~std::uint64_t{0}) - (bytes - 1u) ||
        covered > length || bytes > length - covered) {
      valid = false;
      break;
    }
    covered += bytes;
  }
  valid = valid && covered == length;

  if (!valid) {
    (void)command->CompleteDMA(kIODMACommandCompleteDMANoOptions);
    command->release();
    return kIOReturnError;
  }

  out->command = command;
  out->segmentCount = segmentCount;
  out->flags = flags;
  out->offset = offset;
  out->length = length;
  for (std::uint32_t i = 0u; i < segmentCount; ++i) out->segments[i] = segments[i];
  return kIOReturnSuccess;
}

const RTXMacPreparedDmaChunk* FindChunk(
    const RTXMacPreparedDmaBuffer* prepared,
    std::uint64_t offset,
    std::uint64_t bytes) noexcept {
  if (!prepared || !prepared->chunks || bytes == 0u ||
      offset > prepared->length || bytes > prepared->length - offset) {
    return nullptr;
  }
  for (std::uint32_t i = 0u; i < prepared->chunkCount; ++i) {
    const auto& chunk = prepared->chunks[i];
    if (!chunk.command || chunk.length == 0u) return nullptr;
    if (offset >= chunk.offset &&
        offset - chunk.offset <= chunk.length &&
        bytes <= chunk.length - (offset - chunk.offset)) {
      return &chunk;
    }
  }
  return nullptr;
}
} // namespace

kern_return_t RTXMacAllocateAndPrepareDmaBuffer(
    IOPCIDevice* pci,
    std::uint64_t length,
    RTXMacPreparedDmaBuffer* out) noexcept {
  if (!pci || !out || length == 0u ||
      (length % kRTXMacDmaPageBytes) != 0u) {
    return kIOReturnBadArgument;
  }
  ResetBuffer(out);

  if (length > (~std::uint64_t{0}) - (kRTXMacMaxDmaChunkBytes - 1u)) {
    return kIOReturnNoResources;
  }
  const std::uint64_t chunkCount64 =
      (length + kRTXMacMaxDmaChunkBytes - 1u) / kRTXMacMaxDmaChunkBytes;
  if (chunkCount64 == 0u || chunkCount64 > 0xFFFFFFFFull) {
    return kIOReturnNoResources;
  }
  const auto chunkCount = static_cast<std::uint32_t>(chunkCount64);

  IOBufferMemoryDescriptor* memory = nullptr;
  kern_return_t kr = IOBufferMemoryDescriptor::Create(
      kIOMemoryDirectionInOut, length, kRTXMacDmaPageBytes, &memory);
  if (kr != kIOReturnSuccess || !memory) {
    return kr == kIOReturnSuccess ? kIOReturnNoMemory : kr;
  }

  kr = memory->SetLength(length);
  if (kr != kIOReturnSuccess) {
    memory->release();
    return kr;
  }

  auto* chunks = new RTXMacPreparedDmaChunk[chunkCount]();
  if (!chunks) {
    memory->release();
    return kIOReturnNoMemory;
  }

  std::uint32_t preparedCount = 0u;
  for (std::uint32_t i = 0u; i < chunkCount; ++i) {
    const std::uint64_t offset = static_cast<std::uint64_t>(i) * kRTXMacMaxDmaChunkBytes;
    const std::uint64_t remaining = length - offset;
    const std::uint64_t chunkLength =
        remaining < kRTXMacMaxDmaChunkBytes ? remaining : kRTXMacMaxDmaChunkBytes;
    kr = PrepareChunk(pci, memory, offset, chunkLength, &chunks[i]);
    if (kr != kIOReturnSuccess) {
      for (std::uint32_t j = 0u; j < preparedCount; ++j) CompleteChunk(&chunks[j]);
      delete[] chunks;
      memory->release();
      return kr;
    }
    ++preparedCount;
  }

  out->memory = memory;
  out->chunks = chunks;
  out->chunkCount = chunkCount;
  out->length = length;
  return kIOReturnSuccess;
}

void RTXMacReleasePreparedDmaBuffer(RTXMacPreparedDmaBuffer* prepared) noexcept {
  if (!prepared) return;
  if (prepared->chunks) {
    for (std::uint32_t i = 0u; i < prepared->chunkCount; ++i) {
      CompleteChunk(&prepared->chunks[i]);
    }
    delete[] prepared->chunks;
  }
  if (prepared->memory) prepared->memory->release();
  ResetBuffer(prepared);
}

kern_return_t RTXMacCollectDmaPageAddresses(
    const RTXMacPreparedDmaBuffer* prepared,
    std::uint64_t* pageAddresses,
    std::uint32_t pageCapacity,
    std::uint32_t* pageCount) noexcept {
  if (!prepared || !prepared->memory || !prepared->chunks ||
      !pageAddresses || !pageCount || prepared->length == 0u ||
      (prepared->length % kRTXMacDmaPageBytes) != 0u) {
    return kIOReturnBadArgument;
  }

  const std::uint64_t expected64 = prepared->length / kRTXMacDmaPageBytes;
  if (expected64 == 0u || expected64 > 0xFFFFFFFFull || pageCapacity < expected64) {
    return kIOReturnNoResources;
  }
  const auto expected = static_cast<std::uint32_t>(expected64);

  std::uint64_t expectedChunkOffset = 0u;
  std::uint32_t written = 0u;
  for (std::uint32_t c = 0u; c < prepared->chunkCount; ++c) {
    const auto& chunk = prepared->chunks[c];
    if (!chunk.command || chunk.offset != expectedChunkOffset || chunk.length == 0u ||
        (chunk.length % kRTXMacDmaPageBytes) != 0u || chunk.segmentCount == 0u ||
        chunk.segmentCount > kRTXMacMaxDmaSegments) {
      return kIOReturnError;
    }

    std::uint64_t chunkCovered = 0u;
    for (std::uint32_t s = 0u; s < chunk.segmentCount; ++s) {
      const std::uint64_t address = chunk.segments[s].address;
      const std::uint64_t bytes = chunk.segments[s].length;
      if (bytes == 0u ||
          (address % kRTXMacDmaPageBytes) != 0u ||
          (bytes % kRTXMacDmaPageBytes) != 0u ||
          address > (~std::uint64_t{0}) - (bytes - 1u) ||
          chunkCovered > chunk.length || bytes > chunk.length - chunkCovered) {
        return kIOReturnError;
      }
      for (std::uint64_t off = 0u; off < bytes; off += kRTXMacDmaPageBytes) {
        if (written >= expected) return kIOReturnError;
        pageAddresses[written++] = address + off;
      }
      chunkCovered += bytes;
    }

    if (chunkCovered != chunk.length) return kIOReturnError;
    if (expectedChunkOffset > (~std::uint64_t{0}) - chunk.length) return kIOReturnError;
    expectedChunkOffset += chunk.length;
  }

  if (expectedChunkOffset != prepared->length || written != expected) {
    return kIOReturnError;
  }
  *pageCount = written;
  return kIOReturnSuccess;
}

kern_return_t RTXMacCopyIntoPreparedDmaBuffer(
    const RTXMacPreparedDmaBuffer* prepared,
    const void* source,
    std::uint64_t sourceBytes) noexcept {
  if (!prepared || !prepared->memory || !prepared->chunks || !source ||
      sourceBytes != prepared->length || sourceBytes == 0u ||
      sourceBytes > static_cast<std::uint64_t>(~std::size_t{0})) {
    return kIOReturnBadArgument;
  }

  IOAddressSegment range{};
  kern_return_t kr = prepared->memory->GetAddressRange(&range);
  if (kr != kIOReturnSuccess) return kr;
  if (range.address == 0u || range.length < sourceBytes) return kIOReturnNoResources;

  std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(range.address)),
              source, static_cast<std::size_t>(sourceBytes));

  for (std::uint32_t i = 0u; i < prepared->chunkCount; ++i) {
    const auto& chunk = prepared->chunks[i];
    if (!chunk.command || chunk.length == 0u ||
        chunk.offset > prepared->length || chunk.length > prepared->length - chunk.offset) {
      return kIOReturnError;
    }
    // dmaOffset is relative to this command's prepared mapping. dataOffset is
    // the original descriptor's global offset for the same logical bytes.
    kr = chunk.command->PerformOperation(
        kIODMACommandPerformOperationOptionWrite,
        0u,
        chunk.length,
        chunk.offset,
        prepared->memory);
    if (kr != kIOReturnSuccess) return kr;
  }
  return kIOReturnSuccess;
}

kern_return_t RTXMacReadPreparedDmaU32(
    const RTXMacPreparedDmaBuffer* prepared,
    std::uint64_t offset,
    std::uint32_t* value) noexcept {
  if (!value || !prepared || !prepared->memory || (offset & 3u) != 0u) {
    return kIOReturnBadArgument;
  }
  const auto* chunk = FindChunk(prepared, offset, sizeof(std::uint32_t));
  if (!chunk) return kIOReturnNoResources;

  const std::uint64_t localOffset = offset - chunk->offset;
  kern_return_t kr = chunk->command->PerformOperation(
      kIODMACommandPerformOperationOptionRead,
      localOffset,
      sizeof(std::uint32_t),
      offset,
      prepared->memory);
  if (kr != kIOReturnSuccess) return kr;

  IOAddressSegment range{};
  kr = prepared->memory->GetAddressRange(&range);
  if (kr != kIOReturnSuccess) return kr;
  if (range.address == 0u || offset > range.length ||
      sizeof(std::uint32_t) > range.length - offset) {
    return kIOReturnNoResources;
  }

  std::memcpy(value,
              reinterpret_cast<const void*>(
                  static_cast<std::uintptr_t>(range.address + offset)),
              sizeof(*value));
  return kIOReturnSuccess;
}
