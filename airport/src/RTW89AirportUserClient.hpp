// SPDX-License-Identifier: GPL-2.0-or-later
// User-space channel to rtw89d: packets leaving en3 go to a shared queue, received frames come in through an external method.
#pragma once
#include <IOKit/IOUserClient.h>
#include <IOKit/IOSharedDataQueue.h>

class RTW89Airport;

class RTW89AirportUserClient : public IOUserClient {
	OSDeclareDefaultStructors(RTW89AirportUserClient)
public:
	virtual bool initWithTask(task_t owningTask, void *securityID, UInt32 type) override;
	virtual bool start(IOService *provider) override;
	virtual void stop(IOService *provider) override;
	virtual void free() override;
	virtual IOReturn clientClose() override;
	virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory) override;
	virtual IOReturn registerNotificationPort(mach_port_t port, UInt32 type, UInt32 refCon) override;
	virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments, IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) override;

	bool enqueueTx(const void *data, UInt32 len);

private:
	task_t fTask;
	RTW89Airport *fOwner;
	IOSharedDataQueue *fTxQueue;
};
