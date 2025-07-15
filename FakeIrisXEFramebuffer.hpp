#ifndef _FAKE_IRIS_XE_FRAMEBUFFER_HPP_
#define _FAKE_IRIS_XE_FRAMEBUFFER_HPP_

#include <IOKit/graphics/IOFramebuffer.h>

class com_example_driver_FakeIrisXEFramebuffer : public IOFramebuffer {
    OSDeclareDefaultStructors(com_example_driver_FakeIrisXEFramebuffer)
    
private:
    //IOPCIDevice *pciDevice = nullptr;   // ✅ This is correct place
    IODeviceMemory *bar0 = nullptr;


 public:
    bool start(IOService* provider) override;
    void stop(IOService* provider) override;
    
    virtual IOService *probe(IOService *provider, SInt32 *score) override;


    IOReturn enableController() override;

    IODeviceMemory* getApertureRange(IOPixelAperture aperture) override;
    const char* getPixelFormats(void) override;
    IOItemCount getDisplayModeCount(void) override;
    IOReturn getDisplayModes(IODisplayModeID* allDisplayModes) override;
    UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;

    IOReturn getPixelInformation(IODisplayModeID displayMode,
                                 IOIndex depth,
                                 IOPixelAperture aperture,
                                 IOPixelInformation* info) override;

    IOReturn getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth) override;
    IOReturn getInformationForDisplayMode(IODisplayModeID displayMode,
                                          IODisplayModeInformation* info) override;
};

#endif /* _FAKE_IRIS_XE_FRAMEBUFFER_HPP_ */
