// SPDX-License-Identifier: BSD-3-Clause
// WPA2-PSK / WPA3-SAE (CCMP) supplicant: EAPOL 4-way handshake and group key handshake.
//   WPA2-PSK : PMK = PBKDF2, PTK = PRF-SHA1, MIC = HMAC-SHA1-128, key descriptor version 2
//   WPA3-SAE : PMK from SAE, PTK = KDF-SHA256, MIC = AES-128-CMAC, key descriptor version 0, IGTK (BIP) installed
#include "mm.h"
#include "mlme.h"
#include "datapath.h"
#include "crypto.h"
#include "supplicant.h"

static struct {
	bool active, sae;
	u8 pmk[32];
	u8 anonce[32], snonce[32];
	u8 ptk[48];
	bool have_ptk;
	u64 replay;
	bool have_replay;
	u8 gtk[32]; int gtk_len, gtk_idx; bool have_gtk;
	u8 igtk[16]; int igtk_idx; bool have_igtk;
	bool keys_done;
} S;

#define KI_PAIRWISE (1 << 3)
#define KI_INSTALL (1 << 6)
#define KI_ACK (1 << 7)
#define KI_MIC (1 << 8)
#define KI_SECURE (1 << 9)
#define KI_ENCRYPTED (1 << 12)

static u16 ki_version(void) { return S.sae ? 0 : 2; }

u8 g_pmk_override[32];
int g_pmk_override_set;

void supplicant_start(const char *ssid, const char *psk)
{
	memset(&S, 0, sizeof S);
	if (g_pmk_override_set) { memcpy(S.pmk, g_pmk_override, 32); g_pmk_override_set = 0; }	/* PMK handed over by macOS */
	else wpa_pbkdf2(psk, (const u8 *)ssid, strlen(ssid), S.pmk);
	S.active = true;
}

void supplicant_start_sae(const u8 pmk[32])
{
	memset(&S, 0, sizeof S);
	memcpy(S.pmk, pmk, 32);
	S.sae = true;
	S.active = true;
}

void supplicant_stop(void)
{
	memset(&S, 0, sizeof S);
}

static void be16(u8 *p, u16 v) { p[0] = v >> 8; p[1] = v & 0xff; }

/* MIC over the whole EAPOL frame with the MIC field zeroed */
static void compute_mic(const u8 *frame, size_t len, u8 mic[16])
{
	if (S.sae) {
		wpa_aes_cmac(S.ptk, frame, len, mic);
	} else {
		u8 d[20];
		wpa_hmac_sha1(S.ptk, 16, frame, len, d);
		memcpy(mic, d, 16);
	}
}

static size_t build_key_frame(u8 *buf, u16 key_info, u16 key_len, const u8 replay[8], const u8 nonce[32], const u8 *data, size_t dlen, bool with_mic)
{
	size_t body = 95 + dlen;
	buf[0] = 2; buf[1] = 3; be16(buf + 2, body);
	u8 *k = buf + 4;
	k[0] = 2;
	be16(k + 1, key_info);
	be16(k + 3, key_len);
	memcpy(k + 5, replay, 8);
	memcpy(k + 13, nonce, 32);
	memset(k + 45, 0, 16 + 8 + 8 + 16);
	be16(k + 93, dlen);
	if (dlen) memcpy(k + 95, data, dlen);
	if (with_mic) compute_mic(buf, 4 + body, k + 77);
	return 4 + body;
}

static void derive_ptk(const u8 *aa, const u8 *spa)
{
	u8 data[76];
	memcpy(data, memcmp(aa, spa, 6) < 0 ? aa : spa, 6);
	memcpy(data + 6, memcmp(aa, spa, 6) < 0 ? spa : aa, 6);
	memcpy(data + 12, memcmp(S.anonce, S.snonce, 32) < 0 ? S.anonce : S.snonce, 32);
	memcpy(data + 44, memcmp(S.anonce, S.snonce, 32) < 0 ? S.snonce : S.anonce, 32);
	if (S.sae) wpa_kdf_sha256(S.pmk, 32, "Pairwise key expansion", data, 76, S.ptk, 384);
	else wpa_prf_sha1(S.pmk, 32, "Pairwise key expansion", data, 76, S.ptk, 48);
}

static bool verify_mic(const u8 *frame, size_t flen, const u8 *mic_in_frame)
{
	u8 copy[512], mic[16];
	if (flen > sizeof copy) return false;
	memcpy(copy, frame, flen);
	memset(copy + 4 + 77, 0, 16);
	compute_mic(copy, flen, mic);
	return memcmp(mic, mic_in_frame, 16) == 0;
}

static void parse_key_data(const u8 *kd, size_t plen)
{
	for (size_t i = 0; i + 2 <= plen;) {
		u8 id = kd[i], l = kd[i + 1];
		if (id == 0 && l == 0) break;
		if (i + 2 + l > plen) break;
		if (id == 0xdd && l >= 4 && kd[i + 2] == 0x00 && kd[i + 3] == 0x0f && kd[i + 4] == 0xac) {
			u8 type = kd[i + 5];
			if (type == 1 && l >= 6 + 16) {					/* GTK KDE: keyid/tx(1) reserved(1) GTK */
				S.gtk_idx = kd[i + 6] & 3;
				S.gtk_len = l - 4 - 2;
				if (S.gtk_len > 32) S.gtk_len = 32;
				memcpy(S.gtk, kd + i + 8, S.gtk_len);
				S.have_gtk = true;
				fprintf(stderr, "[wpa] GTK idx %d len %d\n", S.gtk_idx, S.gtk_len);
			} else if (type == 9 && l >= 4 + 2 + 6 + 16) {			/* IGTK KDE: keyid(2) IPN(6) IGTK(16) */
				S.igtk_idx = kd[i + 6] | (kd[i + 7] << 8);
				memcpy(S.igtk, kd + i + 6 + 2 + 6, 16);
				S.have_igtk = true;
				fprintf(stderr, "[wpa] IGTK idx %d\n", S.igtk_idx);
			}
		}
		i += 2 + l;
	}
}

static void install_group_keys(void)
{
	if (S.have_gtk) dp_install_key(S.gtk, S.gtk_len, S.gtk_idx, false, g_mlme.sta);
	if (S.have_igtk) dp_install_igtk(S.igtk, S.igtk_idx);
}

void supplicant_rx_eapol(const u8 *sa, const u8 *frame, size_t len)
{
	if (!S.active || len < 4 + 95 || frame[1] != 3) return;
	const u8 *k = frame + 4;
	if (k[0] != 2 && k[0] != 254) return;
	u16 key_info = (k[1] << 8) | k[2];
	u64 replay = 0;
	for (int i = 0; i < 8; i++) replay = (replay << 8) | k[5 + i];
	u16 dlen = (k[93] << 8) | k[94];
	if (4 + 95 + (size_t)dlen > len) return;
	u8 own_replay[8];
	memcpy(own_replay, k + 5, 8);
	const u8 *bssid = g_mlme.sta->addr;
	const u8 *spa = g_mm->vif->addr;

	if ((key_info & KI_ACK) && (key_info & KI_PAIRWISE) && !(key_info & KI_MIC)) {
		/* message 1 */
		fprintf(stderr, "[wpa] 4-way: message 1\n");
		memcpy(S.anonce, k + 13, 32);
		wpa_random(S.snonce, 32);
		derive_ptk(bssid, spa);
		S.have_ptk = true;
		S.replay = replay; S.have_replay = true;
		u8 out[256];
		u16 ki = ki_version() | KI_PAIRWISE | KI_MIC;
		size_t n = build_key_frame(out, ki, 0, own_replay, S.snonce, g_mlme.assoc_req_rsn, g_mlme.assoc_req_rsn_len, true);
		dp_tx_frame(bssid, ETH_P_PAE, out, n, true);
		return;
	}
	if ((key_info & KI_ACK) && (key_info & KI_PAIRWISE) && (key_info & KI_MIC) && (key_info & KI_INSTALL) && S.have_ptk) {
		/* message 3 */
		fprintf(stderr, "[wpa] 4-way: message 3\n");
		if (S.have_replay && replay <= S.replay) { fprintf(stderr, "[wpa] replayed message 3, ignoring\n"); return; }
		if (!verify_mic(frame, 4 + 95 + dlen, k + 77)) { fprintf(stderr, "[wpa] message 3 MIC mismatch (wrong passphrase?)\n"); return; }
		S.replay = replay;
		u8 plain[512];
		size_t plen = dlen;
		const u8 *kd = k + 95;
		if (key_info & KI_ENCRYPTED) {
			if (wpa_aes_unwrap(S.ptk + 16, kd, dlen, plain, &plen)) { fprintf(stderr, "[wpa] key data unwrap failed\n"); return; }
			kd = plain;
		}
		parse_key_data(kd, plen);
		/* message 4 */
		u8 zero[32] = {0}, out[256];
		u16 ki = ki_version() | KI_PAIRWISE | KI_MIC | KI_SECURE;
		size_t n = build_key_frame(out, ki, 0, own_replay, zero, NULL, 0, true);
		g_mlme.eapol_tx_wait = true;
		g_mlme.eapol_tx_acked = false;
		reinit_completion(&g_mlme.eapol_tx_done);
		dp_tx_frame(bssid, ETH_P_PAE, out, n, true);
		wait_for_completion_timeout(&g_mlme.eapol_tx_done, 300);
		g_mlme.eapol_tx_wait = false;
		fprintf(stderr, "[wpa] message 4 sent (acked=%d)\n", g_mlme.eapol_tx_acked);
		/* install keys */
		if (dp_install_key(S.ptk + 32, 16, 0, true, g_mlme.sta)) return;
		install_group_keys();
		mlme_set_authorized();
		S.keys_done = true;
		complete_all(&g_mlme.eapol_done);
		return;
	}
	if (!(key_info & KI_PAIRWISE) && (key_info & KI_ACK) && (key_info & KI_MIC) && S.keys_done) {
		/* group key handshake: message 1 */
		if (!verify_mic(frame, 4 + 95 + dlen, k + 77)) return;
		u8 plain[512]; size_t plen = dlen;
		const u8 *kd = k + 95;
		if (key_info & KI_ENCRYPTED) { if (wpa_aes_unwrap(S.ptk + 16, kd, dlen, plain, &plen)) return; kd = plain; }
		parse_key_data(kd, plen);
		u8 zero[32] = {0}, out[256];
		size_t n = build_key_frame(out, ki_version() | KI_MIC | KI_SECURE, 0, own_replay, zero, NULL, 0, true);
		dp_tx_frame(bssid, ETH_P_PAE, out, n, true);
		install_group_keys();
		fprintf(stderr, "[wpa] group rekey handled\n");
	}
}
