// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal IO80211 (legacy family) controller that presents a Wi-Fi interface to macOS and will forward requests to rtw89d.
#pragma once
#include <IOKit/network/IOOutputQueue.h>
#include "Airport/Apple80211.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOCommandGate.h>
#include <libkern/c++/OSString.h>

class RTW89AirportInterface : public IO80211Interface {
	OSDeclareDefaultStructors(RTW89AirportInterface)
};

class RTW89Airport : public IO80211Controller {
	OSDeclareDefaultStructors(RTW89Airport)
public:
	virtual bool init(OSDictionary *properties) override;
	virtual void free() override;
	virtual bool start(IOService *provider) override;
	virtual void stop(IOService *provider) override;
	virtual bool createWorkLoop() override;
	virtual IOWorkLoop *getWorkLoop() const override;
	virtual IONetworkInterface *createInterface() override;
	virtual bool configureInterface(IONetworkInterface *netif) override;
	virtual IOReturn enable(IONetworkInterface *netif) override;
	virtual IOReturn disable(IONetworkInterface *netif) override;
	virtual IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
	virtual IOReturn getHardwareAddressForInterface(IO80211Interface *netif, IOEthernetAddress *addr) override;
	virtual UInt32 outputPacket(mbuf_t m, void *param) override;
	virtual SInt32 apple80211Request(unsigned int request_type, int request_number, IO80211Interface *interface, void *data) override;
	virtual SInt32 stopDMA() override;
	virtual UInt32 hardwareOutputQueueDepth(IO80211Interface *interface) override;
	virtual SInt32 performCountryCodeOperation(IO80211Interface *interface, IO80211CountryCodeOp op) override;
	virtual SInt32 enableFeature(IO80211FeatureCode code, void *data) override;
	virtual SInt32 monitorModeSetEnabled(IO80211Interface *interface, bool enabled, UInt32 dlt) override;
	virtual const OSString *newVendorString() const override;
	virtual const OSString *newModelString() const override;

	SInt32 apple80211RequestImpl(unsigned int request_type, int request_number, IO80211Interface *interface, void *data);
	void noteRequest(unsigned type, int num, bool handled);

private:
	UInt32 fReqCount[2][256];
	UInt32 fReqUnsup[2][256];
	UInt32 fPower;
	IOWorkLoop *fWorkLoop;
	IO80211Interface *fNetIf;
	IOEthernetAddress fMac;
};
