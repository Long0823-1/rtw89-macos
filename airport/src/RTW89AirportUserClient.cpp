// SPDX-License-Identifier: GPL-2.0-or-later
#include "RTW89Airport.hpp"
#include "RTW89AirportUserClient.hpp"

#define super IOUserClient
OSDefineMetaClassAndStructors(RTW89AirportUserClient, IOUserClient)

enum { kSelRx = 0, kSelSetActive = 1 };

bool RTW89AirportUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!super::initWithTask(owningTask, securityID, type)) return false;
	if (clientHasPrivilege(owningTask, kIOClientPrivilegeAdministrator) != kIOReturnSuccess) return false;
	fTask = owningTask;
	fOwner = nullptr;
	fTxQueue = nullptr;
	return true;
}

bool RTW89AirportUserClient::start(IOService *provider)
{
	fOwner = OSDynamicCast(RTW89Airport, provider);
	if (!fOwner || !super::start(provider)) return false;
	fTxQueue = IOSharedDataQueue::withCapacity(4 * 1024 * 1024);
	if (!fTxQueue) return false;
	fOwner->attachClient(this);
	return true;
}

void RTW89AirportUserClient::stop(IOService *provider)
{
	if (fOwner) fOwner->detachClient(this);
	super::stop(provider);
}

void RTW89AirportUserClient::free()
{
	if (fTxQueue) { fTxQueue->release(); fTxQueue = nullptr; }
	super::free();
}

IOReturn RTW89AirportUserClient::clientClose()
{
	if (fOwner) { fOwner->detachClient(this); fOwner = nullptr; }
	if (!isInactive()) terminate();
	return kIOReturnSuccess;
}

IOReturn RTW89AirportUserClient::clientMemoryForType(UInt32 type, IOOptionBits *options, IOMemoryDescriptor **memory)
{
	if (type != 0 || !fTxQueue) return kIOReturnBadArgument;
	IOMemoryDescriptor *md = fTxQueue->getMemoryDescriptor();
	if (!md) return kIOReturnNoMemory;
	md->retain();
	*memory = md;
	*options = 0;
	return kIOReturnSuccess;
}

IOReturn RTW89AirportUserClient::registerNotificationPort(mach_port_t port, UInt32 type, UInt32 refCon)
{
	if (!fTxQueue) return kIOReturnNotReady;
	fTxQueue->setNotificationPort(port);
	return kIOReturnSuccess;
}

bool RTW89AirportUserClient::enqueueTx(const void *data, UInt32 len)
{
	return fTxQueue && fTxQueue->enqueue((void *)data, len);
}

IOReturn RTW89AirportUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args, IOExternalMethodDispatch *dispatch, OSObject *target, void *reference)
{
	if (!fOwner) return kIOReturnNotAttached;
	switch (selector) {
	case kSelRx:
		if (!args->structureInput || args->structureInputSize < 14 || args->structureInputSize > 2048) return kIOReturnBadArgument;
		return fOwner->rxFromUser(args->structureInput, args->structureInputSize);
	case kSelSetActive:
		fOwner->setDataActive(args->scalarInput && args->scalarInput[0]);
		return kIOReturnSuccess;
	}
	return kIOReturnUnsupported;
}
