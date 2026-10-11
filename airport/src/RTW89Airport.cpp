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
	fPower = 1;
	memset(fReqCount, 0, sizeof(fReqCount));
	memset(fReqUnsup, 0, sizeof(fReqUnsup));
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
	const IONetworkMedium *primaryMedium = nullptr;
	if (!createMediumTables(&primaryMedium) || !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
		LOG("medium setup failed\n");
		super::stop(provider);
		return false;
	}
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

bool RTW89Airport::createMediumTables(const IONetworkMedium **primary)
{
	OSDictionary *dict = OSDictionary::withCapacity(1);
	if (!dict) return false;
	IONetworkMedium *medium = IONetworkMedium::medium(0x80, 11000000);	/* same IEEE 802.11 medium type AirportItlwm publishes */
	IONetworkMedium::addMedium(dict, medium);
	medium->release();
	if (primary) *primary = medium;
	bool ok = publishMediumDictionary(dict);
	dict->release();
	return ok;
}

IOReturn RTW89Airport::selectMedium(const IONetworkMedium *medium)
{
	setSelectedMedium(medium);
	return kIOReturnSuccess;
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

void RTW89Airport::noteRequest(unsigned type, int num, bool handled)
{
	unsigned t = type == SIOCSA80211 ? 1 : 0;
	if (num < 0 || num > 255) return;
	UInt32 c = ++fReqCount[t][num];
	if (!handled) fReqUnsup[t][num]++;
	if (c == 1 || (c & (c - 1)) == 0) {	/* first sight and every power of two: refresh the registry summary */
		char buf[1536];
		int n = 0;
		for (int i = 0; i < 256 && n < (int)sizeof(buf) - 32; i++)
			for (int k = 0; k < 2; k++)
				if (fReqCount[k][i])
					n += snprintf(buf + n, sizeof(buf) - n, "%d%c:%u%s ", i, k ? 's' : 'g', (unsigned)fReqCount[k][i], fReqUnsup[k][i] ? "!" : "");
		setProperty("RequestStats", buf);
	}
}

SInt32 RTW89Airport::apple80211Request(unsigned int request_type, int request_number, IO80211Interface *interface, void *data)
{
	SInt32 r = apple80211RequestImpl(request_type, request_number, interface, data);
	noteRequest(request_type, request_number, r != (SInt32)kIOReturnUnsupported);
	return r;
}

SInt32 RTW89Airport::apple80211RequestImpl(unsigned int request_type, int request_number, IO80211Interface *interface, void *data)
{
	const bool get = request_type == SIOCGA80211;
	if (request_type != SIOCGA80211 && request_type != SIOCSA80211) return kIOReturnError;
	switch (request_number) {
	case APPLE80211_IOC_POWER: {
		auto *pd = (apple80211_power_data *)data;
		if (!pd) return kIOReturnError;
		if (get) {
			pd->version = APPLE80211_VERSION;
			pd->num_radios = 4;
			for (int i = 0; i < 4; i++) pd->power_state[i] = fPower;
		} else if (pd->num_radios > 0) {
			fPower = pd->power_state[0] ? 1 : 0;
			LOG("setPOWER %u\n", (unsigned)fPower);
		}
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_CARD_CAPABILITIES: {
		if (!get) return kIOReturnError;
		auto *cd = (apple80211_capability_data *)data;
		memset(cd, 0, sizeof(*cd));
		cd->version = APPLE80211_VERSION;
		cd->capabilities[0] |= 1 << APPLE80211_CAP_AES_CCM;
		cd->capabilities[1] |= (1 << (APPLE80211_CAP_SHSLOT - 8)) | (1 << (APPLE80211_CAP_SHPREAMBLE - 8)) |
				       (1 << (APPLE80211_CAP_WPA1 - 8)) | (1 << (APPLE80211_CAP_WPA2 - 8));
		cd->capabilities[2] = 0xFF;
		cd->capabilities[3] = 0x2B;
		cd->capabilities[4] = 0xAD;
		cd->capabilities[5] = 0x8C;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_DRIVER_VERSION:
	case APPLE80211_IOC_HARDWARE_VERSION: {
		if (!get) return kIOReturnError;
		auto *hv = (apple80211_version_data *)data;
		hv->version = APPLE80211_VERSION;
		snprintf(hv->string, sizeof(hv->string), request_number == APPLE80211_IOC_DRIVER_VERSION ? "rtw89d 0.1" : "RTL8852B");
		hv->string_len = (u_int16_t)strlen(hv->string);
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_PHY_MODE: {
		if (!get) return kIOReturnError;
		auto *pd = (apple80211_phymode_data *)data;
		pd->version = APPLE80211_VERSION;
		pd->phy_mode = APPLE80211_MODE_11A | APPLE80211_MODE_11B | APPLE80211_MODE_11G | APPLE80211_MODE_11N | APPLE80211_MODE_11AC | APPLE80211_MODE_11AX;
		pd->active_phy_mode = APPLE80211_MODE_AUTO;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_OP_MODE: {
		if (!get) return kIOReturnError;
		auto *od = (apple80211_opmode_data *)data;
		od->version = APPLE80211_VERSION;
		od->op_mode = APPLE80211_M_STA;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_STATE: {
		if (!get) return kIOReturnError;
		auto *sd = (apple80211_state_data *)data;
		memset(sd, 0, sizeof(*sd));
		sd->version = APPLE80211_VERSION;
		sd->state = APPLE80211_S_INIT;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_LOCALE: {
		if (!get) return kIOReturnError;
		auto *ld = (apple80211_locale_data *)data;
		ld->version = APPLE80211_VERSION;
		ld->locale = APPLE80211_LOCALE_FCC;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_COUNTRY_CODE: {
		auto *cd = (apple80211_country_code_data *)data;
		if (get) {
			cd->version = APPLE80211_VERSION;
			memset(cd->cc, 0, sizeof(cd->cc));
			cd->cc[0] = 'J'; cd->cc[1] = 'P';
		}
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_SUPPORTED_CHANNELS:
	case APPLE80211_IOC_HW_SUPPORTED_CHANNELS: {
		if (!get) return kIOReturnError;
		auto *sc = (apple80211_sup_channel_data *)data;
		memset(sc, 0, sizeof(*sc));
		sc->version = APPLE80211_VERSION;
		static const UInt8 ch24[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
		static const UInt8 ch5[] = { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165 };
		UInt32 n = 0;
		for (UInt8 c : ch24) { sc->supported_channels[n].version = APPLE80211_VERSION; sc->supported_channels[n].channel = c; sc->supported_channels[n].flags = APPLE80211_C_FLAG_2GHZ | APPLE80211_C_FLAG_20MHZ | APPLE80211_C_FLAG_ACTIVE; n++; }
		for (UInt8 c : ch5) { sc->supported_channels[n].version = APPLE80211_VERSION; sc->supported_channels[n].channel = c; sc->supported_channels[n].flags = APPLE80211_C_FLAG_5GHZ | APPLE80211_C_FLAG_20MHZ | APPLE80211_C_FLAG_ACTIVE; n++; }
		sc->num_channels = n;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_SSID:
	case APPLE80211_IOC_BSSID:
	case APPLE80211_IOC_CHANNEL:
	case APPLE80211_IOC_RSSI:
	case APPLE80211_IOC_RATE:
	case APPLE80211_IOC_ASSOCIATE_RESULT:
		/* not associated yet: Apple drivers answer ENXIO-like errors for these */
		return get ? 6 : kIOReturnUnsupported;
	case APPLE80211_IOC_ROAM_THRESH: {
		if (!get) return kIOReturnSuccess;
		auto *md = (apple80211_roam_threshold_data *)data;
		md->threshold = 100;
		md->count = 0;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_TXPOWER: {
		if (!get) return kIOReturnSuccess;
		auto *td = (apple80211_txpower_data *)data;
		td->version = APPLE80211_VERSION;
		td->txpower_unit = APPLE80211_UNIT_DBM;
		td->txpower = 20;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_INT_MIT: {
		if (!get) return kIOReturnSuccess;
		auto *id = (apple80211_intmit_data *)data;
		id->version = APPLE80211_VERSION;
		id->int_mit = APPLE80211_INT_MIT_AUTO;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_ASSOCIATION_STATUS: {
		if (!get) return kIOReturnError;
		auto *as = (apple80211_assoc_status_data *)data;
		as->version = APPLE80211_VERSION;
		as->status = 0;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_DISASSOCIATE:
		return kIOReturnSuccess;
	case APPLE80211_IOC_NOISE: {
		if (!get) return kIOReturnError;
		auto *nd = (apple80211_noise_data *)data;
		memset(nd, 0, sizeof(*nd));
		nd->version = APPLE80211_VERSION;
		nd->num_radios = 1;
		nd->noise[0] = -95;
		nd->aggregate_noise = -95;
		return kIOReturnSuccess;
	}
	default:
		return kIOReturnUnsupported;
	}
}

SInt32 RTW89Airport::stopDMA() { return 0; }
UInt32 RTW89Airport::hardwareOutputQueueDepth(IO80211Interface *interface) { return 0; }
SInt32 RTW89Airport::performCountryCodeOperation(IO80211Interface *interface, IO80211CountryCodeOp op) { return 0; }
SInt32 RTW89Airport::enableFeature(IO80211FeatureCode code, void *data) { return code == kIO80211Feature80211n ? 0 : 102; }

SInt32 RTW89Airport::monitorModeSetEnabled(IO80211Interface *interface, bool enabled, UInt32 dlt) { return kIOReturnUnsupported; }

const OSString *RTW89Airport::newVendorString() const { return OSString::withCString("Realtek"); }
const OSString *RTW89Airport::newModelString() const { return OSString::withCString("RTL8852B (rtw89d)"); }
