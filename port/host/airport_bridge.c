// SPDX-License-Identifier: BSD-3-Clause
// Bridge between rtw89d and RTW89Airport.kext (the System Settings Wi-Fi interface), over IORegistry properties.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IODataQueueClient.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "mm.h"
#include "mlme.h"
#include "../../airport/rtw89_airport_abi.h"

extern void daemon_request_scan(void);
extern int daemon_connect_request(const char *ssid, const char *psk, bool open, const unsigned char *pmk);
extern void daemon_disconnect_request(void);
extern void daemon_set_autoconnect(int on);
extern bool daemon_link_info(char *ssid, size_t n, unsigned char bssid[6], int *chan, int *sig);
extern unsigned long daemon_scan_count(void);

extern int (*net_rx_hook)(const unsigned char *eth, size_t len);
extern int net_tx_raw(const unsigned char *eth, size_t len);

static io_connect_t g_conn;
static volatile int g_active;
static unsigned long g_tx_n, g_rx_n;

static int rx_hook(const unsigned char *eth, size_t len)
{
	if (!g_active || !g_conn) return 0;
	g_rx_n++;
	IOConnectCallStructMethod(g_conn, 0, eth, len, NULL, NULL);
	return 1;
}

static void *tx_thread(void *arg)
{
	IODataQueueMemory *q = arg;
	mach_port_t port = IODataQueueAllocateNotificationPort();
	unsigned char buf[2048];
	if (IOConnectSetNotificationPort(g_conn, 0, port, 0) != KERN_SUCCESS) { fprintf(stderr, "[airport] cannot set the notification port\n"); return NULL; }
	for (;;) {
		while (IODataQueueDataAvailable(q)) {
			uint32_t sz = sizeof buf;
			if (IODataQueueDequeue(q, buf, &sz) == KERN_SUCCESS && sz >= 14) { g_tx_n++; if (g_tx_n <= 8) fprintf(stderr, "[airport] tx from en3: %u bytes, ethertype %02x%02x\n", sz, buf[12], buf[13]); net_tx_raw(buf, sz); }
			else break;
		}
		IODataQueueWaitForAvailableData(q, port);
	}
	return NULL;
}

static void data_connect(io_service_t svc)
{
	if (g_conn) return;
	io_connect_t c;
	if (IOServiceOpen(svc, mach_task_self(), 0, &c) != KERN_SUCCESS) { fprintf(stderr, "[airport] IOServiceOpen failed (data path stays on utun)\n"); return; }
	mach_vm_address_t addr = 0;
	mach_vm_size_t size = 0;
	if (IOConnectMapMemory64(c, 0, mach_task_self(), &addr, &size, kIOMapAnywhere) != KERN_SUCCESS) { IOServiceClose(c); fprintf(stderr, "[airport] cannot map the TX queue\n"); return; }
	g_conn = c;
	daemon_set_autoconnect(0);	/* macOS (System Settings) is in charge of which network to join */
	net_rx_hook = rx_hook;
	pthread_t t;
	pthread_create(&t, NULL, tx_thread, (void *)(uintptr_t)addr);
	pthread_detach(t);
	fprintf(stderr, "[airport] data path to en3 ready (queue %llu bytes)\n", (unsigned long long)size);
}

static io_service_t find_kext(void);

int airport_data_ready(void) { return g_conn != 0; }

/* tell macOS that the connection it asked for failed (it then shows the password prompt again) */
void airport_assoc_failed(void)
{
	static int seq;
	io_service_t svc = find_kext();
	if (!svc) return;
	int v = ++seq;
	CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &v);
	IOReturn r = IORegistryEntrySetCFProperty(svc, CFSTR("RTW89AssocFail"), n);
	CFRelease(n);
	IOObjectRelease(svc);
	fprintf(stderr, "[airport] reported the failed connection to macOS (0x%x)\n", r);
}

void airport_data_active(int on)
{
	g_active = on;
	if (g_conn) { uint64_t v = on; IOConnectCallScalarMethod(g_conn, 1, &v, 1, NULL, NULL); }
}

/* publish the adapter MAC with the start trigger: the kext takes its interface address from it */
void airport_publish_start(const unsigned char mac[6])
{
	io_registry_entry_t r = IORegistryEntryFromPath(kIOMainPortDefault, "IOService:/IOResources");
	if (!r) return;
	CFDataRef d = CFDataCreate(kCFAllocatorDefault, mac, 6);
	IOReturn k = IORegistryEntrySetCFProperty(r, CFSTR("RTW89Airport"), d);
	CFRelease(d);
	IOObjectRelease(r);
	fprintf(stderr, "[airport] published the start trigger with the adapter MAC (0x%x)\n", k);
}

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

/* Scan results are merged over time: weak access points come and go between scans, which made the list flicker. */
#define CACHE_MAX 128
#define CACHE_TTL 45
static struct { struct rtw_scan_ent e; time_t seen; } g_cache[CACHE_MAX];
static int g_cache_n;
static time_t g_cache_scan_at;

static void cache_update(void)
{
	time_t now = time(NULL);
	mlme_lock();
	for (int i = 0; i < bss_n; i++) {
		const struct bss_entry *b = &bss_tab[i];
		int k;
		for (k = 0; k < g_cache_n; k++) if (!memcmp(g_cache[k].e.bssid, b->bssid, 6)) break;
		if (k == g_cache_n) { if (g_cache_n >= CACHE_MAX) continue; g_cache_n++; }
		struct rtw_scan_ent *e = &g_cache[k].e;
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
		g_cache[k].seen = now;
	}
	mlme_unlock();
	int w = 0;
	for (int k = 0; k < g_cache_n; k++) if (now - g_cache[k].seen <= CACHE_TTL) g_cache[w++] = g_cache[k];
	g_cache_n = w;
	g_cache_scan_at = now;
}

static void push_cache(io_service_t s)
{
	static struct rtw_scan_ent ent[RTW_AP_MAX_SCAN];
	int n = 0;
	/* strongest first, at most RTW_AP_MAX_SCAN */
	bool used[CACHE_MAX] = { false };
	while (n < RTW_AP_MAX_SCAN && n < g_cache_n) {
		int best = -1;
		for (int k = 0; k < g_cache_n; k++) if (!used[k] && (best < 0 || g_cache[k].e.rssi > g_cache[best].e.rssi)) best = k;
		if (best < 0) break;
		used[best] = true;
		ent[n++] = g_cache[best].e;
	}
	CFDataRef d = CFDataCreate(kCFAllocatorDefault, (const UInt8 *)ent, n * sizeof(ent[0]));
	IOReturn r = IORegistryEntrySetCFProperty(s, CFSTR("RTW89ScanResults"), d);
	CFRelease(d);
	IOReturn r2 = IORegistryEntrySetCFProperty(s, CFSTR("RTW89ScanDone"), kCFBooleanTrue);
	fprintf(stderr, "[airport] pushed %d scan results (0x%x, 0x%x)\n", n, r, r2);
}

static void push_scan(io_service_t s)
{
	cache_update();
	push_cache(s);
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
		const unsigned char *pmk = (!printable && rq.key_len == 32) ? rq.key : NULL;
		if (daemon_connect_request(ssid, psk, open, pmk) != 0)
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
	time_t disc_deadline = 0;
	long last_assoc_seen = -1;
	bool pending = false;
	unsigned long scans_at_request = 0;
	for (;;) {
		usleep(250000);
		if (!svc) { svc = find_kext(); if (!svc) continue; last = read_seq(svc); last_assoc = read_num(svc, CFSTR("RTW89AssocSeq")); last_disassoc = read_num(svc, CFSTR("RTW89DisassocSeq")); memset(&last_link, 0xff, sizeof last_link); fprintf(stderr, "[airport] kext found\n"); g_active = 0; data_connect(svc); continue; }
		if (!g_conn) data_connect(svc);
		long seq = read_seq(svc);
		if (IORegistryEntryGetRegistryEntryID(svc, &(uint64_t){0}) != KERN_SUCCESS) { IOObjectRelease(svc); svc = 0; continue; }	/* kext went away */
		if (seq != last) {
			last = seq;
			scans_at_request = daemon_scan_count();
			if (g_cache_n > 0 && time(NULL) - g_cache_scan_at <= 20) { push_cache(svc); pending = false; }	/* answer at once from the merged cache */
			else pending = true;
			daemon_request_scan();
			fprintf(stderr, "[airport] scan requested by the system (seq %ld)\n", seq);
		}
		long as = read_num(svc, CFSTR("RTW89AssocSeq"));
		if (as != last_assoc) { last_assoc = as; handle_assoc(svc); }
		long ds = read_num(svc, CFSTR("RTW89DisassocSeq"));
		if (ds != last_disassoc) { last_disassoc = ds; disc_deadline = time(NULL) + 2; fprintf(stderr, "[airport] system disconnect request (deferred 2 s)\n"); }
		if (as != last_assoc_seen) { last_assoc_seen = as; disc_deadline = 0; }	/* a connect request cancels the disconnect that precedes it */
		if (disc_deadline && time(NULL) >= disc_deadline) { disc_deadline = 0; fprintf(stderr, "[airport] applying system disconnect\n"); daemon_disconnect_request(); }
		if ((++tick & 3) == 0) push_link(svc, &last_link);
		if (g_active && tick % 20 == 0) fprintf(stderr, "[airport] data: tx %lu rx %lu\n", g_tx_n, g_rx_n);
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
