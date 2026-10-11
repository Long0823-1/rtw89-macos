// SPDX-License-Identifier: BSD-3-Clause
// IPv6 over the Wi-Fi link: neighbour discovery on behalf of the macOS utun interface (which has no link layer).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "net.h"
#include "net6.h"

static net_tx_fn g_tx6;
static unsigned char g_mac6[6];
static char g_if6[32];
static net6_write_fn g_write6;

struct nd_ent { unsigned char ip[16]; unsigned char mac[6]; time_t t; };
static struct nd_ent nd_tab[64];
static pthread_mutex_t nd_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char router_mac[6];
static unsigned char router_ll[16];
static bool have_router;
static net6_prefix_fn g_prefix_cb;
void net6_set_prefix_cb(net6_prefix_fn cb) { g_prefix_cb = cb; }

void net6_init(net_tx_fn tx, const unsigned char mac[6], const char *ifname, net6_write_fn wr)
{
	g_tx6 = tx; memcpy(g_mac6, mac, 6); snprintf(g_if6, sizeof g_if6, "%s", ifname); g_write6 = wr;
	memset(nd_tab, 0, sizeof nd_tab);
	have_router = false;
}

static uint16_t csum16(const unsigned char *src, const unsigned char *dst, const unsigned char *icmp, size_t len)
{
	uint32_t s = 0;
	for (int i = 0; i < 16; i += 2) { s += (src[i] << 8) | src[i + 1]; s += (dst[i] << 8) | dst[i + 1]; }
	s += len >> 16; s += len & 0xffff; s += 58;
	for (size_t i = 0; i + 1 < len; i += 2) s += (icmp[i] << 8) | icmp[i + 1];
	if (len & 1) s += icmp[len - 1] << 8;
	while (s >> 16) s = (s & 0xffff) + (s >> 16);
	return ~s;
}

static void pend6_flush(const unsigned char *ip, const unsigned char *mac);
static void nd_set_raw(const unsigned char *ip, const unsigned char *mac)
{
	pthread_mutex_lock(&nd_lock);
	int free_i = -1, old = 0;
	for (int i = 0; i < 64; i++) {
		if (!memcmp(nd_tab[i].ip, ip, 16)) { memcpy(nd_tab[i].mac, mac, 6); nd_tab[i].t = time(NULL); pthread_mutex_unlock(&nd_lock); return; }
		bool empty = nd_tab[i].t == 0;
		if (empty && free_i < 0) free_i = i;
		if (nd_tab[i].t < nd_tab[old].t) old = i;
	}
	int i = free_i >= 0 ? free_i : old;
	memcpy(nd_tab[i].ip, ip, 16); memcpy(nd_tab[i].mac, mac, 6); nd_tab[i].t = time(NULL);
	pthread_mutex_unlock(&nd_lock);
}

static void nd_set(const unsigned char *ip, const unsigned char *mac)
{
	nd_set_raw(ip, mac);
	pend6_flush(ip, mac);
}

static bool nd_get(const unsigned char *ip, unsigned char *mac)
{
	bool ok = false;
	pthread_mutex_lock(&nd_lock);
	for (int i = 0; i < 64; i++)
		if (nd_tab[i].t && !memcmp(nd_tab[i].ip, ip, 16)) { memcpy(mac, nd_tab[i].mac, 6); ok = true; break; }
	pthread_mutex_unlock(&nd_lock);
	return ok;
}

static bool is_mcast(const unsigned char *a) { return a[0] == 0xff; }
static bool is_ll(const unsigned char *a) { return a[0] == 0xfe && (a[1] & 0xc0) == 0x80; }

/* send an ICMPv6 packet (hop limit 255) */
static void icmp6_send(const unsigned char *dst_mac, const unsigned char *src, const unsigned char *dst, const unsigned char *icmp, size_t len)
{
	unsigned char pkt[200];
	if (40 + len > sizeof pkt) return;
	memset(pkt, 0, 40);
	pkt[0] = 0x60;
	pkt[4] = len >> 8; pkt[5] = len & 0xff; pkt[6] = 58; pkt[7] = 255;
	memcpy(pkt + 8, src, 16); memcpy(pkt + 24, dst, 16);
	memcpy(pkt + 40, icmp, len);
	pkt[42] = pkt[43] = 0;
	uint16_t c = csum16(src, dst, pkt + 40, len);
	pkt[42] = c >> 8; pkt[43] = c & 0xff;
	g_tx6(dst_mac, 0x86dd, pkt, 40 + len);
}

static void solicited_node(const unsigned char *target, unsigned char *out_ip, unsigned char *out_mac)
{
	memset(out_ip, 0, 16);
	out_ip[0] = 0xff; out_ip[1] = 0x02; out_ip[11] = 0x01; out_ip[12] = 0xff;
	memcpy(out_ip + 13, target + 13, 3);
	out_mac[0] = 0x33; out_mac[1] = 0x33; memcpy(out_mac + 2, out_ip + 12, 4);
}

/* find a source address on the utun interface: prefer link-local */
static bool utun_addr6(unsigned char *out, bool want_ll)
{
	struct ifaddrs *ifa, *p;
	bool found = false;
	if (getifaddrs(&ifa)) return false;
	for (p = ifa; p; p = p->ifa_next) {
		if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET6 || strcmp(p->ifa_name, g_if6)) continue;
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)p->ifa_addr;
		unsigned char *a = s->sin6_addr.s6_addr;
		if (want_ll != is_ll(a)) continue;
		memcpy(out, a, 16);
		if (is_ll(out)) { out[2] = out[3] = 0; }	/* strip KAME scope id */
		found = true;
		break;
	}
	freeifaddrs(ifa);
	return found;
}

static bool addr_is_ours(const unsigned char *t)
{
	struct ifaddrs *ifa, *p;
	bool ours = false;
	if (getifaddrs(&ifa)) return false;
	for (p = ifa; p; p = p->ifa_next) {
		if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET6 || strcmp(p->ifa_name, g_if6)) continue;
		unsigned char a[16];
		memcpy(a, ((struct sockaddr_in6 *)p->ifa_addr)->sin6_addr.s6_addr, 16);
		if (is_ll(a)) { a[2] = a[3] = 0; }
		unsigned char tt[16];
		memcpy(tt, t, 16);
		if (is_ll(tt)) { tt[2] = tt[3] = 0; }
		if (!memcmp(a, tt, 16)) { ours = true; break; }
	}
	freeifaddrs(ifa);
	return ours;
}

static void send_ns(const unsigned char *target, const unsigned char *src)
{
	unsigned char icmp[32] = {0}, dst[16], mac[6];
	icmp[0] = 135;
	memcpy(icmp + 8, target, 16);
	icmp[24] = 1; icmp[25] = 1; memcpy(icmp + 26, g_mac6, 6);
	solicited_node(target, dst, mac);
	icmp6_send(mac, src, dst, icmp, 32);
}

void net6_send_rs(void)
{
	unsigned char src[16], icmp[16] = {0}, dst[16] = {0xff, 0x02, [15] = 0x02}, mac[6] = {0x33, 0x33, 0, 0, 0, 2};
	if (!utun_addr6(src, true)) { memset(src, 0, 16); }
	icmp[0] = 133;
	icmp[8] = 1; icmp[9] = 1; memcpy(icmp + 10, g_mac6, 6);
	icmp6_send(mac, src, dst, icmp, 16);
}

/* utun -> Wi-Fi: resolve the link-layer destination and transmit (never blocks: unresolved packets are queued) */
#include "pend.h"
static struct pend pend6[PEND_MAX];
static pthread_mutex_t pend6_lock = PTHREAD_MUTEX_INITIALIZER;

static void pend6_flush(const unsigned char *ip, const unsigned char *mac)
{
	pthread_mutex_lock(&pend6_lock);
	for (int i = 0; i < PEND_MAX; i++)
		if (pend6[i].used && !memcmp(pend6[i].key, ip, 16)) {
			unsigned char *p = pend6[i].pkt; size_t l = pend6[i].len;
			pend6[i].used = 0; pend6[i].pkt = NULL;
			pthread_mutex_unlock(&pend6_lock);
			g_tx6(mac, 0x86dd, p, l);
			free(p);
			pthread_mutex_lock(&pend6_lock);
		}
	pthread_mutex_unlock(&pend6_lock);
}

void net6_tick(void)
{
	uint64_t now = pend_now_ms();
	pthread_mutex_lock(&pend6_lock);
	for (int i = 0; i < PEND_MAX; i++) {
		if (!pend6[i].used || now - pend6[i].last_ms < 250) continue;
		if (++pend6[i].tries > 3) { free(pend6[i].pkt); pend6[i].used = 0; continue; }
		pend6[i].last_ms = now;
		unsigned char key[16], src[16];
		memcpy(key, pend6[i].key, 16); memcpy(src, pend6[i].src, 16);
		pthread_mutex_unlock(&pend6_lock);
		send_ns(key, src);
		pthread_mutex_lock(&pend6_lock);
	}
	pthread_mutex_unlock(&pend6_lock);
}

void net6_tx(const unsigned char *ip6, size_t len)
{
	if (len < 40) return;
	const unsigned char *dst = ip6 + 24, *src = ip6 + 8;
	unsigned char mac[6];
	if (is_mcast(dst)) {
		mac[0] = 0x33; mac[1] = 0x33; memcpy(mac + 2, dst + 12, 4);
		g_tx6(mac, 0x86dd, ip6, len);
		return;
	}
	unsigned char nh[16];
	if (is_ll(dst)) memcpy(nh, dst, 16);
	else if (have_router) memcpy(nh, router_ll, 16);
	else memcpy(nh, dst, 16);
	if (is_ll(nh)) { nh[2] = nh[3] = 0; }
	bool ok = nd_get(nh, mac);
	if (!ok && have_router && !memcmp(nh, router_ll, 16)) { memcpy(mac, router_mac, 6); ok = true; }
	if (ok) { g_tx6(mac, 0x86dd, ip6, len); return; }
	pthread_mutex_lock(&pend6_lock);
	int slot = -1, oldest = 0;
	for (int i = 0; i < PEND_MAX; i++) {
		if (!pend6[i].used) { slot = i; break; }
		if (pend6[i].last_ms < pend6[oldest].last_ms) oldest = i;
	}
	if (slot < 0) { slot = oldest; free(pend6[slot].pkt); }
	memset(&pend6[slot], 0, sizeof pend6[slot]);
	pend6[slot].used = 1;
	memcpy(pend6[slot].key, nh, 16); memcpy(pend6[slot].src, src, 16);
	pend6[slot].pkt = malloc(len); memcpy(pend6[slot].pkt, ip6, len);
	pend6[slot].len = len; pend6[slot].last_ms = pend_now_ms();
	pthread_mutex_unlock(&pend6_lock);
	send_ns(nh, src);
}

/* Wi-Fi -> utun */
void net6_rx(const unsigned char *eth, size_t len)
{
	if (len < 14 + 40) return;
	const unsigned char *ip6 = eth + 14;
	size_t plen = (ip6[4] << 8) | ip6[5];
	if (40 + plen > len - 14) return;
	if (ip6[6] == 58 && plen >= 8) {
		const unsigned char *ic = ip6 + 40;
		const unsigned char *src = ip6 + 8;
		if (ic[0] == 136 && plen >= 24) {			/* neighbour advertisement */
			const unsigned char *target = ic + 8;
			unsigned char mac[6];
			memcpy(mac, eth + 6, 6);
			for (size_t o = 24; o + 8 <= plen;) {
				if (ic[o] == 2 && ic[o + 1] == 1) { memcpy(mac, ic + o + 2, 6); break; }
				if (!ic[o + 1]) break;
				o += ic[o + 1] * 8;
			}
			unsigned char t[16]; memcpy(t, target, 16);
			if (is_ll(t)) { t[2] = t[3] = 0; }
			nd_set(t, mac);
		} else if (ic[0] == 135 && plen >= 24) {		/* neighbour solicitation */
			const unsigned char *target = ic + 8;
			if (!(src[0] == 0 && src[15] == 0)) {
				unsigned char smac[6]; memcpy(smac, eth + 6, 6);
				for (size_t o = 24; o + 8 <= plen;) {
					if (ic[o] == 1 && ic[o + 1] == 1) { memcpy(smac, ic + o + 2, 6); break; }
					if (!ic[o + 1]) break;
					o += ic[o + 1] * 8;
				}
				unsigned char s[16]; memcpy(s, src, 16);
				if (is_ll(s)) { s[2] = s[3] = 0; }
				nd_set(s, smac);
			}
			if (addr_is_ours(target)) {
				unsigned char na[32] = {0};
				na[0] = 136; na[4] = 0x60;			/* solicited + override */
				memcpy(na + 8, target, 16);
				na[24] = 2; na[25] = 1; memcpy(na + 26, g_mac6, 6);
				unsigned char dstmac[6]; memcpy(dstmac, eth + 6, 6);
				icmp6_send(dstmac, target, src, na, 32);
			}
			return;							/* answered locally */
		} else if (ic[0] == 134) {				/* router advertisement: remember the router, configure SLAAC prefixes */
			unsigned short life = (ic[6] << 8) | ic[7];
			if (life && is_ll(src)) {
				memcpy(router_ll, src, 16); router_ll[2] = router_ll[3] = 0;
				memcpy(router_mac, eth + 6, 6);
				have_router = true;
				nd_set(router_ll, router_mac);
				if (g_prefix_cb) {
					for (size_t o = 16; o + 8 <= plen;) {
						unsigned char type = ic[o], l8 = ic[o + 1];
						if (!l8) break;
						if (type == 3 && l8 >= 4 && (ic[o + 3] & 0x40) && ic[o + 2] == 64)
							g_prefix_cb(ic + o + 16, 64, router_ll);
						o += l8 * 8;
					}
				}
			}
		}
	}
	if (g_write6) g_write6(ip6, 40 + plen);
}
