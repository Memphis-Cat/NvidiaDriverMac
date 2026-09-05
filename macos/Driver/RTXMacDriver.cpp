#include "RTXMacDriver.h"

#include <DriverKit/IOLib.h>
#include <PCIDriverKit/PCIDriverKit.h>

struct RTXMacDriver_IVars {
  IOPCIDevice* pci{nullptr};
};

bool RTXMacDriver::init() {
  if (!super::init()) return false;
  ivars = new RTXMacDriver_IVars();
  return ivars != nullptr;
}

void RTXMacDriver::free() {
  IOSafeDeleteNULL(ivars, RTXMacDriver_IVars, 1);
  super::free();
}

kern_return_t RTXMacDriver::Start_Impl(IOService* provider) {
  kern_return_t kr = Start(provider, SUPERDISPATCH);
  if (kr != kIOReturnSuccess) return kr;

  ivars->pci = OSDynamicCast(IOPCIDevice, provider);
  if (!ivars->pci) return kIOReturnNoDevice;

  kr = ivars->pci->Open(this, 0);
  if (kr != kIOReturnSuccess) {
    ivars->pci = nullptr;
    return kr;
  }

  uint16_t vendor = 0, device = 0, subsystemVendor = 0, subsystemDevice = 0;
  uint8_t revision = 0;
  ivars->pci->ConfigurationRead16(kIOPCIConfigurationOffsetVendorID, &vendor);
  ivars->pci->ConfigurationRead16(kIOPCIConfigurationOffsetDeviceID, &device);
  ivars->pci->ConfigurationRead8(kIOPCIConfigurationOffsetRevisionID, &revision);
  ivars->pci->ConfigurationRead16(0x2C, &subsystemVendor);
  ivars->pci->ConfigurationRead16(0x2E, &subsystemDevice);

  os_log(OS_LOG_DEFAULT,
         "rtxmac: PCI %04x:%04x subsystem %04x:%04x rev %02x",
         vendor, device, subsystemVendor, subsystemDevice, revision);

  for (uint8_t bar = 0; bar < 6; ++bar) {
    uint8_t memoryIndex = 0, memoryType = 0;
    uint64_t memorySize = 0;
    const kern_return_t barKr = ivars->pci->GetBARInfo(bar, &memoryIndex, &memorySize, &memoryType);
    if (barKr == kIOReturnSuccess) {
      os_log(OS_LOG_DEFAULT,
             "rtxmac: BAR%u index=%u size=0x%llx type=%u",
             bar, memoryIndex, memorySize, memoryType);
    }
  }

  RegisterService();
  return kIOReturnSuccess;
}

kern_return_t RTXMacDriver::Stop_Impl(IOService* provider) {
  if (ivars && ivars->pci) {
    ivars->pci->Close(this, 0);
    ivars->pci = nullptr;
  }
  return Stop(provider, SUPERDISPATCH);
}

IOPCIDevice* RTXMacDriver::GetPCI() { return ivars ? ivars->pci : nullptr; }
