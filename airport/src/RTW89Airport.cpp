// SPDX-License-Identifier: GPL-2.0-or-later
#include "RTW89Airport.hpp"

#define super IO80211Controller
OSDefineMetaClassAndStructors(RTW89Airport, IO80211Controller)
OSDefineMetaClassAndStructors(RTW89AirportInterface, IO80211Interface)

#define LOG(fmt, ...) IOLog("RTW89Airport: " fmt, ##__VA_ARGS__)

bool RTW89Airport::init(OSDictionary *properties)
{
	if (!super::init(properties)) return false;
	fWorkLoop = nullptr;
	fNetIf = nullptr;
	fMac = { { 0x02, 0x00, 0x00, 0x89, 0x52, 0x01 } };	/* placeholder until the daemon supplies the real address */
	return true;
}

void RTW89Airport::free()
{
	if (fWorkLoop) { fWorkLoop->release(); fWorkLoop = nullptr; }
	super::free();
}

bool RTW89Airport::createWorkLoop()
{
	if (!fWorkLoop) fWorkLoop = IO80211WorkLoop::workLoop();
	return fWorkLoop != nullptr;
}

IOWorkLoop *RTW89Airport::getWorkLoop() const { return fWorkLoop; }

bool RTW89Airport::start(IOService *provider)
{
	LOG("start\n");
	if (!super::start(provider)) { LOG("super::start failed\n"); return false; }
	if (!attachInterface((IONetworkInterface **)&fNetIf, true)) {
		LOG("attachInterface failed\n");
		super::stop(provider);
		return false;
	}
	setLinkStatus(kIONetworkLinkValid);
	registerService();
	if (fNetIf) fNetIf->registerService();
	LOG("interface attached\n");
	return true;
}

void RTW89Airport::stop(IOService *provider)
{
	LOG("stop\n");
	if (fNetIf) { detachInterface(fNetIf, true); fNetIf = nullptr; }
	super::stop(provider);
}

IONetworkInterface *RTW89Airport::createInterface()
{
	RTW89AirportInterface *netif = new RTW89AirportInterface;
	if (!netif) return nullptr;
	if (!netif->init(this)) { netif->release(); return nullptr; }
	return netif;
}

bool RTW89Airport::configureInterface(IONetworkInterface *netif)
{
	return super::configureInterface(netif);
}

IOReturn RTW89Airport::enable(IONetworkInterface *netif) { LOG("enable\n"); return kIOReturnSuccess; }
IOReturn RTW89Airport::disable(IONetworkInterface *netif) { LOG("disable\n"); return kIOReturnSuccess; }

IOReturn RTW89Airport::getHardwareAddress(IOEthernetAddress *addr) { *addr = fMac; return kIOReturnSuccess; }
IOReturn RTW89Airport::getHardwareAddressForInterface(IO80211Interface *netif, IOEthernetAddress *addr) { *addr = fMac; return kIOReturnSuccess; }

UInt32 RTW89Airport::outputPacket(mbuf_t m, void *param)
{
	freePacket(m);
	return kIOReturnOutputDropped;
}

SInt32 RTW89Airport::apple80211Request(unsigned int request_type, int request_number, IO80211Interface *interface, void *data)
{
	return kIOReturnUnsupported;
}

SInt32 RTW89Airport::stopDMA() { return 0; }
UInt32 RTW89Airport::hardwareOutputQueueDepth(IO80211Interface *interface) { return 0; }
SInt32 RTW89Airport::performCountryCodeOperation(IO80211Interface *interface, IO80211CountryCodeOp op) { return 0; }
SInt32 RTW89Airport::enableFeature(IO80211FeatureCode code, void *data) { return code == kIO80211Feature80211n ? 0 : 102; }

SInt32 RTW89Airport::monitorModeSetEnabled(IO80211Interface *interface, bool enabled, UInt32 dlt) { return kIOReturnUnsupported; }

const OSString *RTW89Airport::newVendorString() const { return OSString::withCString("Realtek"); }
const OSString *RTW89Airport::newModelString() const { return OSString::withCString("RTL8852B (rtw89d)"); }
