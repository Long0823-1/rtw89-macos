// SPDX-License-Identifier: BSD-3-Clause
// Placeholder driver for the TX20U's virtual-CD interface (0bda:1a2b). It outbids IOUSBMassStorageDriver so macOS never
// binds the mass-storage stack there and the user-space daemon can open the interface and send the switching command.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <mach/kmod.h>

extern "C" {
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(local.rtw89.hold, "1.0.0", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
}

class RTW89Hold : public IOService {
	OSDeclareDefaultStructors(RTW89Hold)
public:
	bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(RTW89Hold, IOService)

bool RTW89Hold::start(IOService *provider)
{
	if (!IOService::start(provider)) return false;
	setProperty("RTW89HoldInterface", kOSBooleanTrue);
	registerService();
	return true;
}
