// SPDX-License-Identifier: BSD-3-Clause
// L2<->L3 glue: DHCP client, ARP, and forwarding between 802.11 data frames and a macOS utun interface.
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <net/if_utun.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include "net.h"
#include "net6.h"
#include "pend.h"
unsigned long net_wr_err;

static net_tx_fn g_tx;
static unsigned char g_mac[6];
static int g_utun = -1;
static char g_ifname[32];
static pthread_t g_utun_thr;
static volatile int g_run;

static struct {
	uint32_t ip, mask, gw, dns[3], server, lease;
	int ndns;
	bool have;
} cfg;

/* ARP cache (small, linear) */
struct arp_ent { uint32_t ip; unsigned char mac[6]; time_t t; };
static struct arp_ent arp_tab[64];
static pthread_mutex_t arp_lock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t dhcp_xid;
static int dhcp_state;		/* 0 idle, 1 wait offer, 2 wait ack, 3 bound */
static uint32_t offer_ip, offer_server;
static volatile bool bench_on;
typedef unsigned long long u64;
static void icmp_echo_rx(const unsigned char *ip, size_t len);
static pthread_mutex_t dhcp_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dhcp_cond = PTHREAD_COND_INITIALIZER;

void net_init(net_tx_fn tx, const unsigned char mac[6]) { g_tx = tx; memcpy(g_mac, mac, 6); }

/* frames that macOS sends through en3 (RTW89Airport.kext) go straight out; received frames can be diverted to en3 */
int (*net_rx_hook)(const unsigned char *eth, size_t len);
int net_tx_raw(const unsigned char *eth, size_t len)
{
	if (!g_tx || len < 14) return -1;
	return g_tx(eth, (unsigned short)((eth[12] << 8) | eth[13]), eth + 14, len - 14);
}

static uint16_t csum(const void *data, size_t len, uint32_t init)
{
	const uint8_t *p = data;
	uint32_t s = init;
	while (len > 1) { s += (p[0] << 8) | p[1]; p += 2; len -= 2; }
	if (len) s += p[0] << 8;
	while (s >> 16) s = (s & 0xffff) + (s >> 16);
	return ~s;
}

static struct pend pend4[PEND_MAX];
static pthread_mutex_t pend4_lock = PTHREAD_MUTEX_INITIALIZER;
static void arp_request(uint32_t ip);

/* ---------------- ARP ---------------- */
static void pend4_flush(uint32_t ip, const unsigned char *mac)
{
	pthread_mutex_lock(&pend4_lock);
	for (int i = 0; i < PEND_MAX; i++)
		if (pend4[i].used && !memcmp(pend4[i].key, &ip, 4)) {
			unsigned char *p = pend4[i].pkt; size_t l = pend4[i].len;
			pend4[i].used = 0; pend4[i].pkt = NULL;
			pthread_mutex_unlock(&pend4_lock);
			g_tx(mac, 0x0800, p, l);
			free(p);
			pthread_mutex_lock(&pend4_lock);
		}
	pthread_mutex_unlock(&pend4_lock);
}

static void arp_set(uint32_t ip, const unsigned char *mac)
{
	pthread_mutex_lock(&arp_lock);
	int free_i = -1, oldest = 0, hit = -1;
	for (int i = 0; i < 64; i++) {
		if (arp_tab[i].ip == ip) { hit = i; break; }
		if (!arp_tab[i].ip && free_i < 0) free_i = i;
		if (arp_tab[i].t < arp_tab[oldest].t) oldest = i;
	}
	int i = hit >= 0 ? hit : (free_i >= 0 ? free_i : oldest);
	arp_tab[i].ip = ip; memcpy(arp_tab[i].mac, mac, 6); arp_tab[i].t = time(NULL);
	pthread_mutex_unlock(&arp_lock);
	pend4_flush(ip, mac);
}

static bool arp_get(uint32_t ip, unsigned char *mac)
{
	bool ok = false;
	pthread_mutex_lock(&arp_lock);
	for (int i = 0; i < 64; i++)
		if (arp_tab[i].ip == ip) { memcpy(mac, arp_tab[i].mac, 6); ok = true; break; }
	pthread_mutex_unlock(&arp_lock);
	return ok;
}

static void arp_send(uint16_t op, const unsigned char *dst_mac, uint32_t spa, const unsigned char *tha, uint32_t tpa)
{
	unsigned char p[28];
	p[0] = 0; p[1] = 1; p[2] = 8; p[3] = 0; p[4] = 6; p[5] = 4;
	p[6] = op >> 8; p[7] = op & 0xff;
	memcpy(p + 8, g_mac, 6);
	memcpy(p + 14, &spa, 4);
	memcpy(p + 18, tha, 6);
	memcpy(p + 24, &tpa, 4);
	g_tx(dst_mac, 0x0806, p, sizeof p);
}

static void arp_request(uint32_t ip)
{
	static const unsigned char bc[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, zero[6] = {0};
	arp_send(1, bc, cfg.ip, zero, ip);
}

/* ---------------- DHCP ---------------- */
static size_t dhcp_build(unsigned char *pkt, int msgtype, uint32_t req_ip, uint32_t server)
{
	/* IPv4 + UDP + BOOTP */
	unsigned char *ip = pkt, *udp = pkt + 20, *b = pkt + 28;
	memset(b, 0, 300);
	b[0] = 1; b[1] = 1; b[2] = 6; b[3] = 0;
	b[4] = dhcp_xid >> 24; b[5] = dhcp_xid >> 16; b[6] = dhcp_xid >> 8; b[7] = dhcp_xid;
	b[10] = 0x80;			/* broadcast flag */
	memcpy(b + 28, g_mac, 6);
	b[236] = 99; b[237] = 130; b[238] = 83; b[239] = 99;
	unsigned char *o = b + 240;
	*o++ = 53; *o++ = 1; *o++ = msgtype;
	if (req_ip) { *o++ = 50; *o++ = 4; memcpy(o, &req_ip, 4); o += 4; }
	if (server) { *o++ = 54; *o++ = 4; memcpy(o, &server, 4); o += 4; }
	*o++ = 12; *o++ = 6; memcpy(o, "macrtw", 6); o += 6;
	*o++ = 55; *o++ = 5; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 15; *o++ = 51;
	*o++ = 255;
	size_t blen = o - b;
	if (blen < 300) blen = 300;
	size_t ulen = 8 + blen, iplen = 20 + ulen;
	memset(ip, 0, 20);
	ip[0] = 0x45; ip[2] = iplen >> 8; ip[3] = iplen & 0xff; ip[6] = 0x40; ip[8] = 64; ip[9] = 17;
	memset(ip + 12, 0, 4); memset(ip + 16, 0xff, 4);
	uint16_t c = csum(ip, 20, 0); ip[10] = c >> 8; ip[11] = c & 0xff;
	udp[0] = 0; udp[1] = 68; udp[2] = 0; udp[3] = 67; udp[4] = ulen >> 8; udp[5] = ulen & 0xff; udp[6] = udp[7] = 0;
	return iplen;
}

static void dhcp_send(int msgtype, uint32_t req_ip, uint32_t server)
{
	static const unsigned char bc[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	unsigned char pkt[400];
	size_t n = dhcp_build(pkt, msgtype, req_ip, server);
	g_tx(bc, 0x0800, pkt, n);
}

static void dhcp_rx(const unsigned char *ip, size_t len)
{
	size_t ihl = (ip[0] & 0xf) * 4;
	if (len < ihl + 8 + 240) return;
	const unsigned char *udp = ip + ihl, *b = udp + 8;
	if (b[0] != 2) return;
	uint32_t xid = (b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7];
	if (xid != dhcp_xid || memcmp(b + 28, g_mac, 6)) return;
	if (memcmp(b + 236, "\x63\x82\x53\x63", 4)) return;
	int type = 0;
	uint32_t mask = 0, gw = 0, server = 0, lease = 0, dns[3] = {0}; int nd = 0;
	const unsigned char *o = b + 240, *end = ip + len;
	while (o < end && *o != 255) {
		if (*o == 0) { o++; continue; }
		if (o + 2 > end || o + 2 + o[1] > end) break;
		unsigned char code = o[0], l = o[1];
		const unsigned char *v = o + 2;
		if (code == 53) type = v[0];
		else if (code == 1 && l == 4) memcpy(&mask, v, 4);
		else if (code == 3 && l >= 4) memcpy(&gw, v, 4);
		else if (code == 6) { for (int i = 0; i + 4 <= l && nd < 3; i += 4) memcpy(&dns[nd++], v + i, 4); }
		else if (code == 54 && l == 4) memcpy(&server, v, 4);
		else if (code == 51 && l == 4) lease = (v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3];
		o += 2 + l;
	}
	uint32_t yiaddr;
	memcpy(&yiaddr, b + 16, 4);
	pthread_mutex_lock(&dhcp_lock);
	if (type == 2 && dhcp_state == 1) {
		offer_ip = yiaddr; offer_server = server;
		dhcp_state = 2;
		pthread_cond_broadcast(&dhcp_cond);
	} else if (type == 5 && dhcp_state == 2) {
		cfg.ip = yiaddr; cfg.mask = mask ? mask : htonl(0xffffff00); cfg.gw = gw; cfg.server = server ? server : offer_server; cfg.lease = lease;
		cfg.ndns = nd; for (int i = 0; i < nd; i++) cfg.dns[i] = dns[i];
		cfg.have = true;
		dhcp_state = 3;
		pthread_cond_broadcast(&dhcp_cond);
	} else if (type == 6) {
		dhcp_state = 0;
		pthread_cond_broadcast(&dhcp_cond);
	}
	pthread_mutex_unlock(&dhcp_lock);
}

static int dhcp_run(int timeout_s)
{
	struct timespec ts;
	dhcp_xid = (uint32_t)random();
	pthread_mutex_lock(&dhcp_lock);
	dhcp_state = 1;
	pthread_mutex_unlock(&dhcp_lock);
	for (int attempt = 0; attempt < 6; attempt++) {
		dhcp_send(1, 0, 0);
		clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += timeout_s / 6 + 1;
		pthread_mutex_lock(&dhcp_lock);
		while (dhcp_state == 1) if (pthread_cond_timedwait(&dhcp_cond, &dhcp_lock, &ts)) break;
		int st = dhcp_state;
		pthread_mutex_unlock(&dhcp_lock);
		if (st == 2) break;
	}
	if (dhcp_state != 2) { fprintf(stderr, "[net] DHCP: no offer\n"); return -1; }
	for (int attempt = 0; attempt < 4; attempt++) {
		dhcp_send(3, offer_ip, offer_server);
		clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 2;
		pthread_mutex_lock(&dhcp_lock);
		while (dhcp_state == 2) if (pthread_cond_timedwait(&dhcp_cond, &dhcp_lock, &ts)) break;
		int st = dhcp_state;
		pthread_mutex_unlock(&dhcp_lock);
		if (st == 3) return 0;
		if (st == 0) return -1;
	}
	fprintf(stderr, "[net] DHCP: no ack\n");
	return -1;
}

/* ---------------- utun ---------------- */
static int utun_open(void)
{
	struct ctl_info ci;
	struct sockaddr_ctl sc;
	int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
	if (fd < 0) { perror("utun socket"); return -1; }
	memset(&ci, 0, sizeof ci);
	strlcpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof ci.ctl_name);
	if (ioctl(fd, CTLIOCGINFO, &ci) < 0) { perror("CTLIOCGINFO"); close(fd); return -1; }
	memset(&sc, 0, sizeof sc);
	sc.sc_len = sizeof sc; sc.sc_family = AF_SYSTEM; sc.ss_sysaddr = AF_SYS_CONTROL; sc.sc_id = ci.ctl_id; sc.sc_unit = 0;
	if (connect(fd, (struct sockaddr *)&sc, sizeof sc) < 0) { perror("utun connect (need root)"); close(fd); return -1; }
	socklen_t l = sizeof g_ifname;
	getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, g_ifname, &l);
	return fd;
}

static void pend4_add(uint32_t nh, const unsigned char *ip, size_t len)
{
	pthread_mutex_lock(&pend4_lock);
	int slot = -1, oldest = 0;
	for (int i = 0; i < PEND_MAX; i++) {
		if (!pend4[i].used) { slot = i; break; }
		if (pend4[i].last_ms < pend4[oldest].last_ms) oldest = i;
	}
	if (slot < 0) { slot = oldest; free(pend4[slot].pkt); }
	memset(&pend4[slot], 0, sizeof pend4[slot]);
	pend4[slot].used = 1;
	memcpy(pend4[slot].key, &nh, 4);
	pend4[slot].pkt = malloc(len);
	memcpy(pend4[slot].pkt, ip, len);
	pend4[slot].len = len;
	pend4[slot].last_ms = pend_now_ms();
	pthread_mutex_unlock(&pend4_lock);
	arp_request(nh);
}

/* retries ARP for queued packets and drops the ones that never resolve */
static void *pend_tick(void *arg)
{
	while (g_run) {
		usleep(100000);
		uint64_t now = pend_now_ms();
		pthread_mutex_lock(&pend4_lock);
		for (int i = 0; i < PEND_MAX; i++) {
			if (!pend4[i].used || now - pend4[i].last_ms < 250) continue;
			if (++pend4[i].tries > 3) { free(pend4[i].pkt); pend4[i].used = 0; continue; }
			pend4[i].last_ms = now;
			uint32_t nh; memcpy(&nh, pend4[i].key, 4);
			pthread_mutex_unlock(&pend4_lock);
			arp_request(nh);
			pthread_mutex_lock(&pend4_lock);
		}
		pthread_mutex_unlock(&pend4_lock);
		net6_tick();
	}
	return NULL;
}

static void *utun_reader(void *arg)
{
	unsigned char buf[4 + 2048];
	while (g_run) {
		struct pollfd pf = { .fd = g_utun, .events = POLLIN };
		if (poll(&pf, 1, 200) <= 0) continue;
		ssize_t n = read(g_utun, buf, sizeof buf);
		if (n <= 4) { if (n < 0 && errno == EINTR) continue; if (n < 0) break; continue; }
		uint32_t af = ntohl(*(uint32_t *)buf);
		if (af == AF_INET6) { net6_tx(buf + 4, n - 4); continue; }
		if (af != AF_INET) continue;
		const unsigned char *ip = buf + 4;
		size_t len = n - 4;
		if (len < 20) continue;
		uint32_t dst;
		memcpy(&dst, ip + 16, 4);
		uint32_t nh = ((dst & cfg.mask) == (cfg.ip & cfg.mask)) ? dst : cfg.gw;
		if (dst == (cfg.ip | ~cfg.mask)) {			/* subnet broadcast */
			static const unsigned char bc[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
			g_tx(bc, 0x0800, ip, len);
			continue;
		}
		unsigned char mac[6];
		if (arp_get(nh, mac)) g_tx(mac, 0x0800, ip, len);
		else pend4_add(nh, ip, len);
	}
	return NULL;
}

static int sh(const char *fmt, ...)
{
	char cmd[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);
	fprintf(stderr, "[net] $ %s\n", cmd);
	return system(cmd);
}

static const char *ntoa(uint32_t a) { struct in_addr x = { a }; return inet_ntoa(x); }

void net_write_utun6(const unsigned char *ip6, size_t len);
void net6_configure_linklocal(void);
static void v6_prefix_cb(const unsigned char *prefix, int plen, const unsigned char *router_ll);
static int g_set_default;
static pthread_t g_renew_thr, g_pend_thr;
static time_t g_bound_at;
void (*net_on_lease_lost)(void);

static void restore_default(void);
static void configure(void)
{
	char ip[32], gw[32], mask[32], net[32];
	strcpy(ip, ntoa(cfg.ip)); strcpy(gw, ntoa(cfg.gw)); strcpy(mask, ntoa(cfg.mask)); strcpy(net, ntoa(cfg.ip & cfg.mask));
	sh("/sbin/ifconfig %s inet %s %s netmask 255.255.255.255 mtu 1500 up", g_ifname, ip, ip);
	sh("/sbin/route -n add -net %s -netmask %s -interface %s", net, mask, g_ifname);
	{
		/* publish the link to configd (State:/Network/Service/rtw89): macOS then fails over to it when Ethernet goes away */
		FILE *f = popen("/usr/sbin/scutil", "w");
		if (f) {
			fprintf(f, "d.init\nd.add Addresses * %s\nd.add SubnetMasks * %s\nd.add InterfaceName %s\n", ip, mask, g_ifname);
			if (cfg.gw) fprintf(f, "d.add Router %s\n", gw);
			fprintf(f, "set State:/Network/Service/rtw89/IPv4\n");
			if (cfg.ndns) {
				fprintf(f, "d.init\nd.add ServerAddresses *");
				for (int i = 0; i < cfg.ndns; i++) fprintf(f, " %s", ntoa(cfg.dns[i]));
				fprintf(f, "\nd.add InterfaceName %s\nset State:/Network/Service/rtw89/DNS\n", g_ifname);
			}
			pclose(f);
		}
	}
	if (g_set_default && cfg.gw) net_set_default(1);
	else fprintf(stderr, "[net] Wi-Fi is registered as a standby connection (macOS uses it when Ethernet is unavailable). Test: ping -b %s %s\n", g_ifname, ntoa(cfg.gw));
}

static int dhcp_renew(void)
{
	struct timespec ts;
	pthread_mutex_lock(&dhcp_lock);
	dhcp_xid = (uint32_t)random();
	offer_ip = cfg.ip; offer_server = cfg.server;
	dhcp_state = 2;
	pthread_mutex_unlock(&dhcp_lock);
	for (int attempt = 0; attempt < 4; attempt++) {
		dhcp_send(3, offer_ip, offer_server);
		clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 2;
		pthread_mutex_lock(&dhcp_lock);
		while (dhcp_state == 2) if (pthread_cond_timedwait(&dhcp_cond, &dhcp_lock, &ts)) break;
		int st = dhcp_state;
		pthread_mutex_unlock(&dhcp_lock);
		if (st == 3) return 0;
		if (st == 0) return -1;
	}
	return -1;
}

static void *renew_thread(void *arg)
{
	while (g_run) {
		sleep(1);
		if (!cfg.have || !cfg.lease) continue;
		time_t age = time(NULL) - g_bound_at;
		if (age < (time_t)cfg.lease / 2) continue;
		fprintf(stderr, "[net] renewing DHCP lease\n");
		if (dhcp_renew() == 0) { g_bound_at = time(NULL); continue; }
		if (age > (time_t)cfg.lease) {
			fprintf(stderr, "[net] lease expired\n");
			cfg.have = false;
			if (net_on_lease_lost) net_on_lease_lost();
			break;
		}
		sleep(10);
	}
	return NULL;
}

int net_is_up(void) { return cfg.have && g_utun >= 0; }
void net_get_ip(char *buf, size_t n) { if (cfg.have) { struct in_addr x = { cfg.ip }; snprintf(buf, n, "%s", inet_ntoa(x)); } else buf[0] = 0; }

int net_up(int timeout_s, int set_default)
{
	g_set_default = set_default;
	if (dhcp_run(timeout_s)) return -1;
	g_bound_at = time(NULL);
	fprintf(stderr, "[net] DHCP bound: ip %s", ntoa(cfg.ip));
	fprintf(stderr, " mask %s", ntoa(cfg.mask));
	fprintf(stderr, " gw %s lease %us\n", ntoa(cfg.gw), cfg.lease);
	g_utun = utun_open();
	if (g_utun < 0) return -2;
	fprintf(stderr, "[net] utun interface %s\n", g_ifname);
	configure();
	arp_send(2, (const unsigned char *)"\xff\xff\xff\xff\xff\xff", cfg.ip, (const unsigned char *)"\xff\xff\xff\xff\xff\xff", cfg.ip);	/* gratuitous */
	if (cfg.gw) arp_request(cfg.gw);
	net6_init(g_tx, g_mac, g_ifname, net_write_utun6);
	net6_configure_linklocal();
	g_run = 1;
	pthread_create(&g_utun_thr, NULL, utun_reader, NULL);
	sleep(1);
	net6_send_rs();
	pthread_create(&g_renew_thr, NULL, renew_thread, NULL);
	pthread_create(&g_pend_thr, NULL, pend_tick, NULL);
	return 0;
}

void net_down(void)
{
	int was_up = g_utun >= 0;
	restore_default();
	g_run = 0;
	if (g_utun >= 0) { close(g_utun); g_utun = -1; }
	if (was_up) { pthread_join(g_utun_thr, NULL); pthread_join(g_renew_thr, NULL); pthread_join(g_pend_thr, NULL); }
	FILE *f = popen("/usr/sbin/scutil", "w");
	if (f) { fprintf(f, "remove State:/Network/Service/rtw89/DNS\nremove State:/Network/Service/rtw89/IPv4\n"); pclose(f); }
	cfg.have = false;
	dhcp_state = 0;
}

/* ---------------- receive ---------------- */
void net_rx_eth(const unsigned char *eth, size_t len)
{
	if (len < 14) return;
	if (net_rx_hook && net_rx_hook(eth, len)) return;
	uint16_t et = (eth[12] << 8) | eth[13];
	const unsigned char *p = eth + 14;
	size_t pl = len - 14;
	if (et == 0x0806 && pl >= 28) {
		uint16_t op = (p[6] << 8) | p[7];
		uint32_t spa, tpa;
		memcpy(&spa, p + 14, 4); memcpy(&tpa, p + 24, 4);
		if (spa) arp_set(spa, p + 8);
		if (op == 1 && cfg.have && tpa == cfg.ip)
			arp_send(2, p + 8, cfg.ip, p + 8, spa);
		return;
	}
	if (et == 0x86dd) { net6_rx(eth, len); return; }
	if (et == 0x0800 && pl >= 20) {
		uint8_t proto = p[9];
		if (bench_on) icmp_echo_rx(p, pl);
		size_t ihl = (p[0] & 0xf) * 4;
		if (proto == 17 && pl >= ihl + 8 && p[ihl + 3] == 68) { dhcp_rx(p, pl); return; }
		if (g_utun >= 0 && cfg.have) {
			unsigned char buf[4 + 2048];
			if (pl > 2048) return;
			buf[0] = buf[1] = buf[2] = 0; buf[3] = AF_INET;
			memcpy(buf + 4, p, pl);
			if (write(g_utun, buf, pl + 4) < 0) net_wr_err++;
		}
	}
}

/* ---- primary-connection switch: swap the default route, remember the original and revert if the new path has no internet ---- */
static char g_orig_gw[64], g_orig_if[32];
static int g_swapped;

static void read_default(void)
{
	g_orig_gw[0] = g_orig_if[0] = 0;
	FILE *p = popen("/sbin/route -n get default 2>/dev/null", "r");
	char line[256];
	while (p && fgets(line, sizeof line, p)) {
		char v[64];
		if (sscanf(line, " gateway: %63s", v) == 1) strlcpy(g_orig_gw, v, sizeof g_orig_gw);
		else if (sscanf(line, " interface: %31s", v) == 1) strlcpy(g_orig_if, v, sizeof g_orig_if);
	}
	if (p) pclose(p);
}

static void restore_default(void)
{
	if (!g_swapped) return;
	sh("/sbin/route -n delete default -interface %s", g_ifname);
	if (g_orig_gw[0] && strcmp(g_orig_if, g_ifname)) sh("/sbin/route -n add default %s", g_orig_gw);
	else if (g_orig_if[0] && strcmp(g_orig_if, g_ifname)) sh("/sbin/route -n add default -interface %s", g_orig_if);
	g_swapped = 0;
}

void net_set_default(int on)
{
	if (g_utun < 0 || !cfg.have) { g_set_default = on; return; }
	g_set_default = on;
	if (!on) { restore_default(); return; }
	if (g_swapped) return;
	read_default();
	fprintf(stderr, "[net] original default route: via %s on %s\n", g_orig_gw, g_orig_if);
	sh("/sbin/route -n delete default");
	sh("/sbin/route -n add default -interface %s", g_ifname);
	g_swapped = 1;
	if (system("/sbin/ping -c 2 -t 4 1.1.1.1 >/dev/null 2>&1") != 0) {
		fprintf(stderr, "[net] no internet through the Wi-Fi link: restoring the previous default route\n");
		g_set_default = 0;
		g_swapped = 1;
		restore_default();
	}
}

/* IPv6 packets heading for the macOS stack */
void net_write_utun6(const unsigned char *ip6, size_t len)
{
	unsigned char buf[4 + 2048];
	if (g_utun < 0 || len > 2048) return;
	buf[0] = buf[1] = buf[2] = 0; buf[3] = AF_INET6;
	memcpy(buf + 4, ip6, len);
	if (write(g_utun, buf, len + 4) < 0) {}
}

/* ---- IPv6 addressing: link-local + SLAAC (EUI-64) from router advertisements ---- */
static unsigned char g_v6_prefix[16];
static bool g_v6_have;

static void eui64(const unsigned char *prefix, unsigned char *out)
{
	memcpy(out, prefix, 8);
	out[8] = g_mac[0] ^ 2; out[9] = g_mac[1]; out[10] = g_mac[2]; out[11] = 0xff; out[12] = 0xfe;
	out[13] = g_mac[3]; out[14] = g_mac[4]; out[15] = g_mac[5];
}

static void v6_prefix_cb(const unsigned char *prefix, int plen, const unsigned char *router_ll)
{
	if (g_v6_have && !memcmp(g_v6_prefix, prefix, 8)) return;	/* already configured for this prefix */
	unsigned char addr[16];
	char a[64], r[64];
	eui64(prefix, addr);
	inet_ntop(AF_INET6, addr, a, sizeof a);
	inet_ntop(AF_INET6, router_ll, r, sizeof r);
	memcpy(g_v6_prefix, prefix, 8);
	g_v6_have = true;
	sh("/sbin/ifconfig %s inet6 %s prefixlen %d", g_ifname, a, plen);
	sh("/sbin/route -n add -inet6 default -interface %s", g_ifname);
	fprintf(stderr, "[net] IPv6 address %s/%d (router %s)\n", a, plen, r);
}

void net6_configure_linklocal(void)
{
	static const unsigned char ll[8] = { 0xfe, 0x80 };
	unsigned char addr[16];
	char a[64];
	eui64(ll, addr);
	inet_ntop(AF_INET6, addr, a, sizeof a);
	sh("/sbin/ifconfig %s inet6 %s prefixlen 64", g_ifname, a);
	g_v6_have = false;
	net6_set_prefix_cb(v6_prefix_cb);
}

/* ---- bench: ICMP echo flood to the gateway (measures the USB + driver + radio path without the internet) ---- */
static volatile unsigned long bench_rx;

static void icmp_echo_rx(const unsigned char *ip, size_t len)
{
	size_t ihl = (ip[0] & 0xf) * 4;
	if (len < ihl + 8 || ip[9] != 1) return;
	if (ip[ihl] == 0 && ip[ihl + 4] == 0xbe && ip[ihl + 5] == 0xef) bench_rx++;
}

int net_bench(int seconds, int size, char *out, size_t outn)
{
	if (!cfg.have) { snprintf(out, outn, "not connected"); return -1; }
	unsigned char mac[6];
	for (int i = 0; i < 10 && !arp_get(cfg.gw, mac); i++) { arp_request(cfg.gw); usleep(100000); }
	if (!arp_get(cfg.gw, mac)) { snprintf(out, outn, "gateway MAC unknown"); return -1; }
	if (size < 64) size = 64;
	if (size > 1472) size = 1472;
	unsigned char pkt[1600];
	size_t ilen = 20 + 8 + size;
	memset(pkt, 0, sizeof pkt);
	pkt[0] = 0x45; pkt[2] = ilen >> 8; pkt[3] = ilen & 0xff; pkt[8] = 64; pkt[9] = 1;
	memcpy(pkt + 12, &cfg.ip, 4); memcpy(pkt + 16, &cfg.gw, 4);
	pkt[20] = 8; pkt[24] = 0xbe; pkt[25] = 0xef;
	for (int i = 0; i < size; i++) pkt[28 + i] = (unsigned char)i;
	bench_rx = 0; bench_on = true;
	unsigned long sent = 0;
	uint64_t t0 = pend_now_ms(), end = t0 + seconds * 1000ULL;
	while (pend_now_ms() < end) {
		if (sent - bench_rx > 48) { usleep(200); continue; }	/* window */
		pkt[26] = sent >> 8; pkt[27] = sent & 0xff;
		pkt[10] = pkt[11] = 0;
		uint16_t c = csum(pkt, 20, 0); pkt[10] = c >> 8; pkt[11] = c & 0xff;
		pkt[22] = pkt[23] = 0;
		c = csum(pkt + 20, 8 + size, 0); pkt[22] = c >> 8; pkt[23] = c & 0xff;
		g_tx(mac, 0x0800, pkt, ilen);
		sent++;
	}
	usleep(300000);
	bench_on = false;
	double el = (pend_now_ms() - t0) / 1000.0;
	double bytes = (double)bench_rx * (ilen + 14);
	extern u64 mm_stat_lock_ns, mm_stat_ops_ns, mm_stat_n;
	char st[160] = "";
	if (mm_stat_n) snprintf(st, sizeof st, " [tx: %.0f us waiting for lock, %.0f us in driver per frame]", mm_stat_lock_ns / 1000.0 / mm_stat_n, mm_stat_ops_ns / 1000.0 / mm_stat_n);
	snprintf(out, outn, "sent %lu, received %lu (loss %.1f%%), %.1f s, %d-byte payload: %.1f Mbit/s each way (%.0f pkt/s)",
		 sent, (unsigned long)bench_rx, sent ? 100.0 * (sent - bench_rx) / sent : 0.0, el, size, bytes * 8 / el / 1e6, bench_rx / el);
	strlcat(out, st, outn);
	return 0;
}
