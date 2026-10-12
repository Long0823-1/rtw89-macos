// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// rtw89d daemon: owns the adapter, auto-connects to known networks, and serves a control socket for the menu bar app / CLI.
#include "mm.h"
#include "mlme.h"
#include "datapath.h"
#include "net.h"
#include "daemon.h"
#include <linux/usb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>

static time_t g_daemon_start;

extern void kc_runtime_init(void);
extern int kc_usb_find_and_probe(const struct usb_driver *drv);
extern void kc_usb_close(void);
extern int kc_usb_present(u16 vid, u16 pid);
extern int kc_usb_modeswitch(void);

extern unsigned long net_wr_err;
extern unsigned long st_rx_undec_group, st_rx_undec_uni;
extern unsigned long st_rx_frames, st_rx_data, st_rx_bytes, st_rx_undecrypted;
enum dstate { D_STARTING, D_NO_ADAPTER, D_IDLE, D_SCANNING, D_CONNECTING, D_CONNECTED, D_FAILED };
static const char *dstate_name[] = { "starting", "no_adapter", "idle", "scanning", "connecting", "connected", "failed" };

static struct {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	enum dstate state;
	char want_ssid[33], want_psk[65];
	bool want_active, want_open, scan_req, link_lost, route_default, autoconnect;
	char cur_ssid[33], err[96];
	time_t connected_at;
	bool link_up;		/* 802.11 link established (handshake done), before DHCP */
	bool sys_assoc;		/* the current link was requested by macOS (it knows about it) */
	bool want_sys;		/* the pending connect request comes from macOS */
	unsigned char want_pmk[32];
	bool want_pmk_set;
} D = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .route_default = false, .autoconnect = true };

/* ---------------- known networks (root-only file) ---------------- */
#define MAX_KNOWN 32
static struct { char ssid[33]; char psk[65]; bool open; bool has_pmk; unsigned char pmk[32]; } known[MAX_KNOWN];
static int nknown;
static const char *known_path(void) { const char *e = getenv("RTW89_KNOWN"); return e ? e : "/var/db/rtw89d/known.conf"; }

static void hex_enc(const char *in, size_t n, char *out) { for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", (unsigned char)in[i]); out[2 * n] = 0; }
static int hex_dec(const char *in, char *out, size_t max)
{
	size_t n = strlen(in) / 2;
	if (n >= max) return -1;
	for (size_t i = 0; i < n; i++) { unsigned v; if (sscanf(in + 2 * i, "%2x", &v) != 1) return -1; out[i] = (char)v; }
	out[n] = 0;
	return (int)n;
}

static void known_load(void)
{
	FILE *f = fopen(known_path(), "r");
	char line[512];
	nknown = 0;
	if (!f) return;
	while (fgets(line, sizeof line, f) && nknown < MAX_KNOWN) {
		char a[80], b[160];
		if (sscanf(line, "%79s %159s", a, b) < 1) continue;
		if (hex_dec(a, known[nknown].ssid, sizeof known[0].ssid) < 0) continue;
		known[nknown].open = !strcmp(b, "-");
		known[nknown].psk[0] = 0;
		known[nknown].has_pmk = false;
		if (!strncmp(b, "pmk:", 4)) {
			/* a PMK that macOS handed over when the network was joined from System Settings (no passphrase known) */
			char tmp[40];
			if (strlen(b + 4) != 64 || hex_dec(b + 4, tmp, sizeof tmp) != 32) continue;
			memcpy(known[nknown].pmk, tmp, 32);
			known[nknown].has_pmk = true;
		} else if (!known[nknown].open && hex_dec(b, known[nknown].psk, sizeof known[0].psk) < 0) continue;
		nknown++;
	}
	fclose(f);
}

static void known_save(void)
{
	char dir[256];
	snprintf(dir, sizeof dir, "%s", known_path());
	char *s = strrchr(dir, '/');
	if (s) { *s = 0; mkdir(dir, 0700); }
	mode_t old = umask(077);
	FILE *f = fopen(known_path(), "w");
	umask(old);
	if (!f) return;
	for (int i = 0; i < nknown; i++) {
		char a[80], b[160];
		hex_enc(known[i].ssid, strlen(known[i].ssid), a);
		if (known[i].open) strcpy(b, "-");
		else if (!known[i].psk[0] && known[i].has_pmk) { strcpy(b, "pmk:"); hex_enc((const char *)known[i].pmk, 32, b + 4); }
		else hex_enc(known[i].psk, strlen(known[i].psk), b);
		fprintf(f, "%s %s\n", a, b);
	}
	fclose(f);
	chmod(known_path(), 0600);
}

static void known_add(const char *ssid, const char *psk, bool open)
{
	int i;
	for (i = 0; i < nknown; i++) if (!strcmp(known[i].ssid, ssid)) break;
	if (i == nknown) { if (nknown >= MAX_KNOWN) return; nknown++; }
	snprintf(known[i].ssid, sizeof known[i].ssid, "%s", ssid);
	snprintf(known[i].psk, sizeof known[i].psk, "%s", psk ? psk : "");
	known[i].open = open;
	known[i].has_pmk = false;
	known_save();
}

static void known_add_pmk(const char *ssid, const unsigned char *pmk)
{
	int i;
	for (i = 0; i < nknown; i++) if (!strcmp(known[i].ssid, ssid)) break;
	if (i < nknown && known[i].psk[0]) return;		/* a passphrase is better than a PMK */
	if (i == nknown) { if (nknown >= MAX_KNOWN) return; nknown++; known[i].psk[0] = 0; }
	snprintf(known[i].ssid, sizeof known[i].ssid, "%s", ssid);
	known[i].open = false;
	known[i].has_pmk = true;
	memcpy(known[i].pmk, pmk, 32);
	known_save();
}

static void known_forget(const char *ssid)
{
	for (int i = 0; i < nknown; i++)
		if (!strcmp(known[i].ssid, ssid)) { known[i] = known[--nknown]; known_save(); return; }
}

/* ---------------- helpers ---------------- */
static void set_state(enum dstate s, const char *err)
{
	pthread_mutex_lock(&D.lock);
	D.state = s;
	snprintf(D.err, sizeof D.err, "%s", err ? err : "");
	pthread_mutex_unlock(&D.lock);
}

static int tx_cb(const unsigned char *da, unsigned short ethertype, const unsigned char *payload, size_t len)
{
	if (!g_mlme.sta || g_mlme.state < MLME_ASSOCIATED) return -1;
	return dp_tx_frame(da, ethertype, payload, len, false);
}

static void on_disc(void)
{
	pthread_mutex_lock(&D.lock);
	D.link_lost = true;
	pthread_cond_broadcast(&D.cond);
	pthread_mutex_unlock(&D.lock);
}

static void on_lease_lost(void) { on_disc(); }

static void json_str(FILE *f, const char *s)
{
	fputc('"', f);
	for (; *s; s++) {
		unsigned char c = *s;
		if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
		else if (c < 0x20) fprintf(f, "\\u%04x", c);
		else fputc(c, f);
	}
	fputc('"', f);
}

/* ---------------- control socket ---------------- */
static void cmd_status(FILE *f)
{
	char ip[32] = "", bssid[32] = "";
	int chan = 0, sig = 0;
	pthread_mutex_lock(&D.lock);
	enum dstate st = D.state;
	char ssid[33], err[96];
	snprintf(ssid, sizeof ssid, "%s", D.cur_ssid);
	snprintf(err, sizeof err, "%s", D.err);
	bool route = D.route_default;
	time_t since = D.connected_at;
	pthread_mutex_unlock(&D.lock);
	if (st == D_CONNECTED) { net_get_ip(ip, sizeof ip); mlme_cur_info(bssid, sizeof bssid, &chan, &sig); }
	fprintf(f, "{\"state\":\"%s\",\"ssid\":", dstate_name[st]);
	json_str(f, ssid);
	fprintf(f, ",\"ip\":\"%s\",\"bssid\":\"%s\",\"channel\":%d,\"signal\":%d,\"default_route\":%s,\"uptime\":%ld,\"error\":", ip, bssid, chan, sig, route ? "true" : "false", st == D_CONNECTED ? (long)(time(NULL) - since) : 0L);
	json_str(f, err);
	char rr[48]; mlme_rx_rate_str(rr, sizeof rr);
	fprintf(f, ",\"rx_rate\":\"%s\",\"width\":%u", rr, g_mlme.width == NL80211_CHAN_WIDTH_80 ? 80 : g_mlme.width == NL80211_CHAN_WIDTH_40 ? 40 : 20);
	fprintf(f, ",\"sys\":%d,\"dstart\":%ld", D.sys_assoc ? 1 : 0, (long)(time(NULL) - g_daemon_start));
	fprintf(f, ",\"rx_frames\":%lu,\"rx_data\":%lu,\"rx_bytes\":%lu,\"rx_undecrypted\":%lu,\"rx_undec_group\":%lu,\"rx_undec_uni\":%lu,\"utun_wr_err\":%lu", st_rx_frames, st_rx_data, st_rx_bytes, st_rx_undecrypted, st_rx_undec_group, st_rx_undec_uni, net_wr_err);
	fprintf(f, ",\"known\":[");
	for (int i = 0; i < nknown; i++) { if (i) fputc(',', f); json_str(f, known[i].ssid); }
	fprintf(f, "]}\n");
}

static void cmd_list(FILE *f)
{
	mlme_lock();
	fprintf(f, "[");
	bool first = true;
	for (int i = 0; i < bss_n; i++) {
		struct bss_entry *b = &bss_tab[i];
		if (!b->ssid_len) continue;
		/* strongest BSS per SSID only */
		bool dup = false;
		for (int j = 0; j < bss_n; j++)
			if (j != i && bss_tab[j].ssid_len == b->ssid_len && !memcmp(bss_tab[j].ssid, b->ssid, b->ssid_len) && (bss_tab[j].signal > b->signal || (bss_tab[j].signal == b->signal && j < i))) { dup = true; break; }
		if (dup) continue;
		bool kn = false;
		for (int k = 0; k < nknown; k++) if (!strcmp(known[k].ssid, b->ssid)) kn = true;
		if (!first) fputc(',', f);
		first = false;
		fprintf(f, "{\"ssid\":");
		json_str(f, b->ssid);
		fprintf(f, ",\"signal\":%d,\"channel\":%d,\"band\":\"%s\",\"secure\":%s,\"known\":%s}", b->signal, b->chan, b->freq >= 5000 ? "5GHz" : "2.4GHz", b->rsn ? "true" : "false", kn ? "true" : "false");
	}
	fprintf(f, "]\n");
	mlme_unlock();
}

int daemon_console_is_login(void)
{
	struct stat st;
	return stat("/dev/console", &st) == 0 && st.st_uid == 0;	/* nobody is logged in: the login window owns the console */
}
static volatile unsigned long g_scan_count;
unsigned long daemon_scan_count(void) { return g_scan_count; }
void daemon_request_scan(void)
{
	pthread_mutex_lock(&D.lock); D.scan_req = true; pthread_cond_broadcast(&D.cond); pthread_mutex_unlock(&D.lock);
}
extern void airport_bridge_start(void);
extern void airport_assoc_failed(void);
extern void airport_bounce_link(void);
static int g_from_fallback;
extern int airport_data_ready(void);
extern void airport_data_active(int on);
extern void airport_publish_start(const unsigned char mac[6]);

/* ---- entry points used by the kext bridge (System Settings Wi-Fi) ---- */
/* returns 0 on accepted, -1 when the passphrase is missing and the network is not known */
int daemon_connect_request(const char *ssid, const char *psk, bool open, const unsigned char *pmk)
{
	char use_psk[65] = "";
	bool use_pmk = false;
	pthread_mutex_lock(&D.lock);
	bool same = (D.state == D_CONNECTED || D.link_up) && !strcmp(D.cur_ssid, ssid);
	pthread_mutex_unlock(&D.lock);
	if (same) {
		/* already on this network. If the daemon joined by itself macOS does not know the link: bounce it so that macOS sees a fresh join */
		pthread_mutex_lock(&D.lock);
		bool bounce = !D.sys_assoc && !g_from_fallback;
		if (!g_from_fallback) D.sys_assoc = true;
		pthread_mutex_unlock(&D.lock);
		if (bounce) airport_bounce_link();
		return 0;
	}
	bool use_open = open;
	const unsigned char *pmk_to_use = pmk;
	if (!open && (!psk || !*psk)) {
		int k;
		for (k = 0; k < nknown; k++) if (!strcmp(known[k].ssid, ssid)) break;
		if (k < nknown && known[k].psk[0]) {
			snprintf(use_psk, sizeof use_psk, "%s", known[k].psk);
			use_open = known[k].open;
		} else if (k < nknown && known[k].has_pmk && !pmk) {
			pmk_to_use = known[k].pmk;		/* macOS gave this PMK earlier; reuse it when it joins without one */
			snprintf(use_psk, sizeof use_psk, "pmk-from-macos");
			use_pmk = true;
		} else if (pmk) {
			known_add_pmk(ssid, pmk);
			snprintf(use_psk, sizeof use_psk, "pmk-from-macos");
			use_pmk = true;
		} else return -1;
	} else if (!open) snprintf(use_psk, sizeof use_psk, "%s", psk);
	if (!use_pmk) known_add(ssid, use_psk, use_open);
	pthread_mutex_lock(&D.lock);
	if (use_pmk) { memcpy(D.want_pmk, pmk_to_use, 32); D.want_pmk_set = true; } else D.want_pmk_set = false;
	snprintf(D.want_ssid, sizeof D.want_ssid, "%s", ssid);
	snprintf(D.want_psk, sizeof D.want_psk, "%s", use_psk);
	D.want_open = use_open; D.want_active = true; D.autoconnect = !airport_data_ready(); D.link_lost = (D.state == D_CONNECTED);
	D.want_sys = !g_from_fallback;	/* with the en3 driver macOS decides what to join */
	pthread_cond_broadcast(&D.cond);
	pthread_mutex_unlock(&D.lock);
	return 0;
}

/* the system has not asked for anything: join the strongest known network that is in range (used before login) */
static const char *last_path(void) { return "/var/db/rtw89d/last.conf"; }
static void last_save(const char *ssid, int chan) { FILE *f = fopen(last_path(), "w"); if (f) { chmod(last_path(), 0600); fprintf(f, "%s\n%d\n", ssid, chan); fclose(f); } }
static bool last_load_ex(char *ssid, size_t n, int *chan)
{
	FILE *f = fopen(last_path(), "r");
	char line[64] = "";
	if (!f) return false;
	bool ok = fgets(ssid, (int)n, f) != NULL;
	ssid[strcspn(ssid, "\r\n")] = 0;
	if (chan) { *chan = 0; if (ok && fgets(line, sizeof line, f)) *chan = atoi(line); }
	fclose(f);
	return ok && ssid[0];
}
static bool last_load(char *ssid, size_t n) { return last_load_ex(ssid, n, NULL); }
int daemon_has_last(void) { char b[33]; return last_load(b, sizeof b); }

int daemon_fallback_connect(void)
{
	char best[33] = "";
	int best_sig = -1000;
	char last[33] = "";
	bool have_last = last_load(last, sizeof last);
	mlme_lock();
	for (int i = 0; i < bss_n; i++)
		for (int k = 0; k < nknown; k++)
			if (!strcmp(bss_tab[i].ssid, known[k].ssid) && (bss_tab[i].signal > best_sig || (have_last && !strcmp(known[k].ssid, last)))) {
				if (best[0] && have_last && !strcmp(best, last) && strcmp(known[k].ssid, last)) continue;	/* keep the network we were on */
				best_sig = bss_tab[i].signal; snprintf(best, sizeof best, "%s", known[k].ssid);
			}
	mlme_unlock();
	if (!best[0]) return -1;
	fprintf(stderr, "[daemon] nothing asked us to connect; joining the strongest known network \"%s\" (%d dBm)\n", best, best_sig);
	g_from_fallback = 1;
	int fr = daemon_connect_request(best, NULL, false, NULL);
	g_from_fallback = 0;
	return fr;
}

void daemon_set_autoconnect(int on)
{
	pthread_mutex_lock(&D.lock); D.autoconnect = on != 0; pthread_cond_broadcast(&D.cond); pthread_mutex_unlock(&D.lock);
}

void daemon_disconnect_request(void)
{
	pthread_mutex_lock(&D.lock); D.want_active = false; D.autoconnect = false; D.link_lost = (D.state == D_CONNECTED); if (D.state == D_FAILED) D.state = D_IDLE; pthread_cond_broadcast(&D.cond); pthread_mutex_unlock(&D.lock);
}

/* fills the current link; returns true when associated and the network is up */
bool daemon_link_info(char *ssid, size_t n, unsigned char bssid[6], int *chan, int *sig)
{
	char bs[32] = "";
	pthread_mutex_lock(&D.lock);
	bool up = D.state == D_CONNECTED || D.link_up;
	snprintf(ssid, n, "%s", D.cur_ssid);
	pthread_mutex_unlock(&D.lock);
	if (!up) return false;
	if (mlme_cur_info(bs, sizeof bs, chan, sig) != 0) return false;
	unsigned v[6];
	if (sscanf(bs, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
	for (int i = 0; i < 6; i++) bssid[i] = (unsigned char)v[i];
	return true;
}

static void handle_cmd(char *line, FILE *out)
{
	char *save, *cmd = strtok_r(line, " \t\r\n", &save);
	if (!cmd) { fprintf(out, "error\n"); return; }
	if (!strcmp(cmd, "status")) cmd_status(out);
	else if (!strcmp(cmd, "list")) cmd_list(out);
	else if (!strcmp(cmd, "scan")) {
		pthread_mutex_lock(&D.lock); D.scan_req = true; pthread_cond_broadcast(&D.cond); pthread_mutex_unlock(&D.lock);
		fprintf(out, "ok\n");
	} else if (!strcmp(cmd, "connect")) {
		char *a = strtok_r(NULL, " \t\r\n", &save), *b = strtok_r(NULL, " \t\r\n", &save);
		char ssid[33], psk[65];
		if (!a || hex_dec(a, ssid, sizeof ssid) <= 0) { fprintf(out, "error bad ssid\n"); return; }
		bool open = !b || !strcmp(b, "-");
		if (!open && (hex_dec(b, psk, sizeof psk) < 8)) { fprintf(out, "error bad passphrase (8-63 chars)\n"); return; }
		if (open) psk[0] = 0;
		known_add(ssid, psk, open);
		pthread_mutex_lock(&D.lock);
		snprintf(D.want_ssid, sizeof D.want_ssid, "%s", ssid);
		snprintf(D.want_psk, sizeof D.want_psk, "%s", psk);
		D.want_open = open; D.want_active = true; D.link_lost = (D.state == D_CONNECTED);
		pthread_cond_broadcast(&D.cond);
		pthread_mutex_unlock(&D.lock);
		fprintf(out, "ok\n");
	} else if (!strcmp(cmd, "use")) {
		char *a = strtok_r(NULL, " \t\r\n", &save);
		char ssid[33];
		if (!a || hex_dec(a, ssid, sizeof ssid) <= 0) { fprintf(out, "error bad ssid\n"); return; }
		int k;
		for (k = 0; k < nknown; k++) if (!strcmp(known[k].ssid, ssid)) break;
		if (k == nknown) { fprintf(out, "error unknown network\n"); return; }
		pthread_mutex_lock(&D.lock);
		snprintf(D.want_ssid, sizeof D.want_ssid, "%s", known[k].ssid);
		snprintf(D.want_psk, sizeof D.want_psk, "%s", known[k].psk);
		D.want_open = known[k].open; D.want_active = true; D.autoconnect = true; D.link_lost = (D.state == D_CONNECTED);
		pthread_cond_broadcast(&D.cond);
		pthread_mutex_unlock(&D.lock);
		fprintf(out, "ok\n");
	} else if (!strcmp(cmd, "disconnect")) {
		pthread_mutex_lock(&D.lock); D.want_active = false; D.autoconnect = false; D.link_lost = (D.state == D_CONNECTED); if (D.state == D_FAILED) D.state = D_IDLE; pthread_cond_broadcast(&D.cond); pthread_mutex_unlock(&D.lock);
		fprintf(out, "ok\n");
	} else if (!strcmp(cmd, "autoconnect")) {
		char *a = strtok_r(NULL, " \t\r\n", &save);
		pthread_mutex_lock(&D.lock); D.autoconnect = a && !strcmp(a, "on"); pthread_cond_broadcast(&D.cond); pthread_mutex_unlock(&D.lock);
		fprintf(out, "ok\n");
	} else if (!strcmp(cmd, "forget")) {
		char *a = strtok_r(NULL, " \t\r\n", &save);
		char ssid[33];
		if (!a || hex_dec(a, ssid, sizeof ssid) <= 0) { fprintf(out, "error bad ssid\n"); return; }
		known_forget(ssid);
		fprintf(out, "ok\n");
	} else if (!strcmp(cmd, "bench")) {
		char *a = strtok_r(NULL, " \t\r\n", &save), *b = strtok_r(NULL, " \t\r\n", &save);
		char res[256];
		net_bench(a ? atoi(a) : 5, b ? atoi(b) : 1400, res, sizeof res);
		fprintf(out, "%s\n", res);
	} else if (!strcmp(cmd, "rsn")) {
		char *a = strtok_r(NULL, "\r\n", &save);
		char res[1024];
		mlme_rsn_info(a ? a : "", res, sizeof res);
		fprintf(out, "%s", res);
	} else if (!strcmp(cmd, "rxdebug")) {
		char *a = strtok_r(NULL, " \t\r\n", &save);
		g_rx_debug = a && !strcmp(a, "on");
		fprintf(out, "ok\n");
	} else if (!strcmp(cmd, "wpa3")) {
		char *a = strtok_r(NULL, " \t\r\n", &save);
		g_prefer_sae = a && !strcmp(a, "on");
		fprintf(out, "ok (prefer SAE: %d)\n", g_prefer_sae);
	} else if (!strcmp(cmd, "usbstat")) {
		char res[300];
		extern void kc_usb_stats(char *, size_t);
		kc_usb_stats(res, sizeof res);
		fprintf(out, "%s\n", res);
	} else if (!strcmp(cmd, "quit")) {
		fprintf(out, "ok\n"); fflush(out);
		net_down();
		fprintf(stderr, "[daemon] quit requested\n");
		_exit(0);
	} else if (!strcmp(cmd, "route")) {
		char *a = strtok_r(NULL, " \t\r\n", &save);
		bool on = a && !strcmp(a, "on");
		pthread_mutex_lock(&D.lock); D.route_default = on; pthread_mutex_unlock(&D.lock);
		net_set_default(on);
		fprintf(out, "ok\n");
	} else fprintf(out, "error unknown command\n");
}

static void *ctl_thread(void *arg)
{
	const char *path = getenv("RTW89_SOCK") ? getenv("RTW89_SOCK") : "/var/run/rtw89d.sock";
	int srv = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
	unlink(path);
	if (bind(srv, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("control socket bind"); return NULL; }
	chmod(path, getuid() == 0 ? 0660 : 0600);
	if (getuid() == 0) { if (chown(path, 0, 80) < 0) {} }	/* group admin */
	listen(srv, 8);
	fprintf(stderr, "[daemon] control socket %s\n", path);
	for (;;) {
		int c = accept(srv, NULL, NULL);
		if (c < 0) continue;
		FILE *in = fdopen(c, "r+");
		char line[512];
		if (in && fgets(line, sizeof line, in)) { handle_cmd(line, in); fflush(in); }
		if (in) fclose(in); else close(c);
	}
	return NULL;
}

/* ---------------- adapter bring-up ---------------- */
static bool adapter_present(void)
{
	const struct usb_device_id *id;
	for (id = kc_usb_driver_ptr->id_table; id->idVendor; id++)
		if (kc_usb_present(id->idVendor, id->idProduct)) return true;
	return false;
}

static int bring_up_adapter(void)
{
	for (int attempt = 0; attempt < 30; attempt++) {
		if (kc_usb_present(0x0bda, 0x1a2b)) {			/* virtual-CD mode */
			kc_usb_modeswitch();
			for (int i = 0; i < 40 && kc_usb_present(0x0bda, 0x1a2b); i++) usleep(200000);
			usleep(500000);
		}
		if (!adapter_present()) { set_state(D_NO_ADAPTER, "adapter not found"); sleep(2); continue; }
		int r = kc_usb_find_and_probe(kc_usb_driver_ptr);
		if (!r && g_mm && g_mm->registered) return 0;
		fprintf(stderr, "[daemon] probe returned %d (registered=%d); retrying after USB re-enumeration\n", r, g_mm ? g_mm->registered : 0);
		kc_usb_close();
		sleep(3);
	}
	return -1;
}

struct rtw89_dev;
extern u32 rtw89_host_fw_halt_code(void *);
extern void rtw89_fw_st_dbg_dump(struct rtw89_dev *);
extern void rtw89_mac_dump_dmac_err_status(struct rtw89_dev *);
static void *stall_dump_thread(void *a)
{
	struct rtw89_dev *d = g_mm->hw.priv;
	rtw89_fw_st_dbg_dump(d);
	rtw89_mac_dump_dmac_err_status(d);
	*(volatile int *)a = 1;
	return NULL;
}
/* register reads use control transfers, which may still work when the bulk pipes are wedged; bounded by a timeout */
static void stall_dump(void)
{
	static volatile int done;
	pthread_t t;
	if (pthread_create(&t, NULL, stall_dump_thread, (void *)&done)) return;
	pthread_detach(t);
	for (int i = 0; i < 30 && !done; i++) usleep(100000);
	fprintf(stderr, "[daemon] stall dump %s\n", done ? "complete" : "timed out (control pipe also wedged)");
	fflush(stderr);
}

static void *watchdog_thread(void *arg)
{
	int misses = 0, stalled = 0;
	unsigned long last_rx = st_rx_frames;
	for (int tick = 0;; tick++) {
		usleep(500000);
		if (tick & 3) continue;	/* 0.5 s tick, checks run every 2 s */
		if (!adapter_present()) {
			if (++misses >= 2) { fprintf(stderr, "[daemon] adapter removed, exiting\n"); fflush(stderr); _exit(1); }
		} else misses = 0;
		/* a healthy link receives beacons constantly; no frames at all for 6 s means the RX path is wedged */
		/* only a connected link is guaranteed to hear beacons constantly; an idle adapter on a quiet channel hears nothing */
		if (D.state == D_CONNECTED || D.link_up) {
			unsigned long now = st_rx_frames;
			if (now == last_rx) {
				if (++stalled >= 3) {
					char st[300];
					extern void kc_usb_stats(char *, size_t);
					kc_usb_stats(st, sizeof st);
					fprintf(stderr, "[daemon] RX stalled for 6 s (%s); restarting to reset the adapter\n", st);
					fflush(stderr);
					stall_dump();
					net_down();
					_exit(2);
				}
			} else stalled = 0;
			last_rx = now;
		} else { stalled = 0; last_rx = st_rx_frames; }
	}
	return NULL;
}

/* ---------------- supervisor ---------------- */
static void tear_down_link(void)
{
	pthread_mutex_lock(&D.lock); D.link_up = false; D.sys_assoc = false; pthread_mutex_unlock(&D.lock);
	airport_data_active(0);
	net_down();
	if (g_mlme.state != MLME_IDLE) mlme_disconnect();
	pthread_mutex_lock(&D.lock);
	D.cur_ssid[0] = 0;
	D.link_lost = false;
	pthread_mutex_unlock(&D.lock);
}

static void pick_autoconnect(void)
{
	const char *names[MAX_KNOWN];
	int idx;
	for (int i = 0; i < nknown; i++) names[i] = known[i].ssid;
	mlme_best_known(names, nknown, &idx);
	if (idx < 0) return;
	pthread_mutex_lock(&D.lock);
	snprintf(D.want_ssid, sizeof D.want_ssid, "%s", known[idx].ssid);
	snprintf(D.want_psk, sizeof D.want_psk, "%s", known[idx].psk);
	D.want_open = known[idx].open;
	D.want_active = true;
	pthread_mutex_unlock(&D.lock);
}

/* every log line gets a time stamp: stderr is routed through a pipe and a thread prefixes each line */
static int g_log_fd = -1;
static void *log_thread(void *arg)
{
	int rfd = (int)(intptr_t)arg;
	char buf[4096];
	size_t n = 0;
	for (;;) {
		ssize_t r = read(rfd, buf + n, sizeof buf - n - 1);
		if (r <= 0) break;
		n += (size_t)r;
		char *nl;
		size_t off = 0;
		while ((nl = memchr(buf + off, '\n', n - off))) {
			time_t t = time(NULL);
			struct tm tm;
			localtime_r(&t, &tm);
			char ts[16];
			strftime(ts, sizeof ts, "%H:%M:%S ", &tm);
			dprintf(g_log_fd, "%s%.*s\n", ts, (int)(nl - (buf + off)), buf + off);
			off = (size_t)(nl + 1 - buf);
		}
		memmove(buf, buf + off, n - off);
		n -= off;
		if (n >= sizeof buf - 1) { dprintf(g_log_fd, "%.*s\n", (int)n, buf); n = 0; }
	}
	return NULL;
}

static void install_log_timestamps(void)
{
	int p[2];
	if (pipe(p)) return;
	g_log_fd = dup(2);
	dup2(p[1], 2);
	close(p[1]);
	pthread_t t;
	pthread_create(&t, NULL, log_thread, (void *)(intptr_t)p[0]);
	pthread_detach(t);
}

int daemon_main(void)
{
	install_log_timestamps();
	g_daemon_start = time(NULL);
	kc_runtime_init();
	known_load();
	set_state(D_STARTING, "");
	pthread_t t;
	pthread_create(&t, NULL, ctl_thread, NULL);
	if (bring_up_adapter()) { set_state(D_FAILED, "adapter bring-up failed"); sleep(5); return 1; }
	if (mm_up()) { set_state(D_FAILED, "driver start failed"); return 2; }
	pthread_create(&t, NULL, watchdog_thread, NULL);
	airport_bridge_start();
	net_init(tx_cb, g_mm->vif->addr);
	airport_publish_start(g_mm->vif->addr);
	net_on_lease_lost = on_lease_lost;
	g_mlme.on_disconnect = on_disc;
	set_state(D_IDLE, "");
	sleep(1);
	bool first = true;
	int backoff = 2;
	for (;;) {
		pthread_mutex_lock(&D.lock);
		while (!D.link_lost && !D.scan_req && !(D.want_active && D.state != D_CONNECTED) && !first && !(D.autoconnect && D.state == D_IDLE && nknown))
			pthread_cond_wait(&D.cond, &D.lock);
		bool lost = D.link_lost, scan = D.scan_req;
		D.scan_req = false;
		pthread_mutex_unlock(&D.lock);

		if (lost) {
			fprintf(stderr, "[daemon] link down\n");
			tear_down_link();
			set_state(D_IDLE, "");
			pthread_mutex_lock(&D.lock); if (D.want_active) { /* reconnect */ } pthread_mutex_unlock(&D.lock);
			sleep(1);
		}
		if (first || scan) {
			bool was_conn = D.state == D_CONNECTED;
			if (!was_conn) set_state(D_SCANNING, "");
			{
				char lssid[33];
				int lchan = 0;
				if (first && last_load_ex(lssid, sizeof lssid, &lchan) && lchan > 0) mlme_scan_quick(lssid, lchan);	/* rejoin quickly: one channel, one name */
				else mlme_scan(NULL);
			}
			g_scan_count++;
			if (!was_conn) set_state(D_IDLE, "");
			if (first && D.autoconnect) pick_autoconnect();
			first = false;
		}
		pthread_mutex_lock(&D.lock);
		bool do_connect = D.want_active && D.state != D_CONNECTED;
		char ssid[33], psk[65];
		bool open = D.want_open;
		snprintf(ssid, sizeof ssid, "%s", D.want_ssid);
		snprintf(psk, sizeof psk, "%s", D.want_psk);
		bool route = D.route_default;
		pthread_mutex_unlock(&D.lock);
		if (!do_connect && D.state == D_IDLE && D.autoconnect && nknown && !D.want_active) { mlme_scan(NULL); g_scan_count++; pick_autoconnect(); sleep(5); continue; }
		if (!do_connect) { if (D.state == D_IDLE) sleep(1); continue; }

		set_state(D_CONNECTING, "");
		pthread_mutex_lock(&D.lock); snprintf(D.cur_ssid, sizeof D.cur_ssid, "%s", ssid); pthread_mutex_unlock(&D.lock);
		fprintf(stderr, "[daemon] connecting to \"%s\"\n", ssid);
		pthread_mutex_lock(&D.lock);
		D.link_up = false;
		if (D.want_pmk_set) { extern unsigned char g_pmk_override[32]; extern int g_pmk_override_set; memcpy(g_pmk_override, D.want_pmk, 32); g_pmk_override_set = 1; D.want_pmk_set = false; }
		pthread_mutex_unlock(&D.lock);
		int r = mlme_connect(ssid, open ? NULL : psk);
		for (int tries = 0; r == -EACCES && airport_data_ready() && tries < 2; tries++) {	/* handshakes right after an adapter reset can time out once; retry before reporting a wrong password */
			fprintf(stderr, "[daemon] handshake failed, retrying (%d)\n", tries + 1);
			mlme_disconnect();
			sleep(1);
			r = mlme_connect(ssid, open ? NULL : psk);
		}
		if (r == 0) { pthread_mutex_lock(&D.lock); D.link_up = true; D.sys_assoc = D.want_sys; pthread_mutex_unlock(&D.lock); }
		if (r == 0 && !getenv("RTW89_UTUN")) {
			/* macOS drives en3 (DHCP, ARP, IPv6 included); frames are exchanged with RTW89Airport.kext */
			for (int w = 0; w < 40 && !airport_data_ready(); w++) usleep(500000);
			if (airport_data_ready()) {
				airport_data_active(1);
				fprintf(stderr, "[daemon] data path: en3 (RTW89Airport.kext)\n");
			} else {
				r = -ENODEV;
			}
		} else if (r == 0) { int nr = net_up(20, route); r = nr == 0 ? 0 : (nr == -2 ? -EPERM : -EIO); fprintf(stderr, "[daemon] data path: utun (RTW89_UTUN)\n"); }
		if (r) {
			char msg[96];
			snprintf(msg, sizeof msg, r == -EACCES ? "wrong password?" : r == -ENOENT ? "network not found" : r == -EIO ? "no IP address (DHCP failed)" : r == -EPERM ? "cannot create network interface (daemon needs root)" : r == -ENODEV ? "en3 driver (RTW89Airport.kext) is not loaded" : "connection failed (%d)", r);
			fprintf(stderr, "[daemon] connect failed: %s\n", msg);
			if (airport_data_ready()) { airport_assoc_failed(); pthread_mutex_lock(&D.lock); D.want_active = false; pthread_mutex_unlock(&D.lock); }	/* macOS decides whether to retry */
			tear_down_link();
			set_state(D_FAILED, msg);
			pthread_mutex_lock(&D.lock);
			if (r == -EACCES) D.want_active = false;		/* do not hammer the AP with a wrong password */
			pthread_mutex_unlock(&D.lock);
			sleep(backoff); if (backoff < 30) backoff *= 2;
			continue;
		}
		backoff = 2;
		pthread_mutex_lock(&D.lock);
		D.state = D_CONNECTED; D.err[0] = 0; D.connected_at = time(NULL);
		pthread_mutex_unlock(&D.lock);
		fprintf(stderr, "[daemon] connected to \"%s\"\n", ssid);
		{ char b[32]; int c = 0, sg = 0; if (mlme_cur_info(b, sizeof b, &c, &sg) == 0) last_save(ssid, c); else last_save(ssid, 0); }
	}
	return 0;
}
