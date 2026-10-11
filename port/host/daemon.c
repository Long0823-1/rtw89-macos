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

extern void kc_runtime_init(void);
extern int kc_usb_find_and_probe(const struct usb_driver *drv);
extern void kc_usb_close(void);
extern int kc_usb_present(u16 vid, u16 pid);
extern int kc_usb_modeswitch(void);

extern unsigned long net_wr_err;
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
} D = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .route_default = false, .autoconnect = true };

/* ---------------- known networks (root-only file) ---------------- */
#define MAX_KNOWN 32
static struct { char ssid[33]; char psk[65]; bool open; } known[MAX_KNOWN];
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
		if (!known[nknown].open && hex_dec(b, known[nknown].psk, sizeof known[0].psk) < 0) continue;
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
		if (known[i].open) strcpy(b, "-"); else hex_enc(known[i].psk, strlen(known[i].psk), b);
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
	fprintf(f, ",\"rx_frames\":%lu,\"rx_data\":%lu,\"rx_bytes\":%lu,\"rx_undecrypted\":%lu,\"utun_wr_err\":%lu", st_rx_frames, st_rx_data, st_rx_bytes, st_rx_undecrypted, net_wr_err);
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

static volatile unsigned long g_scan_count;
unsigned long daemon_scan_count(void) { return g_scan_count; }
void daemon_request_scan(void)
{
	pthread_mutex_lock(&D.lock); D.scan_req = true; pthread_cond_broadcast(&D.cond); pthread_mutex_unlock(&D.lock);
}
extern void airport_bridge_start(void);

/* ---- entry points used by the kext bridge (System Settings Wi-Fi) ---- */
/* returns 0 on accepted, -1 when the passphrase is missing and the network is not known */
int daemon_connect_request(const char *ssid, const char *psk, bool open)
{
	char use_psk[65] = "";
	bool use_open = open;
	if (!open && (!psk || !*psk)) {
		int k;
		for (k = 0; k < nknown; k++) if (!strcmp(known[k].ssid, ssid)) break;
		if (k == nknown) return -1;
		snprintf(use_psk, sizeof use_psk, "%s", known[k].psk);
		use_open = known[k].open;
	} else if (!open) snprintf(use_psk, sizeof use_psk, "%s", psk);
	known_add(ssid, use_psk, use_open);
	pthread_mutex_lock(&D.lock);
	snprintf(D.want_ssid, sizeof D.want_ssid, "%s", ssid);
	snprintf(D.want_psk, sizeof D.want_psk, "%s", use_psk);
	D.want_open = use_open; D.want_active = true; D.autoconnect = true; D.link_lost = (D.state == D_CONNECTED);
	pthread_cond_broadcast(&D.cond);
	pthread_mutex_unlock(&D.lock);
	return 0;
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
	bool up = D.state == D_CONNECTED;
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
		if (D.state == D_CONNECTED || D.state == D_IDLE || D.state == D_SCANNING) {
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

int daemon_main(void)
{
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
			mlme_scan(NULL);
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
		int r = mlme_connect(ssid, open ? NULL : psk);
		if (r == 0) { int nr = net_up(20, route); r = nr == 0 ? 0 : (nr == -2 ? -EPERM : -EIO); }
		if (r) {
			char msg[96];
			snprintf(msg, sizeof msg, r == -EACCES ? "wrong password?" : r == -ENOENT ? "network not found" : r == -EIO ? "no IP address (DHCP failed)" : r == -EPERM ? "cannot create network interface (daemon needs root)" : "connection failed (%d)", r);
			fprintf(stderr, "[daemon] connect failed: %s\n", msg);
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
	}
	return 0;
}
