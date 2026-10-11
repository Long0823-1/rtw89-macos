// SPDX-License-Identifier: BSD-3-Clause
// Bridge between rtw89d and RTW89Airport.kext (the System Settings Wi-Fi interface), over IORegistry properties.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "mm.h"
#include "mlme.h"
#include "../../airport/rtw89_airport_abi.h"

extern void daemon_request_scan(void);
extern int daemon_connect_request(const char *ssid, const char *psk, bool open);
extern void daemon_disconnect_request(void);
extern bool daemon_link_info(char *ssid, size_t n, unsigned char bssid[6], int *chan, int *sig);
extern unsigned long daemon_scan_count(void);

static io_service_t find_kext(void)
{
	return IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("RTW89Airport"));
}

static long read_seq(io_service_t s)
{
	long v = 0;	/* the property only appears after the first scan request */
	CFTypeRef p = IORegistryEntryCreateCFProperty(s, CFSTR("RTW89ScanSeq"), kCFAllocatorDefault, 0);
	if (p) {
		if (CFGetTypeID(p) == CFNumberGetTypeID()) CFNumberGetValue(p, kCFNumberLongType, &v);
		CFRelease(p);
	}
	return v;
}

static void push_scan(io_service_t s)
{
	static struct rtw_scan_ent ent[RTW_AP_MAX_SCAN];
	int n = 0;
	mlme_lock();
	for (int i = 0; i < bss_n && n < RTW_AP_MAX_SCAN; i++) {
		const struct bss_entry *b = &bss_tab[i];
		struct rtw_scan_ent *e = &ent[n++];
		memset(e, 0, sizeof(*e));
		memcpy(e->bssid, b->bssid, 6);
		e->ssid_len = b->ssid_len > 32 ? 32 : b->ssid_len;
		memcpy(e->ssid, b->ssid, e->ssid_len);
		e->channel = (uint16_t)b->chan;
		e->band5 = b->freq >= 4900;
		e->rssi = b->signal;
		e->noise = -95;
		e->cap = b->capab;
		e->beacon_int = b->beacon_int;
		const u8 *rsn = ie_find(b->ies, b->ie_len, 48);
		if (rsn && 2u + rsn[1] <= RTW_AP_MAX_IE) { e->ie_len = 2 + rsn[1]; memcpy(e->ie, rsn, e->ie_len); }
	}
	mlme_unlock();
	CFDataRef d = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)ent, n * sizeof(ent[0]));
	IOReturn r = IORegistryEntrySetCFProperty(s, CFSTR("RTW89ScanResults"), d);
	CFRelease(d);
	IOReturn r2 = IORegistryEntrySetCFProperty(s, CFSTR("RTW89ScanDone"), kCFBooleanTrue);
	fprintf(stderr, "[airport] pushed %d scan results (0x%x, 0x%x)\n", n, r, r2);
}

static long read_num(io_service_t s, CFStringRef key)
{
	long v = 0;
	CFTypeRef p = IORegistryEntryCreateCFProperty(s, key, kCFAllocatorDefault, 0);
	if (p) { if (CFGetTypeID(p) == CFNumberGetTypeID()) CFNumberGetValue(p, kCFNumberLongType, &v); CFRelease(p); }
	return v;
}

static void handle_assoc(io_service_t s)
{
	CFTypeRef p = IORegistryEntryCreateCFProperty(s, CFSTR("RTW89AssocReq"), kCFAllocatorDefault, 0);
	if (!p) return;
	if (CFGetTypeID(p) == CFDataGetTypeID() && CFDataGetLength(p) >= (CFIndex)sizeof(struct rtw_assoc_req)) {
		struct rtw_assoc_req rq;
		memcpy(&rq, CFDataGetBytePtr(p), sizeof rq);
		char ssid[33] = "", psk[65] = "";
		memcpy(ssid, rq.ssid, rq.ssid_len > 32 ? 32 : rq.ssid_len);
		bool printable = rq.key_len >= 8 && rq.key_len <= 63;
		for (int i = 0; printable && i < rq.key_len; i++) if (rq.key[i] < 0x20 || rq.key[i] > 0x7e) printable = false;
		if (printable) { memcpy(psk, rq.key, rq.key_len); psk[rq.key_len] = 0; }
		bool open = rq.rsn_len == 0 && rq.key_len == 0 && rq.auth_upper == 0;
		fprintf(stderr, "[airport] system connect request: \"%s\" auth %u/%u key_len %u rsn_len %u (%s)\n", ssid, rq.auth_lower, rq.auth_upper, rq.key_len, rq.rsn_len, printable ? "passphrase" : "no passphrase");
		if (daemon_connect_request(ssid, psk, open) != 0)
			fprintf(stderr, "[airport] no stored passphrase for \"%s\"; connect it once with the menu bar app\n", ssid);
	}
	CFRelease(p);
}

static void push_link(io_service_t s, struct rtw_link *last)
{
	struct rtw_link l;
	char ssid[33] = "";
	unsigned char bssid[6] = { 0 };
	int chan = 0, sig = 0;
	memset(&l, 0, sizeof l);
	if (daemon_link_info(ssid, sizeof ssid, bssid, &chan, &sig)) {
		l.up = 1;
		l.ssid_len = (uint8_t)strlen(ssid);
		memcpy(l.ssid, ssid, l.ssid_len);
		memcpy(l.bssid, bssid, 6);
		l.channel = (uint16_t)chan;
		l.band5 = chan > 14;
		l.rssi = (int16_t)sig;
	}
	if (memcmp(&l, last, sizeof l) == 0) return;
	*last = l;
	CFDataRef d = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)&l, sizeof l);
	IOReturn r = IORegistryEntrySetCFProperty(s, CFSTR("RTW89Link"), d);
	CFRelease(d);
	fprintf(stderr, "[airport] link %s (0x%x)\n", l.up ? "up" : "down", r);
}

static void *bridge_thread(void *a)
{
	io_service_t svc = 0;
	long last = -2, last_assoc = -1, last_disassoc = -1;
	struct rtw_link last_link;
	memset(&last_link, 0xff, sizeof last_link);
	int tick = 0;
	bool pending = false;
	unsigned long scans_at_request = 0;
	for (;;) {
		usleep(250000);
		if (!svc) { svc = find_kext(); if (!svc) continue; last = read_seq(svc); last_assoc = read_num(svc, CFSTR("RTW89AssocSeq")); last_disassoc = read_num(svc, CFSTR("RTW89DisassocSeq")); memset(&last_link, 0xff, sizeof last_link); fprintf(stderr, "[airport] kext found\n"); continue; }
		long seq = read_seq(svc);
		if (IORegistryEntryGetRegistryEntryID(svc, &(uint64_t){0}) != KERN_SUCCESS) { IOObjectRelease(svc); svc = 0; continue; }	/* kext went away */
		if (seq != last) {
			last = seq;
			scans_at_request = daemon_scan_count();
			pending = true;
			daemon_request_scan();
			fprintf(stderr, "[airport] scan requested by the system (seq %ld)\n", seq);
		}
		long as = read_num(svc, CFSTR("RTW89AssocSeq"));
		if (as != last_assoc) { last_assoc = as; handle_assoc(svc); }
		long ds = read_num(svc, CFSTR("RTW89DisassocSeq"));
		if (ds != last_disassoc) { last_disassoc = ds; fprintf(stderr, "[airport] system disconnect request\n"); daemon_disconnect_request(); }
		if ((++tick & 3) == 0) push_link(svc, &last_link);
		if (pending && daemon_scan_count() > scans_at_request) { pending = false; push_scan(svc); }
	}
	return NULL;
}

void airport_bridge_start(void)
{
	pthread_t t;
	pthread_create(&t, NULL, bridge_thread, NULL);
	pthread_detach(t);
}
