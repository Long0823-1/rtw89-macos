// SPDX-License-Identifier: GPL-2.0-or-later
#include "RTW89Airport.hpp"
#include "RTW89AirportUserClient.hpp"

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
	fClient = nullptr;
	fAwdl = fP2pDev = fP2pGo = nullptr;
	fDataActive = false;
	fAssocFailSeq = 0;
	fScanLastReadNs = 0;
	fTxCalls = fTxQueued = fRxOk = fRxFail = fBpfCalls = fBpfLastDlt = fBpfQueued = 0;
	fGate = nullptr;
	memset(&fLink, 0, sizeof(fLink));
	fAssocSeq = fDisassocSeq = 0;
	memset(fVirtCount, 0, sizeof(fVirtCount));
	fScanN = fScanCursor = fScanSeq = 0;
	fScanWrap = false;
	memset(fReqCount, 0, sizeof(fReqCount));
	memset(fReqUnsup, 0, sizeof(fReqUnsup));
	fMac = { { 0x02, 0x00, 0x00, 0x89, 0x52, 0x01 } };	/* placeholder until the daemon supplies the real address */
	return true;
}

void RTW89Airport::free()
{
	if (fGate) { if (fWorkLoop) fWorkLoop->removeEventSource(fGate); fGate->release(); fGate = nullptr; }
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
	if (OSData *mac = OSDynamicCast(OSData, provider->getProperty("RTW89Airport"))) {
		if (mac->getLength() == 6) memcpy(&fMac, mac->getBytesNoCopy(), 6);	/* the daemon publishes the adapter MAC with the start trigger */
	}
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
	fGate = IOCommandGate::commandGate(this);
	if (fGate && fWorkLoop) fWorkLoop->addEventSource(fGate);
	setLinkStatus(kIONetworkLinkValid);
	registerService();
	if (fNetIf) fNetIf->registerService();
	LOG("interface attached\n");
	return true;
}

IOReturn RTW89Airport::linkActionStatic(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3)
{
	RTW89Airport *self = OSDynamicCast(RTW89Airport, owner);
	if (!self) return kIOReturnBadArgument;
	self->applyLink((uintptr_t)arg0 != 0);
	return kIOReturnSuccess;
}

void RTW89Airport::applyLink(bool up)
{
	setProperty("RTW89LastApply", up ? "up" : "down");
	if (!fNetIf) return;
	if (up) {
		setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive, getCurrentMedium(), 866000000);
		fNetIf->setLinkState(kIO80211NetworkLinkUp, 0);
		fNetIf->setLinkQualityMetric(100);
		if (ifnet_t ifp = fNetIf->getIfnet()) ifnet_set_flags(ifp, IFF_RUNNING, IFF_RUNNING);	/* the legacy family does not always raise it */
	} else {
		setLinkStatus(kIONetworkLinkValid, getCurrentMedium(), 0);
		fNetIf->setLinkState(kIO80211NetworkLinkDown, 0);
		if (ifnet_t ifp = fNetIf->getIfnet()) ifnet_set_flags(ifp, 0, IFF_RUNNING);
	}
	/* a real driver announces every part of a join/leave, also when the join was not started by macOS */
	fNetIf->postMessage(APPLE80211_M_SSID_CHANGED);
	if (up) fNetIf->postMessage(APPLE80211_M_ASSOC_DONE);
	/* keep the plain event: airportd logs a payload-length complaint, but adding the 32-byte payload made the network list empty */
	fNetIf->postMessage(APPLE80211_M_LINK_CHANGED);
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

IOReturn RTW89Airport::setProperties(OSObject *properties)
{
	OSDictionary *d = OSDynamicCast(OSDictionary, properties);
	if (!d) return kIOReturnBadArgument;
	if (OSData *res = OSDynamicCast(OSData, d->getObject("RTW89ScanResults"))) {
		UInt32 n = res->getLength() / sizeof(rtw_scan_ent);
		if (n > RTW_AP_MAX_SCAN) n = RTW_AP_MAX_SCAN;
		memcpy(fScan, res->getBytesNoCopy(), n * sizeof(rtw_scan_ent));
		fScanN = n;
		fScanCursor = 0;
		fScanWrap = false;
	}
	if (OSData *ld = OSDynamicCast(OSData, d->getObject("RTW89Link"))) {
		if (ld->getLength() >= sizeof(rtw_link)) {
			rtw_link nl;
			memcpy(&nl, ld->getBytesNoCopy(), sizeof(nl));
			bool changed = nl.up != fLink.up;
			fLink = nl;
			if (changed) {
				if (fGate) fGate->runAction(&RTW89Airport::linkActionStatic, (void *)(uintptr_t)nl.up);
				else applyLink(nl.up);
			}
		}
	}
	if (OSNumber *fl = OSDynamicCast(OSNumber, d->getObject("RTW89AssocFail"))) {
		UInt32 seq = fl->unsigned32BitValue();
		if (seq != fAssocFailSeq) {
			fAssocFailSeq = seq;
			if (fLink.up) { fLink.up = 0; if (fGate) fGate->runAction(&RTW89Airport::linkActionStatic, (void *)(uintptr_t)0); }
			if (fNetIf) fNetIf->postMessage(APPLE80211_M_ASSOC_DONE);
			setProperty("RTW89AssocFailSeq", (UInt64)seq, 32);
		}
	}
	if (OSBoolean *done = OSDynamicCast(OSBoolean, d->getObject("RTW89ScanDone"))) {
		if (done == kOSBooleanTrue && fNetIf) fNetIf->postMessage(APPLE80211_M_SCAN_DONE);
	}
	return kIOReturnSuccess;
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

IOReturn RTW89Airport::enable(IONetworkInterface *netif)
{
	LOG("enable\n");
	super::enable(netif);
	if (fLink.up) applyLink(true);	/* the link may have come up before macOS enabled the interface */
	return kIOReturnSuccess;
}
IOReturn RTW89Airport::disable(IONetworkInterface *netif)
{
	LOG("disable\n");
	super::disable(netif);
	return kIOReturnSuccess;
}

IOReturn RTW89Airport::getHardwareAddress(IOEthernetAddress *addr) { *addr = fMac; return kIOReturnSuccess; }
IOReturn RTW89Airport::getHardwareAddressForInterface(IO80211Interface *netif, IOEthernetAddress *addr) { *addr = fMac; return kIOReturnSuccess; }

void RTW89Airport::publishDataStats()
{
	char b[160];
	snprintf(b, sizeof b, "txcalls=%u txqueued=%u bpfcalls=%u bpfdlt=%u bpfqueued=%u rxok=%u rxfail=%u client=%d active=%d up=%d", (unsigned)fTxCalls, (unsigned)fTxQueued, (unsigned)fBpfCalls, (unsigned)fBpfLastDlt, (unsigned)fBpfQueued, (unsigned)fRxOk, (unsigned)fRxFail, fClient != nullptr, (int)fDataActive, (int)fLink.up);
	setProperty("RTW89DataStats", b);
}

bool RTW89Airport::queueEthernet(mbuf_t m)
{
	if (!(fClient && fDataActive && fLink.up && m)) return false;
	UInt8 buf[2048];
	UInt32 len = (UInt32)mbuf_pkthdr_len(m);
	if (len < 14 || len > sizeof(buf) || mbuf_copydata(m, 0, len, buf) != 0) return false;
	return fClient->enqueueTx(buf, len);
}

UInt32 RTW89Airport::outputPacket(mbuf_t m, void *param)
{
	UInt32 c = ++fTxCalls;
	if (queueEthernet(m)) fTxQueued++;
	if (c < 16 || (c & (c - 1)) == 0) publishDataStats();
	freePacket(m);
	return kIOReturnOutputSuccess;
}

/* the BPF write path (what the DHCP client uses): DLT_EN10MB frames are complete Ethernet frames */
int RTW89Airport::bpfOutputPacket(OSObject *obj, UInt dlt, mbuf_t m)
{
	UInt32 c = ++fBpfCalls;
	fBpfLastDlt = dlt;
	if (dlt == 1 /* DLT_EN10MB */ && queueEthernet(m)) fBpfQueued++;
	if (c < 16 || (c & (c - 1)) == 0) publishDataStats();
	if (m) mbuf_freem(m);
	return 0;
}

IOReturn RTW89Airport::newUserClient(task_t owningTask, void *securityID, UInt32 type, IOUserClient **handler)
{
	RTW89AirportUserClient *uc = OSTypeAlloc(RTW89AirportUserClient);
	if (!uc) return kIOReturnNoMemory;
	if (!uc->initWithTask(owningTask, securityID, type) || !uc->attach(this) || !uc->start(this)) {
		uc->detach(this);
		uc->release();
		return kIOReturnError;
	}
	*handler = uc;
	return kIOReturnSuccess;
}

void RTW89Airport::attachClient(RTW89AirportUserClient *c) { fClient = c; }
void RTW89Airport::detachClient(RTW89AirportUserClient *c) { if (fClient == c) { fClient = nullptr; fDataActive = false; } }
void RTW89Airport::setDataActive(bool on) { fDataActive = on; setProperty("RTW89DataActive", on ? kOSBooleanTrue : kOSBooleanFalse); }

bool RTW89Airport::useAppleRSNSupplicant(IO80211Interface *interface) { return false; }	/* rtw89d runs the 4-way handshake itself */

IOReturn RTW89Airport::rxFromUser(const void *data, UInt32 len)
{
	if (!fNetIf || !fLink.up) { fRxFail++; return kIOReturnNotReady; }
	mbuf_t m = allocatePacket(len);
	if (!m) { fRxFail++; return kIOReturnNoMemory; }
	if (mbuf_copyback(m, 0, len, data, MBUF_DONTWAIT) != 0) { fRxFail++; freePacket(m); return kIOReturnNoMemory; }
	mbuf_pkthdr_setlen(m, len);
	mbuf_setlen(m, len);
	fNetIf->inputPacket(m, len, 0, nullptr);
	UInt32 c = ++fRxOk;
	if (c < 16 || (c & (c - 1)) == 0) publishDataStats();
	return kIOReturnSuccess;
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

SInt32 RTW89Airport::apple80211VirtualRequest(UInt request_type, int request_number, IO80211VirtualInterface *interface, void *data)
{
	unsigned t = request_type == SIOCSA80211 ? 1 : 0;
	if (request_number >= 0 && request_number < 256) {
		UInt32 c = ++fVirtCount[t][request_number];
		if (c == 1 || (c & (c - 1)) == 0) {
			char buf[768];
			int n = 0;
			for (int i = 0; i < 256 && n < (int)sizeof(buf) - 24; i++)
				for (int k = 0; k < 2; k++)
					if (fVirtCount[k][i]) n += snprintf(buf + n, sizeof(buf) - n, "%d%c:%u ", i, k ? 's' : 'g', (unsigned)fVirtCount[k][i]);
			setProperty("VirtRequestStats", buf);
		}
	}
	if (request_type != SIOCGA80211 && request_type != SIOCSA80211) return kIOReturnError;
	if (request_number == APPLE80211_IOC_CHANNELS_INFO) {
		if (request_type != SIOCGA80211) return kIOReturnError;
		auto *ci = (apple80211_channels_info *)data;
		memset(ci, 0, sizeof(*ci));
		ci->version = APPLE80211_VERSION;
		static const UInt8 ch[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165 };
		for (UInt8 c2 : ch) {
			ci->chan_num[ci->num_chan_specs] = c2;
			ci->support_40Mhz[ci->num_chan_specs] = c2 >= 36 || c2 <= 9;
			ci->support_80Mhz[ci->num_chan_specs] = c2 >= 36;
			ci->num_chan_specs++;
		}
		return kIOReturnSuccess;
	}
	return apple80211RequestImpl(request_type, request_number, nullptr, data);
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
			setProperty("RTW89PowerOff", (UInt64)(fPower ? 0 : 1), 32);	/* rtw89d disconnects and stops joining while macOS has Wi-Fi off */
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
		sd->state = fLink.up ? APPLE80211_S_RUN : APPLE80211_S_INIT;
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
	case APPLE80211_IOC_SSID: {
		if (!get) return kIOReturnSuccess;
		if (!fLink.up) return 6;
		auto *sd = (apple80211_ssid_data *)data;
		memset(sd, 0, sizeof(*sd));
		sd->version = APPLE80211_VERSION;
		sd->ssid_len = fLink.ssid_len;
		memcpy(sd->ssid_bytes, fLink.ssid, fLink.ssid_len);
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_BSSID: {
		if (!get) return kIOReturnSuccess;
		if (!fLink.up) return 6;
		auto *bd = (apple80211_bssid_data *)data;
		memset(bd, 0, sizeof(*bd));
		bd->version = APPLE80211_VERSION;
		memcpy(bd->bssid.octet, fLink.bssid, 6);
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_CHANNEL: {
		if (!get) return kIOReturnSuccess;
		if (!fLink.up) return 6;
		auto *cd = (apple80211_channel_data *)data;
		memset(cd, 0, sizeof(*cd));
		cd->version = APPLE80211_VERSION;
		cd->channel.version = APPLE80211_VERSION;
		cd->channel.channel = fLink.channel;
		cd->channel.flags = (fLink.band5 ? APPLE80211_C_FLAG_5GHZ : APPLE80211_C_FLAG_2GHZ) | APPLE80211_C_FLAG_20MHZ | APPLE80211_C_FLAG_ACTIVE;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_RSSI: {
		if (!get) return kIOReturnError;
		if (!fLink.up) return 6;
		auto *rd = (apple80211_rssi_data *)data;
		memset(rd, 0, sizeof(*rd));
		rd->version = APPLE80211_VERSION;
		rd->num_radios = 1;
		rd->rssi_unit = APPLE80211_UNIT_DBM;
		rd->rssi[0] = rd->aggregate_rssi = rd->rssi_ext[0] = rd->aggregate_rssi_ext = fLink.rssi;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_RATE: {
		if (!get) return kIOReturnError;
		if (!fLink.up) return 6;
		auto *rt = (apple80211_rate_data *)data;
		memset(rt, 0, sizeof(*rt));
		rt->version = APPLE80211_VERSION;
		rt->num_radios = 1;
		rt->rate[0] = 866;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_ASSOCIATE_RESULT: {
		if (!get) return kIOReturnError;
		if (!fLink.up) return kIOReturnError;	/* failed (or not associated): macOS then asks the user again */
		auto *ar = (apple80211_assoc_result_data *)data;
		ar->version = APPLE80211_VERSION;
		ar->result = APPLE80211_RESULT_SUCCESS;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_ASSOCIATE: {
		if (get) return kIOReturnError;
		auto *ad = (apple80211_assoc_data *)data;
		rtw_assoc_req rq;
		memset(&rq, 0, sizeof(rq));
		rq.seq = ++fAssocSeq;
		rq.ssid_len = ad->ad_ssid_len > 32 ? 32 : ad->ad_ssid_len;
		memcpy(rq.ssid, ad->ad_ssid, rq.ssid_len);
		memcpy(rq.bssid, ad->ad_bssid.octet, 6);
		rq.auth_lower = ad->ad_auth_lower;
		rq.auth_upper = ad->ad_auth_upper;
		rq.key_len = ad->ad_key.key_len > 64 ? 64 : ad->ad_key.key_len;
		memcpy(rq.key, ad->ad_key.key, rq.key_len);
		rq.rsn_len = ad->ad_rsn_ie_len > 64 ? 64 : ad->ad_rsn_ie_len;
		memcpy(rq.rsn, ad->ad_rsn_ie, rq.rsn_len);
		OSData *od = OSData::withBytes(&rq, sizeof(rq));
		if (od) { setProperty("RTW89AssocReq", od); od->release(); }
		setProperty("RTW89AssocSeq", (UInt64)fAssocSeq, 32);
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_DISASSOCIATE:
	case APPLE80211_IOC_DEAUTH:
		if (!get) { fDisassocSeq++; setProperty("RTW89DisassocSeq", (UInt64)fDisassocSeq, 32); }
		return kIOReturnSuccess;
	case APPLE80211_IOC_AUTH_TYPE: {
		if (!get) return kIOReturnSuccess;
		auto *ad = (apple80211_authtype_data *)data;
		ad->version = APPLE80211_VERSION;
		ad->authtype_lower = APPLE80211_AUTHTYPE_OPEN;
		ad->authtype_upper = fLink.up ? APPLE80211_AUTHTYPE_WPA2_PSK : APPLE80211_AUTHTYPE_NONE;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_LINK_CHANGED_EVENT_DATA: {
		if (!get) return kIOReturnError;
		auto *ed = (apple80211_link_changed_event_data *)data;
		memset(ed, 0, sizeof(*ed));
		ed->isLinkDown = !fLink.up;
		if (ed->isLinkDown) { ed->voluntary = true; ed->reason = APPLE80211_LINK_DOWN_REASON_DEAUTH; }
		else ed->rssi = (uint32_t)(int32_t)fLink.rssi;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_RATE_SET:
	case APPLE80211_IOC_MCS_INDEX_SET:
	case APPLE80211_IOC_VHT_MCS_INDEX_SET:
		return get ? 6 : kIOReturnSuccess;
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
	case APPLE80211_IOC_SCAN_REQ:
	case APPLE80211_IOC_SCAN_REQ_MULTIPLE: {
		if (get) return kIOReturnError;
		{	/* keep the last request visible in ioreg: the parameters tell what macOS expects from the scan */
			char b[200];
			if (request_number == APPLE80211_IOC_SCAN_REQ) {
				auto *sd = (apple80211_scan_data *)data;
				snprintf(b, sizeof b, "SCAN_REQ type=%u bss=%u phy=0x%x dwell=%u rest=%u nchan=%u first=%u ssid_len=%u", (unsigned)sd->scan_type, (unsigned)sd->bss_type, (unsigned)sd->phy_mode, (unsigned)sd->dwell_time, (unsigned)sd->rest_time, (unsigned)sd->num_channels, sd->num_channels ? (unsigned)sd->channels[0].channel : 0u, (unsigned)sd->ssid_len);
			} else {
				auto *sm = (apple80211_scan_multiple_data *)data;
				snprintf(b, sizeof b, "SCAN_REQ_MULTIPLE type=%u ssids=%u bssids=%u dwell=%u rest=%u nchan=%u first=%u", (unsigned)sm->scan_type, (unsigned)sm->ssid_count, (unsigned)sm->bssid_count, (unsigned)sm->dwell_time, (unsigned)sm->rest_time, (unsigned)sm->num_channels, sm->num_channels ? (unsigned)sm->channels[0].channel : 0u);
			}
			setProperty("RTW89LastScanReq", b);
		}
		if (!fPower) return 22;
		fScanCursor = 0; fScanWrap = false;	/* every pass of results follows a request: always start from the first entry */
		fScanSeq++;
		setProperty("RTW89ScanSeq", (UInt64)fScanSeq, 32);
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_SCAN_RESULT: {
		if (!get) return kIOReturnError;
		{	/* a read that comes long after the previous one starts a new pass, even if the old pass was never closed with the "no more" answer */
			uint64_t abs;
			UInt64 ns;
			clock_get_uptime(&abs);
			absolutetime_to_nanoseconds(abs, &ns);
			if (fScanLastReadNs && ns - fScanLastReadNs > 500000000ULL) { fScanCursor = 0; fScanWrap = false; }
			fScanLastReadNs = ns;
		}
		if (fScanCursor >= fScanN) {
			if (fScanWrap) { fScanWrap = false; fScanCursor = 0; return 5; }
			if (fScanN == 0) return 12;
			fScanCursor = 0;
		}
		const rtw_scan_ent &e = fScan[fScanCursor];
		apple80211_scan_result *r = &fResult;
		memset(r, 0, sizeof(*r));
		r->version = APPLE80211_VERSION;
		r->asr_channel.version = APPLE80211_VERSION;
		r->asr_channel.channel = e.channel;
		r->asr_channel.flags = (e.band5 ? APPLE80211_C_FLAG_5GHZ : APPLE80211_C_FLAG_2GHZ) | APPLE80211_C_FLAG_20MHZ | APPLE80211_C_FLAG_ACTIVE;
		r->asr_noise = e.noise;
		r->asr_rssi = e.rssi;
		r->asr_snr = e.rssi - e.noise;
		r->asr_beacon_int = e.beacon_int;
		r->asr_cap = e.cap;
		memcpy(r->asr_bssid, e.bssid, 6);
		r->asr_nrates = 1;
		r->asr_rates[0] = 6;
		r->asr_ssid_len = e.ssid_len;
		memcpy(r->asr_ssid, e.ssid, e.ssid_len);
		r->asr_age = 0;
		UInt16 il = e.ie_len > sizeof(r->asr_ie_data) ? (UInt16)sizeof(r->asr_ie_data) : e.ie_len;
		r->asr_ie_len = il;
		memcpy(r->asr_ie_data, e.ie, il);
		*(apple80211_scan_result **)data = r;
		fScanCursor++;
		if (fScanCursor >= fScanN) fScanWrap = true;
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_VIRTUAL_IF_CREATE: {
		if (get) return kIOReturnError;
		auto *vd = (apple80211_virt_if_create_data *)data;
		{	/* keep the request visible in ioreg: it tells which interface roles macOS wants */
			char b[120];
			snprintf(b, sizeof b, "role=%u bsd=%.16s mac=%02x:%02x:%02x:%02x:%02x:%02x unk1=%u", (unsigned)vd->role, (const char *)vd->bsd_name, vd->mac[0], vd->mac[1], vd->mac[2], vd->mac[3], vd->mac[4], vd->mac[5], (unsigned)vd->unk1);
			setProperty("RTW89VifReq", b);
		}
		ether_addr mac;
		memcpy(mac.octet, vd->mac, 6);
		/* airportd on this macOS numbers the roles 3 P2P device, 4 P2P client, 5 P2P GO, 6 AWDL, 7 soft AP; the macOS 12 family
		 * we run on wants 1, 2, 3, 4, 5 */
		IO80211P2PInterface **slot = nullptr;
		const char *name = "p2p";
		uint32_t oldRole = 0;
		if (vd->role == 3) { slot = &fP2pDev; oldRole = 1; }
		else if (vd->role == 5) { slot = &fP2pGo; oldRole = 3; }
		else if (vd->role == 6) { slot = &fAwdl; name = "awdl"; oldRole = 4; }
		else { setProperty("RTW89VifResult", "unhandled role"); return kIOReturnError; }
		if (*slot) return kIOReturnSuccess;	/* already there */
		IO80211P2PInterface *inf = new IO80211P2PInterface;
		if (!inf) return kIOReturnError;
		if (!inf->init(this, &mac, oldRole, name)) { inf->release(); setProperty("RTW89VifResult", "init failed"); return kIOReturnError; }
		*slot = inf;
		setProperty("RTW89VirtualIf", name);
		setProperty("RTW89VifResult", "created");
		return kIOReturnSuccess;
	}
	case APPLE80211_IOC_VIRTUAL_IF_DELETE:
		return kIOReturnSuccess;	/* the objects stay until the controller goes away */
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
