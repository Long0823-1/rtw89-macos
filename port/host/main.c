// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// rtw89 userspace host for macOS.  usage: rtw89d scan | rtw89d connect <ssid> [--open]   (PSK from $RTW89_PSK or $RTW89_PSK_FILE)
#include "mm.h"
#include "mlme.h"
#include "datapath.h"
#include "net.h"
#include "daemon.h"
#include <linux/usb.h>
#include <signal.h>

extern void kc_runtime_init(void);
extern int kc_usb_find_and_probe(const struct usb_driver *drv);

static int tx_cb(const unsigned char *da, unsigned short ethertype, const unsigned char *payload, size_t len)
{
	if (!g_mlme.sta || g_mlme.state < MLME_ASSOCIATED) return -1;
	return dp_tx_frame(da, ethertype, payload, len, false);
}

static volatile int stop_flag;
static void on_sig(int s) { stop_flag = 1; }
static void on_disc(void) { fprintf(stderr, "[main] disconnected by AP\n"); stop_flag = 1; }

static char *read_psk(void)
{
	const char *e = getenv("RTW89_PSK");
	if (e) return strdup(e);
	const char *f = getenv("RTW89_PSK_FILE");
	if (f) {
		FILE *fp = fopen(f, "r");
		if (!fp) { perror(f); return NULL; }
		char buf[128] = {0};
		if (!fgets(buf, sizeof buf, fp)) { fclose(fp); return NULL; }
		fclose(fp);
		buf[strcspn(buf, "\r\n")] = 0;
		return strdup(buf);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 2) { fprintf(stderr, "usage: %s daemon | scan | connect <ssid> [--open]\n", argv[0]); return 1; }
	if (!strcmp(argv[1], "daemon")) return daemon_main();
	kc_runtime_init();
	int ret = kc_usb_find_and_probe(kc_usb_driver_ptr);
	fprintf(stderr, "probe: %d\n", ret);
	if (ret) return 1;
	if (!g_mm || !g_mm->registered) { fprintf(stderr, "driver did not register (USB mode switch pending? run again)\n"); return 2; }
	ret = mm_up();
	if (ret) { fprintf(stderr, "mm_up failed: %d\n", ret); return 3; }
	sleep(1);
	signal(SIGINT, on_sig); signal(SIGTERM, on_sig);

	if (!strcmp(argv[1], "scan")) {
		mlme_scan(NULL);
		mlme_dump_bss();
	} else if (!strcmp(argv[1], "connect") && argc >= 3) {
		bool open = argc >= 4 && !strcmp(argv[3], "--open");
		char *psk = open ? NULL : read_psk();
		if (!open && !psk) { fprintf(stderr, "set RTW89_PSK or RTW89_PSK_FILE (or pass --open)\n"); mm_down(); return 4; }
		net_init(tx_cb, g_mm->vif->addr);
		g_mlme.on_disconnect = on_disc;
		mlme_scan(argv[2]);
		ret = mlme_connect(argv[2], psk);
		fprintf(stderr, "connect: %d\n", ret);
		if (!ret) {
			if (net_up(20, getenv("RTW89_SET_DEFAULT") != NULL) == 0) {
				fprintf(stderr, "[main] online. Ctrl-C to disconnect.\n");
				while (!stop_flag) sleep(1);
			}
			net_down();
			mlme_disconnect();
		}
	}
	mm_down();
	return 0;
}
