#include "RTXMacUserClient.h"

#include "RTXMacDriver.h"
#include "rtxmac/boot_package.hpp"
#include "rtxmac/boot_package_policy.hpp"

#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/OSData.h>
#include <PCIDriverKit/PCIDriverKit.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace {
constexpr std::uint64_t kMaxPackageBytes = 128ull * 1024ull * 1024ull;
constexpr std::uint32_t kStatusScalarCount = 8u;

enum Selector : std::uint64_t {
  kValidatePackage = 0u,
  kGetValidationStatus = 1u,
  kSelectorCount = 2u,
};

struct ValidationSnapshot {
  bool hasResult{};
  bool accepted{};
  std::uint32_t parseStatus{};
  std::uint32_t semanticFailure{};
  std::uint64_t packageBytes{};
  std::uint64_t liveIdentity{};
  std::uint64_t packageIdentity{};
};

struct InputView {
  const std::uint8_t* bytes{nullptr};
  std::size_t size{};
  IOMemoryMap* map{nullptr};
};

void ReleaseInput(InputView* input) noexcept {
  if (!input) return;
  if (input->map) input->map->release();
  input->bytes = nullptr;
  input->size = 0u;
  input->map = nullptr;
}

std::uint64_t PackIdentity(std::uint16_t vendor,
                           std::uint16_t device,
                           std::uint16_t subsystemVendor,
                           std::uint16_t subsystemDevice) noexcept {
  return static_cast<std::uint64_t>(vendor) |
      (static_cast<std::uint64_t>(device) << 16u) |
      (static_cast<std::uint64_t>(subsystemVendor) << 32u) |
      (static_cast<std::uint64_t>(subsystemDevice) << 48u);
}

kern_return_t MakeInputView(IOUserClientMethodArguments* arguments,
                            InputView* out) noexcept {
  if (!arguments || !out) return kIOReturnBadArgument;
  *out = {};
  if (arguments->structureInput && arguments->structureInputDescriptor) {
    return kIOReturnBadArgument;
  }

  if (arguments->structureInput) {
    const std::uint64_t length = arguments->structureInput->getLength();
    if (length == 0u || length > kMaxPackageBytes ||
        length > std::numeric_limits<std::size_t>::max()) {
      return kIOReturnBadArgument;
    }
    const void* ptr = arguments->structureInput->getBytesNoCopy();
    if (!ptr) return kIOReturnNoResources;
    out->bytes = static_cast<const std::uint8_t*>(ptr);
    out->size = static_cast<std::size_t>(length);
    return kIOReturnSuccess;
  }

  IOMemoryDescriptor* descriptor = arguments->structureInputDescriptor;
  if (!descriptor) return kIOReturnBadArgument;
  const std::uint64_t length = descriptor->GetLength();
  if (length == 0u || length > kMaxPackageBytes ||
      length > std::numeric_limits<std::size_t>::max()) {
    return kIOReturnBadArgument;
  }

  IOMemoryMap* map = nullptr;
  kern_return_t kr = descriptor->CreateMapping(0, 0, 0, length, 0, &map);
  if (kr != kIOReturnSuccess || !map) {
    return kr == kIOReturnSuccess ? kIOReturnNoResources : kr;
  }
  const auto address = static_cast<std::uintptr_t>(map->GetAddress());
  if (address == 0u) {
    map->release();
    return kIOReturnNoResources;
  }
  out->bytes = reinterpret_cast<const std::uint8_t*>(address);
  out->size = static_cast<std::size_t>(length);
  out->map = map;
  return kIOReturnSuccess;
}

void WriteStatus(const ValidationSnapshot& snapshot,
                 IOUserClientMethodArguments* arguments) noexcept {
  if (!arguments || !arguments->scalarOutput ||
      arguments->scalarOutputCount < kStatusScalarCount) return;
  arguments->scalarOutput[0] = snapshot.hasResult ? 1u : 0u;
  arguments->scalarOutput[1] = snapshot.accepted ? 1u : 0u;
  arguments->scalarOutput[2] = snapshot.parseStatus;
  arguments->scalarOutput[3] = snapshot.semanticFailure;
  arguments->scalarOutput[4] = snapshot.packageBytes;
  arguments->scalarOutput[5] = snapshot.liveIdentity;
  arguments->scalarOutput[6] = snapshot.packageIdentity;
  arguments->scalarOutput[7] = kMaxPackageBytes;
}

kern_return_t ValidateAction(OSObject* target,
                             void*,
                             IOUserClientMethodArguments* arguments) {
  return static_cast<RTXMacUserClient*>(target)->ValidatePackage(arguments);
}

kern_return_t StatusAction(OSObject* target,
                           void*,
                           IOUserClientMethodArguments* arguments) {
  return static_cast<RTXMacUserClient*>(target)->GetValidationStatus(arguments);
}

const IOUserClientMethodDispatch kDispatch[kSelectorCount] = {
    {ValidateAction, false, 0u, kIOUserClientVariableStructureSize,
     kStatusScalarCount, 0u},
    {StatusAction, false, 0u, 0u, kStatusScalarCount, 0u},
};
} // namespace

struct RTXMacUserClient_IVars {
  RTXMacDriver* driver{nullptr};
  ValidationSnapshot validation{};
};

bool RTXMacUserClient::init() {
  if (!super::init()) return false;
  ivars = new RTXMacUserClient_IVars();
  return ivars != nullptr;
}

void RTXMacUserClient::free() {
  IOSafeDeleteNULL(ivars, RTXMacUserClient_IVars, 1);
  super::free();
}

kern_return_t RTXMacUserClient::Start_Impl(IOService* provider) {
  kern_return_t kr = Start(provider, SUPERDISPATCH);
  if (kr != kIOReturnSuccess) return kr;
  ivars->driver = OSDynamicCast(RTXMacDriver, provider);
  if (!ivars->driver) {
    Stop(provider, SUPERDISPATCH);
    return kIOReturnBadArgument;
  }
  kr = RegisterService();
  if (kr != kIOReturnSuccess) {
    ivars->driver = nullptr;
    Stop(provider, SUPERDISPATCH);
  }
  return kr;
}

kern_return_t RTXMacUserClient::Stop_Impl(IOService* provider) {
  ivars->driver = nullptr;
  return Stop(provider, SUPERDISPATCH);
}

kern_return_t RTXMacUserClient::ExternalMethod(
    std::uint64_t selector,
    IOUserClientMethodArguments* arguments,
    const IOUserClientMethodDispatch*,
    OSObject*,
    void*) {
  if (!ivars || !ivars->driver || !arguments || selector >= kSelectorCount) {
    return kIOReturnUnsupported;
  }
  return IOUserClient::ExternalMethod(
      selector, arguments, &kDispatch[selector], this, nullptr);
}

kern_return_t RTXMacUserClient::ValidatePackage(
    IOUserClientMethodArguments* arguments) {
  using namespace rtxmac::nvidia::package;
  if (!ivars || !ivars->driver || !arguments) return kIOReturnNotReady;

  InputView input{};
  const kern_return_t inputKr = MakeInputView(arguments, &input);
  if (inputKr != kIOReturnSuccess) return inputKr;

  ValidationSnapshot snapshot{};
  snapshot.hasResult = true;
  snapshot.packageBytes = input.size;
  const std::span<const std::uint8_t> bytes(input.bytes, input.size);
  const PackageView view = ParseAndVerify(bytes);
  snapshot.parseStatus = static_cast<std::uint32_t>(view.status);
  if (view.status == ParseStatus::Ok) {
    snapshot.packageIdentity = PackIdentity(
        view.metadata.pci.vendor, view.metadata.pci.device,
        view.metadata.pci.subsystemVendor, view.metadata.pci.subsystemDevice);
    const SemanticReport semantic = CheckGa10xPackageSemantics(bytes, view);
    snapshot.semanticFailure = static_cast<std::uint32_t>(semantic.failure);
    if (semantic.valid) {
      IOPCIDevice* pci = ivars->driver->GetPCI();
      if (pci) {
        std::uint16_t vendor = 0xffffu, device = 0xffffu;
        std::uint16_t subsystemVendor = 0xffffu, subsystemDevice = 0xffffu;
        pci->ConfigurationRead16(kIOPCIConfigurationOffsetVendorID, &vendor);
        pci->ConfigurationRead16(kIOPCIConfigurationOffsetDeviceID, &device);
        pci->ConfigurationRead16(0x2cu, &subsystemVendor);
        pci->ConfigurationRead16(0x2eu, &subsystemDevice);
        snapshot.liveIdentity = PackIdentity(
            vendor, device, subsystemVendor, subsystemDevice);
        snapshot.accepted = snapshot.liveIdentity == snapshot.packageIdentity;
      }
    }
  }

  ivars->validation = snapshot;
  WriteStatus(ivars->validation, arguments);
  ReleaseInput(&input);
  return kIOReturnSuccess;
}

kern_return_t RTXMacUserClient::GetValidationStatus(
    IOUserClientMethodArguments* arguments) {
  if (!ivars || !arguments) return kIOReturnNotReady;
  WriteStatus(ivars->validation, arguments);
  return kIOReturnSuccess;
}
