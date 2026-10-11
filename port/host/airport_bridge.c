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
extern unsigned long daemon_scan_count(void);

static io_service_t find_kext(void)
{
	return IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("RTW89Airport"));
}

static long read_seq(io_service_t s)
{
	long v = -1;
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

static void *bridge_thread(void *a)
{
	io_service_t svc = 0;
	long last = -2;
	bool pending = false;
	unsigned long scans_at_request = 0;
	for (;;) {
		usleep(250000);
		if (!svc) { svc = find_kext(); if (!svc) continue; last = read_seq(svc); fprintf(stderr, "[airport] kext found\n"); continue; }
		long seq = read_seq(svc);
		if (seq < 0) { IOObjectRelease(svc); svc = 0; continue; }	/* kext went away */
		if (seq != last) {
			last = seq;
			scans_at_request = daemon_scan_count();
			pending = true;
			daemon_request_scan();
			fprintf(stderr, "[airport] scan requested by the system (seq %ld)\n", seq);
		}
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
