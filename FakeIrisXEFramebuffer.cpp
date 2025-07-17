#include "FakeIrisXEFramebuffer.hpp"
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <libkern/c++/OSSymbol.h>
#include <IOKit/IOLib.h>
using namespace libkern;

// New: Render and Media domain FORCEWAKE_ACK registers for Gen11+
#define FORCEWAKE_ACK_RENDER 0x0A188  // Read-only
#define FORCEWAKE_ACK_MEDIA  0x0A18C  // Optional, already used
#define FORCEWAKE_ACK 0x0A188  // This was probably used as generic

#define super IOFramebuffer
OSDefineMetaClassAndStructors(com_example_driver_FakeIrisXEFramebuffer, IOFramebuffer)

// Add private members
IOPCIDevice *pciDevice = nullptr;
IODeviceMemory *bar0 = nullptr;


//probe
IOService *com_example_driver_FakeIrisXEFramebuffer::probe(IOService *provider, SInt32 *score) {
    IOPCIDevice *pdev = OSDynamicCast(IOPCIDevice, provider);
    if (!pdev) return nullptr;

    UInt16 vendor = pdev->configRead16(kIOPCIConfigVendorID);
    UInt16 device = pdev->configRead16(kIOPCIConfigDeviceID);
    if (vendor == 0x8086 && device == 0x9A49) {
        IOLog("FakeIrisXEFramebuffer::probe(): Found matching GPU (8086:9A49)\n");

        if (score) {
            *score += 50000; // force to beat IONDRVFramebuffer
        }

        return this;
    }

    return nullptr;
}


//start
bool com_example_driver_FakeIrisXEFramebuffer::start(IOService* provider) {
    IOLog("FakeIrisXEFramebuffer::start() - Entered\n");

    if (!super::start(provider)) {
        IOLog("FakeIrisXEFramebuffer::start() - super::start() failed\n");
        return false;
    }

    pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice) {
        IOLog("FakeIrisXEFramebuffer::start() - provider is not IOPCIDevice\n");
        return false;
    }

    pciDevice->retain();
    pciDevice->setMemoryEnable(true);

    uint32_t pciCommand = pciDevice->configRead16(kIOPCIConfigCommand);
    IOLog("FakeIrisXEFramebuffer::start() - PCI COMMAND = 0x%04X\n", pciCommand);

    uint8_t pciPower = pciDevice->configRead8(0xD4);
    IOLog("FakeIrisXEFramebuffer::start() - PCI Power Control = 0x%02X\n", pciPower);
    
    
    //ACPI plane walk
    IORegistryEntry *acpiWalker = pciDevice;
    IOLog("🧭 ACPI plane walk from PCI device:\n");

    while ((acpiWalker = acpiWalker->getParentEntry(gIOACPIPlane)) != nullptr) {
        const char* name = acpiWalker->getName();
        const char* location = acpiWalker->getLocation();
        IOLog(" → ACPI node: %s @ %s\n", name ? name : "?", location ? location : "?");

        OSData* adr = OSDynamicCast(OSData, acpiWalker->getProperty("_ADR"));
        if (adr && adr->getLength() == 4) {
            uint32_t adrVal = *(uint32_t*)adr->getBytesNoCopy();
            IOLog("   |_ _ADR = 0x%08X\n", adrVal);
            if (adrVal == 0x00020000) {
                IOLog("✅ Matched _ADR 0x00020000 — this is likely GFX0\n");
            }
        }
    }
    
    // call _DSM
    IOACPIPlatformDevice *acpiDev = nullptr;

    
    //dsm
    IORegistryEntry *parent = pciDevice;
    while ((parent = parent->getParentEntry(gIOACPIPlane)) != nullptr) {
        const char* name = parent->getName();
        const char* location = parent->getLocation();
        IOLog("ACPI Walk: node = %s, location = %s\n", name, location ? location : "null");

        OSData* adr = OSDynamicCast(OSData, parent->getProperty("_ADR"));
        if (adr && adr->getLength() == 4) {
            uint32_t adrVal = *(uint32_t*)adr->getBytesNoCopy();
            IOLog("Found _ADR = 0x%08X\n", adrVal);
            if (adrVal == 0x00020000) {
                acpiDev = OSDynamicCast(IOACPIPlatformDevice, parent);
                break;
            }
        }
    }

    
    // --- Fallback: Manually locate GFX0 via fromPath ---
    if (!acpiDev) {
           IOLog("🔍 Trying fallback path to locate GFX0 via IOACPIPlane\n");
           IORegistryEntry *gfx0Path = IORegistryEntry::fromPath("/_SB/PC00/GFX0", gIOACPIPlane);
           if (gfx0Path) {
               IOLog("✅ Found GFX0 via fromPath fallback\n");
               acpiDev = OSDynamicCast(IOACPIPlatformDevice, gfx0Path);
               if (acpiDev) {
                   IOLog("✅ Found GFX0 as IOACPIPlatformDevice via fallback\n");
               } else {
                   IOLog("❌ Fallback: GFX0 is not IOACPIPlatformDevice\n");
               }
           } else {
               IOLog("❌ Fallback: GFX0 path not found in IOACPIPlane\n");
           }
       }

    if (acpiDev) {
        IOLog("✅ FakeIrisXEFramebuffer::start() - Found ACPI parent: %s\n", acpiDev->getName());

        // _DSM eval block
        OSObject *params[4];
        uint8_t uuid[16] = { 0xA0, 0x12, 0x93, 0x6E, 0x50, 0x9A, 0x4C, 0x5B,
                             0x8A, 0x21, 0x3A, 0x36, 0x15, 0x29, 0x2C, 0x79 };
        params[0] = OSData::withBytes(uuid, sizeof(uuid));
        params[1] = OSNumber::withNumber(0ULL, 32);
        params[2] = OSNumber::withNumber(1ULL, 32);
        params[3] = OSArray::withCapacity(0);

        OSObject *dsmResult = nullptr;
        IOReturn ret = acpiDev->evaluateObject("_DSM", &dsmResult, params, 4, 0);
        if (ret == kIOReturnSuccess && dsmResult) {
            IOLog("✅ FakeIrisXEFramebuffer::_DSM OK: %s\n", dsmResult->getMetaClass()->getClassName());
            dsmResult->release();
        } else {
            IOLog("❌ FakeIrisXEFramebuffer::_DSM failed: 0x%x\n", ret);
        }

        for (int i = 0; i < 4; i++) if (params[i]) params[i]->release();
    } else {
        IOLog("❌ FakeIrisXEFramebuffer::start() - GFX0 ACPI parent not found\n");
    }



    //BAR0 mapping
    IOMemoryMap *mmioMap = pciDevice->mapDeviceMemoryWithIndex(0);
    if (!mmioMap) {
        IOLog("FakeIrisXEFramebuffer::start() - BAR0 mapping failed\n");
        return false;
    }

    IOLog("FakeIrisXEFramebuffer::start() - BAR0 OK, length = %llu\n", mmioMap->getLength());
    volatile uint32_t *mmio = (volatile uint32_t *) mmioMap->getVirtualAddress();
    IOLog("FakeIrisXEFramebuffer::start() - Mapped MMIO VA: %p\n", mmio);

    // --- PCI fix ---
    uint16_t pmcsr = pciDevice->configRead16(0x84);
    IOLog("FakeIrisXEFramebuffer::start() - PCI PMCSR before = 0x%04X\n", pmcsr);
    pmcsr &= ~0x3; // Force D0
    pciDevice->configWrite16(0x84, pmcsr);
    IOSleep(5);
    pmcsr = pciDevice->configRead16(0x84);
    IOLog("FakeIrisXEFramebuffer::start() - PCI PMCSR after force = 0x%04X\n", pmcsr);

    // --- New safe GT power + forcewake ---
    const uint32_t FORCEWAKE_MT = 0xA188;
   // const uint32_t FORCEWAKE_ACK = 0x130040;
    // const uint32_t FORCEWAKE_ACK_MEDIA = 0x130044;
    const uint32_t GT_PG_ENABLE = 0xA218;
    //const uint32_t PWR_WELL_CTL = 0x45400;
    
    
    
//GT_PG_ENABLE
    uint32_t pg_enable = mmio[GT_PG_ENABLE / 4];
    IOLog("GT_PG_ENABLE before: 0x%08X\n", pg_enable);
    mmio[GT_PG_ENABLE / 4] = pg_enable & ~0x1;
    IOSleep(5);
    uint32_t pg_enable_after = mmio[GT_PG_ENABLE / 4];
    IOLog("GT_PG_ENABLE after: 0x%08X\n", pg_enable_after);
    
    // Read FUSE_CTRL
    uint32_t fuse_ctrl = mmio[0x42000 / 4];
    IOLog("FakeIrisXEFramebuffer::start() - FUSE_CTRL before: 0x%08X\n", fuse_ctrl);

    // Try force bit 0 ON
    mmio[0x42000 / 4] = fuse_ctrl | 0x1;
    IOSleep(10);
    uint32_t fuse_ctrl_after = mmio[0x42000 / 4];
    IOLog("FakeIrisXEFramebuffer::start() - FUSE_CTRL after: 0x%08X\n", fuse_ctrl_after);

    // 1. PUNIT handshake: disable GT power gating
    const uint32_t PUNIT_PG_CTRL = 0xA2B0;
    uint32_t punit_pg = mmio[PUNIT_PG_CTRL / 4];
    IOLog("FakeIrisXEFramebuffer::start() - PUNIT_PG_CTRL before: 0x%08X\n", punit_pg);
    
    
    // ✅ Fix: Disable GT power gating by clearing bit 31
    punit_pg &= ~0x80000000;
    mmio[PUNIT_PG_CTRL / 4] = punit_pg;
    IOSleep(10);

    uint32_t punit_pg_after = mmio[PUNIT_PG_CTRL / 4];
    IOLog("FakeIrisXEFramebuffer::start() - PUNIT_PG_CTRL after: 0x%08X\n", punit_pg_after);
    


    // 2. Render Power Well ON
    const uint32_t PWR_WELL_CTL = 0x45400;
    uint32_t pw_ctl = mmio[PWR_WELL_CTL / 4];
    IOLog("FakeIrisXEFramebuffer::start() - Forcing Render PWR_WELL_CTL ON: before: 0x%08X\n", pw_ctl);

    // Set BIT(1) = Render Well
    mmio[PWR_WELL_CTL / 4] = pw_ctl | 0x2;
    IOSleep(10);
    
    // Enable Power Well 2 also
    mmio[PWR_WELL_CTL / 4] |= 0x4; // Bit 2 = PW2
    IOSleep(10);
    IOLog("PWR_WELL_CTL now: 0x%08X\n", mmio[PWR_WELL_CTL / 4]);

    uint32_t pw_ctl_after = mmio[PWR_WELL_CTL / 4];
    IOLog("FakeIrisXEFramebuffer::start() - Forcing Render PWR_WELL_CTL ON: after: 0x%08X\n", pw_ctl_after);

    // GT0: Wait for power well to be fully ON
    uint32_t pw_status = mmio[0x45408 / 4];
    IOLog("PWR_WELL_CTL_STATUS = 0x%08X\n", pw_status);
    
    // GEN11 render power domain: try unlocking manually
    const uint32_t GEN11_PWR_DOMAIN_MASK = 0x10000;
    mmio[0xA278 / 4] |= GEN11_PWR_DOMAIN_MASK;
    IOSleep(10);

    // Enable GT thread dispatch (test)
    mmio[0x138128 / 4] = 0x00000001;
    IOSleep(5);

    
    IOLog("Trying FORCEWAKE Render domain\n");
    // Try FORCEWAKE_ALL
    mmio[FORCEWAKE_MT / 4] = 0x000F000F;  // all domains
    IOSleep(10);
    uint32_t ack_all = mmio[FORCEWAKE_ACK / 4];
    IOLog("FORCEWAKE_ACK (Global): 0x%08X\n", ack_all);


    if ((ack_all & 0x1) == 0) {
        IOLog("Trying FORCEWAKE Media domain\n");
        mmio[FORCEWAKE_MT / 4] = 0x00020002;
        IOSleep(5);
        uint32_t media_ack = mmio[FORCEWAKE_ACK_MEDIA / 4];
        IOLog("FORCEWAKE_ACK (Media): 0x%08X\n", media_ack);
    }
    
    // New: try force dummy read to latch Render domain
    IOLog("Trying dummy read after FORCEWAKE Render write\n");
    volatile uint32_t dummy = mmio[FORCEWAKE_MT / 4];
    IOSleep(5);
    uint32_t ack_force = mmio[FORCEWAKE_ACK / 4];
    IOLog("FORCEWAKE_ACK (Render) after dummy read: 0x%08X\n", ack_force);

    
    
    //  Try to ping Render domain again with loop
    IOLog("Re-trying FORCEWAKE Render domain after Media wake\n");
    mmio[FORCEWAKE_MT / 4] = 0x00010001;

    for (int i = 0; i < 1000; ++i) {
        uint32_t ack = mmio[FORCEWAKE_ACK / 4];
        if (ack & 0x1) {
            IOLog("FORCEWAKE_ACK (Render) now set! 0x%08X\n", ack);
            break;
        }
        IOSleep(1);
        if (i == 999) {
            IOLog("FORCEWAKE_ACK (Render) still not set, final: 0x%08X\n", ack);
        }
    }
    
    // Extra: Poke GT Thread Status to nudge Render domain awake
    const uint32_t GT_THREAD_STATUS = 0x138124;
    uint32_t gt_thread = mmio[GT_THREAD_STATUS / 4];
    IOLog("GT_THREAD_STATUS before poke: 0x%08X\n", gt_thread);

    // Sometimes poking the first bit wakes up Render domain
    mmio[GT_THREAD_STATUS / 4] = gt_thread | 0x1;
    IOSleep(5);
    uint32_t gt_thread_after = mmio[GT_THREAD_STATUS / 4];
    IOLog("GT_THREAD_STATUS after poke: 0x%08X\n", gt_thread_after);

    // Re-read FORCEWAKE_ACK Render
    uint32_t ack_retry = mmio[FORCEWAKE_ACK / 4];
    IOLog("FORCEWAKE_ACK (Render) after GT poke: 0x%08X\n", ack_retry);

    uint32_t mmio_test = mmio[0];
    IOLog("First DWORD after handshake: 0x%08X\n", mmio_test);
    
    
    // -------------------------------------------------------------
    // 🧪 Try FORCEWAKE_REQ as alternate to FORCEWAKE_MT
    // -------------------------------------------------------------
    IOLog("→ Trying FORCEWAKE_REQ for Render domain\n");
    mmio[0xA188 / 4] = 0x00010001;  // FORCEWAKE_REQ register for Render
    IOSleep(10);
    uint32_t ackRenderREQ = mmio[FORCEWAKE_ACK_RENDER / 4];
    IOLog("→ FORCEWAKE_ACK (Render) after REQ write: 0x%08X\n", ackRenderREQ);

    // -------------------------------------------------------------
    // 🧪 Try legacy FORCEWAKE registers (pre-Gen9 compatibility)
    // -------------------------------------------------------------
    IOLog("→ Trying legacy FORCEWAKE request\n");
    mmio[0xA008 / 4] = 0x00010001;  // Legacy FORCEWAKE Request
    IOSleep(10);
    uint32_t legacyAck = mmio[0xA00C / 4];
    IOLog("→ Legacy FORCEWAKE_ACK: 0x%08X\n", legacyAck);


    // Example: dump small MMIO window
    for (uint32_t offset = 0; offset < 0x40; offset += 4) {
        uint32_t val = mmio[offset / 4];
        IOLog("MMIO[0x%04X] = 0x%08X\n", offset, val);
    }
    
    
    // ---------- EXTRA: Attempt to unlock GT domains ----------

    // 1. Disable GT Clock Gating (optional but safe)
    const uint32_t GT_CLOCK_GATE_DISABLE = 0x09400;
    mmio[GT_CLOCK_GATE_DISABLE / 4] = 0xFFFFFFFF;
    IOLog("GT_CLOCK_GATE_DISABLE set\n");

    // 2. Disable RC6 sleep states (optional, may help bring up Render)
    const uint32_t RC6_CONTROL = 0x08500;
    mmio[RC6_CONTROL / 4] = 0;
    IOLog("RC6_CONTROL cleared (RC6 disabled)\n");

    // 3. Dump ECOBUS (debug sanity check)
    const uint32_t ECOBUS = 0x0A180;
    uint32_t ecobus = mmio[ECOBUS / 4];
    IOLog("ECOBUS = 0x%08X\n", ecobus);


    registerService();
    IOLog("FakeIrisXEFramebuffer::start() - Completed\n");
    return true;
}


    

void com_example_driver_FakeIrisXEFramebuffer::stop(IOService* provider) {
    IOLog("FakeIrisXEFramebuffer::stop() called\n");
    super::stop(provider);
}

IOReturn com_example_driver_FakeIrisXEFramebuffer::enableController() {
    IOLog("enableController() called\n");
    return kIOReturnSuccess;
}

IODeviceMemory* com_example_driver_FakeIrisXEFramebuffer::getApertureRange(IOPixelAperture aperture) {
    IOLog("getApertureRange() called\n");

    IOPCIDevice *pciDevice = OSDynamicCast(IOPCIDevice, getProvider());
    if (pciDevice) {
        pciDevice->setMemoryEnable(true);

        IODeviceMemory *barMemory = pciDevice->getDeviceMemoryWithIndex(0);
        if (barMemory) {
            IOLog("FakeIrisXEFramebuffer::getApertureRange() - Found BAR0 device memory\n");
            return barMemory;
        } else {
            IOLog("FakeIrisXEFramebuffer::getApertureRange() - Failed to get BAR0 device memory\n");
        }
    } else {
        IOLog("FakeIrisXEFramebuffer::getApertureRange() - No PCI device found\n");
    }

    return nullptr;
}



const char* com_example_driver_FakeIrisXEFramebuffer::getPixelFormats(void) {
    IOLog("getPixelFormats() called\n");
    return IO32BitDirectPixels "\0";
}

IOItemCount com_example_driver_FakeIrisXEFramebuffer::getDisplayModeCount(void) {
    IOLog("getDisplayModeCount() called\n");
    return 1;
}

IOReturn com_example_driver_FakeIrisXEFramebuffer::getDisplayModes(IODisplayModeID* allDisplayModes) {
    IOLog("getDisplayModes() called\n");
    if (allDisplayModes)
        allDisplayModes[0] = 1;
    return kIOReturnSuccess;
}

UInt64 com_example_driver_FakeIrisXEFramebuffer::getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth) {
    IOLog("getPixelFormatsForDisplayMode() called\n");
    return 0; // obsolete — always return 0
}

IOReturn com_example_driver_FakeIrisXEFramebuffer::getPixelInformation(IODisplayModeID displayMode,
                                                                       IOIndex depth,
                                                                       IOPixelAperture aperture,
                                                                       IOPixelInformation* info) {
    IOLog("getPixelInformation() called\n");
    if (!info) return kIOReturnBadArgument;

    bzero(info, sizeof(IOPixelInformation));
    strlcpy(info->pixelFormat, IO32BitDirectPixels, sizeof(info->pixelFormat));
    info->flags = 0;
    info->activeWidth = 1920;
    info->activeHeight = 1080;
    info->bytesPerRow = 1920 * 4;
    info->bitsPerPixel = 32;
    info->componentCount = 3;
    info->bitsPerComponent = 8;
    return kIOReturnSuccess;
}

IOReturn com_example_driver_FakeIrisXEFramebuffer::getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth) {
    IOLog("getCurrentDisplayMode() called\n");
    if (displayMode) *displayMode = 1;
    if (depth) *depth = 0;
    return kIOReturnSuccess;
}

IOReturn com_example_driver_FakeIrisXEFramebuffer::getInformationForDisplayMode(IODisplayModeID displayMode,
                                                                                IODisplayModeInformation* info) {
    IOLog("getInformationForDisplayMode() called\n");
    if (!info) return kIOReturnBadArgument;

    info->nominalWidth = 1920;
    info->nominalHeight = 1080;
    info->refreshRate = 60 << 16;
    info->maxDepthIndex = 0;
    info->flags = 0;
    return kIOReturnSuccess;
}

#include <libkern/libkern.h> // For kern_return_t, kmod_info_t

// Kext entry/exit points
extern "C" kern_return_t FakeIrisXEFramebuffer_start(kmod_info_t *ki, void *data) {
    return KERN_SUCCESS;
}

extern "C" kern_return_t FakeIrisXEFramebuffer_stop(kmod_info_t *ki, void *data) {
    return KERN_SUCCESS;
}
