// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Station-mode MLME for the macOS rtw89 host: scan table, authentication, association, capability negotiation,
// station/channel-context bookkeeping towards the driver (the part Linux's mac80211 mlme.c normally does).
#include "mm.h"
#include "mlme.h"
extern unsigned long st_rx_frames, st_rx_data, st_rx_bytes, st_rx_undecrypted, st_rx_undec_group, st_rx_undec_uni;
static void handle_action(struct sk_buff *skb);
void mlme_ba_start_tx(int tid);
void mlme_ba_reset(void);
bool mlme_tx_ba_ready(int tid);
#include "datapath.h"
static void dp_remove_keys_if_any(void);

struct bss_entry bss_tab[MAX_BSS];
int g_prefer_sae;
int g_rx_debug;
int bss_n;
static pthread_mutex_t bss_lock = PTHREAD_MUTEX_INITIALIZER;

struct mlme g_mlme;
static u8 g_bssid[6];
static u8 g_bssid_last[6];

/* ---------------- IE helpers ---------------- */
const u8 *ie_find(const u8 *ies, size_t len, u8 id)
{
	const u8 *p = ies, *end = ies + len;
	while (p + 2 <= end && p + 2 + p[1] <= end) {
		if (p[0] == id) return p;
		p += 2 + p[1];
	}
	return NULL;
}

const u8 *ie_find_ext(const u8 *ies, size_t len, u8 ext_id)
{
	const u8 *p = ies, *end = ies + len;
	while (p + 3 <= end && p + 2 + p[1] <= end) {
		if (p[0] == WLAN_EID_EXTENSION && p[1] >= 1 && p[2] == ext_id) return p;
		p += 2 + p[1];
	}
	return NULL;
}

static const u8 *ie_find_vendor(const u8 *ies, size_t len, const u8 *oui_type, int n)
{
	const u8 *p = ies, *end = ies + len;
	while (p + 2 <= end && p + 2 + p[1] <= end) {
		if (p[0] == WLAN_EID_VENDOR_SPECIFIC && p[1] >= n && !memcmp(p + 2, oui_type, n)) return p;
		p += 2 + p[1];
	}
	return NULL;
}

/* ---------------- scan table ---------------- */
static void handle_beacon(struct sk_buff *skb)
{
	struct ieee80211_rx_status *st = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	size_t hdr = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	if (skb->len < hdr) return;
	const u8 *ies = mgmt->u.beacon.variable;
	size_t ielen = skb->len - hdr;
	const u8 *ssid = ie_find(ies, ielen, WLAN_EID_SSID);
	pthread_mutex_lock(&bss_lock);
	struct bss_entry *b = NULL;
	for (int i = 0; i < bss_n; i++)
		if (!memcmp(bss_tab[i].bssid, mgmt->bssid, 6)) { b = &bss_tab[i]; break; }
	if (!b) {
		if (bss_n >= MAX_BSS) { pthread_mutex_unlock(&bss_lock); return; }
		b = &bss_tab[bss_n++];
		memset(b, 0, sizeof(*b));
		memcpy(b->bssid, mgmt->bssid, 6);
	}
	b->freq = st->freq;
	b->signal = st->signal;
	b->capab = le16_to_cpu(mgmt->u.beacon.capab_info);
	b->beacon_int = le16_to_cpu(mgmt->u.beacon.beacon_int);
	b->tsf = le64_to_cpu(mgmt->u.beacon.timestamp);
	if (ssid && ssid[1] <= 32) {
		if (ssid[1] || !b->ssid_len) { memcpy(b->ssid, ssid + 2, ssid[1]); b->ssid[ssid[1]] = 0; b->ssid_len = ssid[1]; }
	}
	if (ielen <= sizeof(b->ies) && (ieee80211_is_probe_resp(mgmt->frame_control) || !b->ie_len)) {
		memcpy(b->ies, ies, ielen);
		b->ie_len = ielen;
	}
	const u8 *rsn_ie = ie_find(b->ies, b->ie_len, WLAN_EID_RSN);
	b->rsn = rsn_ie != NULL;
	b->akm_psk = b->akm_sae = b->mfpc = b->mfpr = b->h2e = false;
	if (rsn_ie && rsn_ie[1] >= 12) {
		const u8 *p = rsn_ie + 2 + 2 + 4, *end = rsn_ie + 2 + rsn_ie[1];
		if (p + 2 <= end) {
			int npair = p[0] | (p[1] << 8); p += 2 + 4 * npair;
			if (p + 2 <= end) {
				int nakm = p[0] | (p[1] << 8); p += 2;
				for (int k = 0; k < nakm && p + 4 * (k + 1) <= end; k++) {
					if (p[4 * k] == 0x00 && p[4 * k + 1] == 0x0f && p[4 * k + 2] == 0xac) {
						if (p[4 * k + 3] == 2) b->akm_psk = true;
						if (p[4 * k + 3] == 8) b->akm_sae = true;
					}
				}
				p += 4 * nakm;
				if (p + 2 <= end) { unsigned caps = p[0] | (p[1] << 8); b->mfpr = caps & 0x40; b->mfpc = caps & 0x80; }
			}
		}
	}
	const u8 *rsnx = ie_find(b->ies, b->ie_len, 244);
	if (rsnx && rsnx[1] >= 1) b->h2e = rsnx[2] & 0x20;
	const u8 *ds = ie_find(b->ies, b->ie_len, WLAN_EID_DS_PARAMS);
	b->chan = ds ? ds[2] : ieee80211_freq_khz_to_channel(st->freq * 1000);
	b->seen = jiffies;
	pthread_mutex_unlock(&bss_lock);
}

void mlme_dump_bss(void)
{
	pthread_mutex_lock(&bss_lock);
	for (int i = 0; i < bss_n; i++) {
		struct bss_entry *b = &bss_tab[i];
		printf("%02x:%02x:%02x:%02x:%02x:%02x  ch%-3d %4d MHz  %4d dBm  %s  \"%.*s\"\n",
		       b->bssid[0], b->bssid[1], b->bssid[2], b->bssid[3], b->bssid[4], b->bssid[5],
		       b->chan, b->freq, b->signal, b->rsn ? "WPA2" : "open", b->ssid_len, b->ssid);
	}
	pthread_mutex_unlock(&bss_lock);
}

/* ---------------- frame construction ---------------- */
static u16 tx_seq;

static struct sk_buff *mgmt_alloc(size_t len)
{
	struct ieee80211_hw *hw = &g_mm->hw;
	struct sk_buff *skb = alloc_skb(hw->extra_tx_headroom + len + 16, GFP_KERNEL);
	skb_reserve(skb, hw->extra_tx_headroom);
	return skb;
}

static void mgmt_hdr(struct ieee80211_mgmt *m, u16 stype)
{
	m->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | stype);
	m->duration = 0;
	memcpy(m->da, g_bssid, 6);
	memcpy(m->sa, g_mm->vif->addr, 6);
	memcpy(m->bssid, g_bssid, 6);
	m->seq_ctrl = cpu_to_le16((tx_seq++ & 0xfff) << 4);
}

static void tx_prepare(struct sk_buff *skb, u8 band, int queue)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	memset(info, 0, sizeof(*info));
	info->band = band;
	info->control.vif = g_mm->vif;
	skb_set_queue_mapping(skb, queue);
	info->hw_queue = queue;
	skb->priority = queue == IEEE80211_AC_VO ? 7 : 0;
}

int mlme_tx_mgmt(struct sk_buff *skb)
{
	tx_prepare(skb, g_mlme.chan->band, IEEE80211_AC_VO);
	return mm_tx(skb, g_mlme.sta);
}

static void send_auth(void)
{
	struct sk_buff *skb = mgmt_alloc(64);
	struct ieee80211_mgmt *m = skb_put_zero(skb, offsetof(struct ieee80211_mgmt, u.auth.variable));
	mgmt_hdr(m, IEEE80211_STYPE_AUTH);
	m->u.auth.auth_alg = cpu_to_le16(WLAN_AUTH_OPEN);
	m->u.auth.auth_transaction = cpu_to_le16(1);
	m->u.auth.status_code = 0;
	mlme_tx_mgmt(skb);
}

/* ---- WPA3-Personal (SAE) authentication exchange ---- */
#include "sae.h"

static void send_sae(u16 seq, u16 status, const u8 *body, size_t blen)
{
	struct sk_buff *skb = mgmt_alloc(64 + blen);
	struct ieee80211_mgmt *m = skb_put_zero(skb, offsetof(struct ieee80211_mgmt, u.auth.variable));
	mgmt_hdr(m, IEEE80211_STYPE_AUTH);
	m->u.auth.auth_alg = cpu_to_le16(WLAN_AUTH_SAE);
	m->u.auth.auth_transaction = cpu_to_le16(seq);
	m->u.auth.status_code = cpu_to_le16(status);
	skb_put_data(skb, body, blen);
	mlme_tx_mgmt(skb);
}

static pthread_mutex_t sae_lock = PTHREAD_MUTEX_INITIALIZER;

static void sae_push(u16 seq, u16 status, const u8 *data, size_t len)
{
	pthread_mutex_lock(&sae_lock);
	if (g_mlme.sae_qn < 4) {
		struct sae_frame *f = &g_mlme.sae_q[g_mlme.sae_qn++];
		f->seq = seq; f->status = status;
		f->len = len > sizeof f->data ? sizeof f->data : len;
		memcpy(f->data, data, f->len);
	}
	pthread_mutex_unlock(&sae_lock);
}

static struct sae_frame *sae_wait(u16 seq, int ms)
{
	static struct sae_frame f;
	u64 end = kc_now_ms() + ms;
	while (kc_now_ms() < end) {
		pthread_mutex_lock(&sae_lock);
		for (int i = 0; i < g_mlme.sae_qn; i++)
			if (g_mlme.sae_q[i].seq == seq) {
				f = g_mlme.sae_q[i];
				g_mlme.sae_q[i] = g_mlme.sae_q[--g_mlme.sae_qn];
				pthread_mutex_unlock(&sae_lock);
				return &f;
			}
		pthread_mutex_unlock(&sae_lock);
		if (g_mlme.deauthed) return NULL;
		usleep(10000);
	}
	return NULL;
}

/* returns 0 on success; fills g_mlme.sae_pmk / sae_pmkid */
static int do_sae(const char *password, const u8 *bssid)
{
	static struct sae s;
	u8 commit[96], body[2 + 128 + 96];
	if (sae_init(&s, g_mm->vif->addr, bssid, password)) { fprintf(stderr, "[sae] password element derivation failed\n"); return -1; }
	sae_commit(&s, commit);
	g_mlme.sae_qn = 0;
	const u8 *token = NULL; size_t tlen = 0;
	u8 tokbuf[128];
	struct sae_frame *pf = NULL;
	for (int attempt = 0; attempt < 4 && !pf; attempt++) {
		size_t n = 0;
		body[n++] = 19; body[n++] = 0;				/* group 19 */
		if (token) { memcpy(body + n, token, tlen); n += tlen; }
		memcpy(body + n, commit, 96); n += 96;
		send_sae(1, 0, body, n);
		struct sae_frame *f = sae_wait(1, 800);
		if (!f) continue;
		if (f->status == 76 && f->len > 2) {			/* anti-clogging token request */
			tlen = f->len - 2; if (tlen > sizeof tokbuf) tlen = sizeof tokbuf;
			memcpy(tokbuf, f->data + 2, tlen); token = tokbuf;
			fprintf(stderr, "[sae] anti-clogging token requested\n");
			continue;
		}
		if (f->status) { fprintf(stderr, "[sae] AP rejected commit (status %u)%s\n", f->status, f->status == 126 || f->status == 123 ? " -- AP requires hash-to-element (not supported yet)" : ""); return -2; }
		pf = f;
	}
	if (!pf) { fprintf(stderr, "[sae] no commit from AP\n"); return -3; }
	if (pf->len < 2 + 96 || pf->data[0] != 19) { fprintf(stderr, "[sae] bad commit from AP (len %zu group %u)\n", pf->len, pf->data[0]); return -4; }
	const u8 *peer = pf->data + 2 + (pf->len - 2 - 96);
	if (sae_process_commit(&s, peer)) { fprintf(stderr, "[sae] invalid commit from AP\n"); return -5; }
	/* send confirm, wait for the AP's confirm */
	u8 cbody[2 + 32];
	for (int attempt = 0; attempt < 4; attempt++) {
		cbody[0] = s.send_confirm & 0xff; cbody[1] = s.send_confirm >> 8;
		sae_confirm(&s, cbody + 2);
		send_sae(2, 0, cbody, sizeof cbody);
		struct sae_frame *f = sae_wait(2, 800);
		if (!f) continue;
		if (f->status || f->len < 2 + 32) { fprintf(stderr, "[sae] AP confirm status %u len %zu\n", f->status, f->len); return -6; }
		u16 peer_sc = f->data[0] | (f->data[1] << 8);
		if (sae_check_confirm(&s, peer_sc, f->data + 2)) { fprintf(stderr, "[sae] confirm mismatch (wrong password?)\n"); return -7; }
		memcpy(g_mlme.sae_pmk, s.pmk, 32);
		memcpy(g_mlme.sae_pmkid, s.pmkid, 16);
		return 0;
	}
	fprintf(stderr, "[sae] no confirm from AP\n");
	return -8;
}

static void send_deauth(u16 reason)
{
	struct sk_buff *skb = mgmt_alloc(32);
	struct ieee80211_mgmt *m = skb_put_zero(skb, offsetofend(struct ieee80211_mgmt, u.deauth.reason_code));
	mgmt_hdr(m, IEEE80211_STYPE_DEAUTH);
	m->u.deauth.reason_code = cpu_to_le16(reason);
	mlme_tx_mgmt(skb);
}

/* HT / VHT / HE capability IEs from the hardware's supported-band description */
static u8 *put_ht_cap(u8 *pos, struct ieee80211_supported_band *sb)
{
	struct ieee80211_ht_cap cap;
	if (!sb->ht_cap.ht_supported) return pos;
	memset(&cap, 0, sizeof cap);
	cap.cap_info = cpu_to_le16(sb->ht_cap.cap);
	cap.ampdu_params_info = sb->ht_cap.ampdu_factor | (sb->ht_cap.ampdu_density << 2);
	cap.mcs = sb->ht_cap.mcs;
	*pos++ = WLAN_EID_HT_CAPABILITY; *pos++ = sizeof cap;
	memcpy(pos, &cap, sizeof cap);
	return pos + sizeof cap;
}

static u8 *put_vht_cap(u8 *pos, struct ieee80211_supported_band *sb)
{
	struct ieee80211_vht_cap cap;
	if (!sb->vht_cap.vht_supported) return pos;
	cap.vht_cap_info = cpu_to_le32(sb->vht_cap.cap);
	cap.supp_mcs = sb->vht_cap.vht_mcs;
	*pos++ = WLAN_EID_VHT_CAPABILITY; *pos++ = sizeof cap;
	memcpy(pos, &cap, sizeof cap);
	return pos + sizeof cap;
}

static const struct ieee80211_sta_he_cap *sb_he_cap(struct ieee80211_supported_band *sb)
{
	for (int i = 0; i < sb->n_iftype_data; i++)
		if ((sb->iftype_data[i].types_mask & BIT(NL80211_IFTYPE_STATION)) && sb->iftype_data[i].he_cap.has_he)
			return &sb->iftype_data[i].he_cap;
	return NULL;
}

static int he_mcs_nss_len(const struct ieee80211_he_cap_elem *e)
{
	int n = 4;
	if (e->phy_cap_info[0] & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) n += 4;
	if (e->phy_cap_info[0] & IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) n += 4;
	return n;
}

static u8 *put_he_cap(u8 *pos, struct ieee80211_supported_band *sb)
{
	const struct ieee80211_sta_he_cap *he = sb_he_cap(sb);
	if (!he) return pos;
	int mcs = he_mcs_nss_len(&he->he_cap_elem);
	int ppe = 0;
	if (he->he_cap_elem.phy_cap_info[6] & IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
		u8 nss = (he->ppe_thres[0] & 0x7) + 1;
		u8 ru = hweight8((he->ppe_thres[0] >> 3) & 0xf);
		ppe = (7 + nss * ru * 6) / 8;
		(void)ppe;
		ppe = DIV_ROUND_UP(7 + nss * ru * 6, 8);
	}
	*pos++ = WLAN_EID_EXTENSION;
	*pos++ = 1 + sizeof(struct ieee80211_he_cap_elem) + mcs + ppe;
	*pos++ = WLAN_EID_EXT_HE_CAPABILITY;
	memcpy(pos, &he->he_cap_elem, sizeof(struct ieee80211_he_cap_elem)); pos += sizeof(struct ieee80211_he_cap_elem);
	memcpy(pos, &he->he_mcs_nss_supp, mcs); pos += mcs;
	memcpy(pos, he->ppe_thres, ppe); pos += ppe;
	return pos;
}

static const u8 rsn_ccmp_psk[] = {	/* RSN IE: v1, group CCMP, pairwise CCMP, AKM PSK, caps 0 */
	WLAN_EID_RSN, 20, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
	0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00 };

/* WPA3 RSN element: CCMP, AKM SAE, MFP capable+required, PMKID from the SAE exchange, group management cipher BIP-CMAC-128 */
static size_t build_rsn_sae(u8 *out)
{
	static const u8 head[] = { WLAN_EID_RSN, 2 + 4 + 2 + 4 + 2 + 4 + 2 + 2 + 16 + 4,
		0x01, 0x00,				/* version */
		0x00, 0x0f, 0xac, 0x04,			/* group data cipher: CCMP */
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,	/* pairwise: CCMP */
		0x01, 0x00, 0x00, 0x0f, 0xac, 0x08,	/* AKM: SAE */
		0xc0, 0x00,				/* capabilities: MFPR | MFPC */
		0x01, 0x00 };				/* PMKID count */
	static const u8 tail[] = { 0x00, 0x0f, 0xac, 0x06 };	/* group management cipher: BIP-CMAC-128 */
	size_t n = 0;
	memcpy(out, head, sizeof head); n = sizeof head;
	memcpy(out + n, g_mlme.sae_pmkid, 16); n += 16;
	memcpy(out + n, tail, sizeof tail); n += sizeof tail;
	return n;
}

static void send_assoc_req(void)
{
	struct ieee80211_supported_band *sb = g_mm->hw.wiphy->bands[g_mlme.chan->band];
	struct sk_buff *skb = mgmt_alloc(512);
	struct ieee80211_mgmt *m = skb_put_zero(skb, offsetof(struct ieee80211_mgmt, u.assoc_req.variable));
	mgmt_hdr(m, IEEE80211_STYPE_ASSOC_REQ);
	u16 capab = WLAN_CAPABILITY_ESS;
	if (g_mlme.secure) capab |= WLAN_CAPABILITY_PRIVACY;
	if (g_mlme.chan->band == NL80211_BAND_2GHZ) capab |= WLAN_CAPABILITY_SHORT_PREAMBLE | WLAN_CAPABILITY_SHORT_SLOT_TIME;
	m->u.assoc_req.capab_info = cpu_to_le16(capab);
	m->u.assoc_req.listen_interval = cpu_to_le16(10);
	u8 *pos = skb_put(skb, 400);
	u8 *start = pos;
	/* SSID */
	*pos++ = WLAN_EID_SSID; *pos++ = g_mlme.ssid_len; memcpy(pos, g_mlme.ssid, g_mlme.ssid_len); pos += g_mlme.ssid_len;
	/* supported rates: our bitrates, basic flag for those the AP marks basic */
	{
		u8 rates[16]; int n = 0;
		for (int i = 0; i < sb->n_bitrates && n < 16; i++) {
			if (g_mlme.chan->band == NL80211_BAND_2GHZ && (sb->bitrates[i].flags & 0)) continue;
			u8 r = sb->bitrates[i].bitrate / 5;
			if (g_mlme.ap_basic_rates & BIT(i)) r |= 0x80;
			rates[n++] = r;
		}
		int n1 = n > 8 ? 8 : n;
		*pos++ = WLAN_EID_SUPP_RATES; *pos++ = n1; memcpy(pos, rates, n1); pos += n1;
		if (n > 8) { *pos++ = WLAN_EID_EXT_SUPP_RATES; *pos++ = n - 8; memcpy(pos, rates + 8, n - 8); pos += n - 8; }
	}
	/* power capability / supported channels are optional; RSN */
	if (g_mlme.secure) {
		if (g_mlme.sae_mode) {
			size_t n = build_rsn_sae(pos);
			memcpy(g_mlme.assoc_req_rsn, pos, n); g_mlme.assoc_req_rsn_len = n; pos += n;
		} else {
			memcpy(pos, rsn_ccmp_psk, sizeof rsn_ccmp_psk); pos += sizeof rsn_ccmp_psk;
			memcpy(g_mlme.assoc_req_rsn, rsn_ccmp_psk, sizeof rsn_ccmp_psk); g_mlme.assoc_req_rsn_len = sizeof rsn_ccmp_psk;
		}
	}
	/* extended capabilities (BSS transition etc. not claimed): minimal */
	*pos++ = WLAN_EID_EXT_CAPABILITY; *pos++ = 8; memset(pos, 0, 8); pos[2] = 0x00; pos[7] = 0x40; pos += 8;
	/* HT, VHT, HE */
	pos = put_ht_cap(pos, sb);
	pos = put_vht_cap(pos, sb);
	pos = put_he_cap(pos, sb);
	/* WMM */
	{ static const u8 wmm[] = { WLAN_EID_VENDOR_SPECIFIC, 7, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00 }; memcpy(pos, wmm, sizeof wmm); pos += sizeof wmm; }
	skb_trim(skb, skb->len - (400 - (pos - start)));
	g_mlme.assoc_req_ies = malloc(pos - start);
	memcpy(g_mlme.assoc_req_ies, start, pos - start);
	g_mlme.assoc_req_ies_len = pos - start;
	mlme_tx_mgmt(skb);
}

/* ---------------- capability parsing (assoc response -> sta/bss_conf) ---------------- */
static u32 rates_to_bitmap(struct ieee80211_supported_band *sb, const u8 *ie, bool basic_only, u32 *basic)
{
	u32 map = 0;
	if (!ie) return 0;
	for (int i = 0; i < ie[1]; i++) {
		u8 r = ie[2 + i] & 0x7f;
		for (int j = 0; j < sb->n_bitrates; j++)
			if (sb->bitrates[j].bitrate == r * 5) {
				map |= BIT(j);
				if ((ie[2 + i] & 0x80) && basic) *basic |= BIT(j);
			}
	}
	return map;
}

static enum ieee80211_sta_rx_bandwidth sta_bw(const struct ieee80211_sta_ht_cap *ht, const struct ieee80211_sta_vht_cap *vht, const struct ieee80211_sta_he_cap *he, enum nl80211_band band, u32 ap_width)
{
	/* operate at the narrower of what the AP offers and what we use (we configure 20 or 40/80 via chanctx width) */
	switch (ap_width) {
	case NL80211_CHAN_WIDTH_160: case NL80211_CHAN_WIDTH_80P80: return IEEE80211_STA_RX_BW_160;
	case NL80211_CHAN_WIDTH_80: return IEEE80211_STA_RX_BW_80;
	case NL80211_CHAN_WIDTH_40: return IEEE80211_STA_RX_BW_40;
	default: return IEEE80211_STA_RX_BW_20;
	}
}

static void fill_peer_caps(struct ieee80211_sta *sta, struct ieee80211_supported_band *sb, const u8 *ies, size_t len, struct ieee80211_bss_conf *bc)
{
	struct ieee80211_link_sta *ls = &sta->deflink;
	const u8 *ie;
	u32 basic = 0;
	ls->supp_rates[sb->band] = rates_to_bitmap(sb, ie_find(ies, len, WLAN_EID_SUPP_RATES), false, &basic);
	ls->supp_rates[sb->band] |= rates_to_bitmap(sb, ie_find(ies, len, WLAN_EID_EXT_SUPP_RATES), false, &basic);
	bc->basic_rates = basic;
	g_mlme.ap_basic_rates = basic;
	/* HT */
	ie = ie_find(ies, len, WLAN_EID_HT_CAPABILITY);
	if (ie && ie[1] >= sizeof(struct ieee80211_ht_cap) && sb->ht_cap.ht_supported) {
		const struct ieee80211_ht_cap *c = (const void *)(ie + 2);
		ls->ht_cap.ht_supported = true;
		ls->ht_cap.cap = le16_to_cpu(c->cap_info) & sb->ht_cap.cap;
		ls->ht_cap.ampdu_factor = c->ampdu_params_info & 3;
		ls->ht_cap.ampdu_density = (c->ampdu_params_info >> 2) & 7;
		ls->ht_cap.mcs = c->mcs;
		for (int i = 0; i < 10; i++) ls->ht_cap.mcs.rx_mask[i] &= sb->ht_cap.mcs.rx_mask[i];
	}
	/* VHT */
	ie = ie_find(ies, len, WLAN_EID_VHT_CAPABILITY);
	if (ie && ie[1] >= sizeof(struct ieee80211_vht_cap) && sb->vht_cap.vht_supported) {
		const struct ieee80211_vht_cap *c = (const void *)(ie + 2);
		ls->vht_cap.vht_supported = true;
		ls->vht_cap.cap = le32_to_cpu(c->vht_cap_info);
		ls->vht_cap.vht_mcs = c->supp_mcs;
	}
	/* HE */
	ie = ie_find_ext(ies, len, WLAN_EID_EXT_HE_CAPABILITY);
	if (ie && sb_he_cap(sb)) {
		const u8 *p = ie + 3;
		size_t n = ie[1] - 1;
		if (n >= sizeof(struct ieee80211_he_cap_elem) + 4) {
			ls->he_cap.has_he = true;
			memcpy(&ls->he_cap.he_cap_elem, p, sizeof(struct ieee80211_he_cap_elem));
			int mcs = he_mcs_nss_len(&ls->he_cap.he_cap_elem);
			memset(&ls->he_cap.he_mcs_nss_supp, 0, sizeof(ls->he_cap.he_mcs_nss_supp));
			memcpy(&ls->he_cap.he_mcs_nss_supp, p + sizeof(struct ieee80211_he_cap_elem), mcs);
			size_t ppe = n - sizeof(struct ieee80211_he_cap_elem) - mcs;
			if (ppe > sizeof(ls->he_cap.ppe_thres)) ppe = sizeof(ls->he_cap.ppe_thres);
			memcpy(ls->he_cap.ppe_thres, p + sizeof(struct ieee80211_he_cap_elem) + mcs, ppe);
		}
	}
	bc->he_support = ls->he_cap.has_he;
	/* HE operation: bss color, params */
	ie = ie_find_ext(ies, len, WLAN_EID_EXT_HE_OPERATION);
	if (ie && bc->he_support && ie[1] >= 7) {
		const u8 *p = ie + 3;
		bc->he_oper.params = p[0] | (p[1] << 8) | (p[2] << 16);
		bc->he_oper.nss_set = p[4] | (p[5] << 8);
		u8 color = p[3];
		bc->he_bss_color.color = color & 0x3f;
		bc->he_bss_color.enabled = !(color & 0x80);
		bc->he_bss_color.partial = !!(color & 0x40);
	}
	/* HT operation */
	ie = ie_find(ies, len, WLAN_EID_HT_OPERATION);
	if (ie && ie[1] >= 5) bc->ht_operation_mode = ie[4] | (ie[5] << 8);
	/* rx nss from the MCS maps */
	int nss = 1;
	if (ls->vht_cap.vht_supported) {
		u16 map = le16_to_cpu(ls->vht_cap.vht_mcs.rx_mcs_map);
		nss = 0;
		for (int i = 0; i < 8; i++) if (((map >> (2 * i)) & 3) != 3) nss = i + 1;
	} else if (ls->ht_cap.ht_supported) {
		nss = ls->ht_cap.mcs.rx_mask[1] ? 2 : 1;
	}
	if (ls->he_cap.has_he) {
		u16 m = le16_to_cpu(ls->he_cap.he_mcs_nss_supp.rx_mcs_80);
		int n = 0;
		for (int i = 0; i < 8; i++) if (((m >> (2 * i)) & 3) != 3) n = i + 1;
		if (n) nss = n;
	}
	ls->rx_nss = nss ? nss : 1;
	ls->smps_mode = IEEE80211_SMPS_OFF;
	ls->bandwidth = IEEE80211_STA_RX_BW_20;
	sta->wme = true;
	sta->max_rx_aggregation_subframes = 64;
	sta->max_amsdu_subframes = 0;
	ls->agg.max_amsdu_len = 7935;
	ls->agg.max_rc_amsdu_len = 7935;
	ls->agg.max_tid_amsdu_len[0] = 7935;
}

/* ---------------- station / channel context bookkeeping ---------------- */
static struct ieee80211_sta *sta_alloc(const u8 *addr)
{
	struct ieee80211_hw *hw = &g_mm->hw;
	struct ieee80211_sta *sta = calloc(1, sizeof(*sta) + hw->sta_data_size + 64);
	memcpy(sta->addr, addr, 6);
	sta->deflink.sta = sta;
	memcpy(sta->deflink.addr, addr, 6);
	sta->deflink.link_id = 0;
	sta->link[0] = &sta->deflink;
	sta->cur = &sta->deflink.agg;
	sta->wme = true;
	for (int i = 0; i <= IEEE80211_NUM_TIDS; i++) {
		struct ieee80211_txq *q = calloc(1, sizeof(*q) + hw->txq_data_size + 16);
		q->vif = g_mm->vif;
		q->sta = sta;
		q->tid = i;
		q->ac = i < 8 ? ((u8[]){IEEE80211_AC_BE, IEEE80211_AC_BK, IEEE80211_AC_BK, IEEE80211_AC_BE, IEEE80211_AC_VI, IEEE80211_AC_VI, IEEE80211_AC_VO, IEEE80211_AC_VO})[i] : IEEE80211_AC_VO;
		sta->txq[i] = q;
	}
	return sta;
}

static int sta_set_state(struct ieee80211_sta *sta, enum ieee80211_sta_state from, enum ieee80211_sta_state to)
{
	struct mm *m = g_mm;
	wiphy_lock(m->hw.wiphy);
	int r = m->ops->sta_state(&m->hw, m->vif, sta, from, to);
	wiphy_unlock(m->hw.wiphy);
	if (r) fprintf(stderr, "[mlme] sta_state %d->%d returned %d\n", from, to, r);
	return r;
}

/* choose the operating channel width/centre from the AP's HT/VHT operation elements and our hardware capabilities */
static void pick_width(const struct bss_entry *bss, struct ieee80211_supported_band *sb, u32 primary, u32 *width, u32 *center)
{
	*width = NL80211_CHAN_WIDTH_20;
	*center = primary;
	const u8 *ht = ie_find(bss->ies, bss->ie_len, WLAN_EID_HT_OPERATION);
	const u8 *vht = ie_find(bss->ies, bss->ie_len, WLAN_EID_VHT_OPERATION);
	if (ht && ht[1] >= 2 && sb->ht_cap.ht_supported && (sb->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)) {
		u8 param = ht[3], off = param & 3;
		if ((param & 4) && (off == 1 || off == 3)) {
			*width = NL80211_CHAN_WIDTH_40;
			*center = primary + (off == 1 ? 10 : -10);
		}
	}
	if (vht && vht[1] >= 3 && sb->vht_cap.vht_supported && sb->band == NL80211_BAND_5GHZ && *width == NL80211_CHAN_WIDTH_40) {
		u8 vw = vht[2], seg0 = vht[3];
		if (vw >= 1 && seg0) {
			*width = NL80211_CHAN_WIDTH_80;
			if (vw == 1) *center = 5000 + 5 * seg0;
			else {	/* legacy 160/80+80 coding: derive the 80 MHz block containing the primary channel */
				int ch = (primary - 5000) / 5, c80;
				if (ch >= 36 && ch <= 48) c80 = 42; else if (ch >= 52 && ch <= 64) c80 = 58;
				else if (ch >= 100 && ch <= 112) c80 = 106; else if (ch >= 116 && ch <= 128) c80 = 122;
				else if (ch >= 132 && ch <= 144) c80 = 138; else c80 = 155;
				*center = 5000 + 5 * c80;
			}
		}
	}
}

static void setup_chanctx(struct ieee80211_channel *chan, u32 width, u32 center)
{
	struct mm *m = g_mm;
	struct ieee80211_hw *hw = &m->hw;
	struct ieee80211_chanctx_conf *ctx = calloc(1, sizeof(*ctx) + hw->chanctx_data_size + 64);
	ctx->def.chan = chan;
	ctx->def.width = width;
	ctx->def.center_freq1 = center;
	ctx->min_def = ctx->def;
	ctx->ap = ctx->def;
	ctx->rx_chains_static = 2;
	ctx->rx_chains_dynamic = 2;
	struct ieee80211_bss_conf *bc = &m->vif->bss_conf;
	bc->chanreq.oper = ctx->def;
	bc->chanreq.ap = ctx->def;
	bc->chanctx_conf = ctx;
	wiphy_lock(hw->wiphy);
	int r = m->ops->add_chanctx(hw, ctx);
	if (!r) r = m->ops->assign_vif_chanctx(hw, m->vif, bc, ctx);
	wiphy_unlock(hw->wiphy);
	if (r) fprintf(stderr, "[mlme] chanctx setup returned %d\n", r);
	m->chanctx = ctx;
}

static void teardown_chanctx(void)
{
	struct mm *m = g_mm;
	if (!m->chanctx) return;
	wiphy_lock(m->hw.wiphy);
	m->ops->unassign_vif_chanctx(&m->hw, m->vif, &m->vif->bss_conf, m->chanctx);
	m->ops->remove_chanctx(&m->hw, m->chanctx);
	wiphy_unlock(m->hw.wiphy);
	m->vif->bss_conf.chanctx_conf = NULL;
	free(m->chanctx);
	m->chanctx = NULL;
}

/* ---------------- rx dispatch ---------------- */
void mlme_tx_status(struct sk_buff *skb, bool acked)
{
	struct ieee80211_hdr *h = (void *)skb->data;
	if (getenv("RTW89_MLMEDBG")) fprintf(stderr, "[mlme] tx status len %u fc 0x%04x acked=%d\n", skb->len, le16_to_cpu(h->frame_control), acked);
	if (skb->len >= 24 && ieee80211_is_data(h->frame_control) && g_mlme.eapol_tx_wait) {
		g_mlme.eapol_tx_acked = acked;
		complete_all(&g_mlme.eapol_tx_done);
	}
}

void mlme_scan_done(bool aborted)
{
	fprintf(stderr, "[mlme] scan %s, %d BSS\n", aborted ? "aborted" : "complete", bss_n);
}

extern void supplicant_rx_eapol(const u8 *sa, const u8 *frame, size_t len);
extern void net_rx_eth(const u8 *eth, size_t len);

static void handle_data(struct sk_buff *skb)
{
	struct ieee80211_rx_status *st = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *h = (void *)skb->data;
	__le16 fc = h->frame_control;
	if (ieee80211_is_data(fc) && !ieee80211_is_nullfunc(fc) && !ieee80211_is_qos_nullfunc(fc) && !is_multicast_ether_addr(h->addr1)) {
		g_mlme.rx_rate.enc = st->encoding; g_mlme.rx_rate.nss = st->nss; g_mlme.rx_rate.bw = st->bw; g_mlme.rx_rate.idx = st->rate_idx; g_mlme.rx_rate.gi = st->enc_flags & RX_ENC_FLAG_SHORT_GI ? 1 : 0; g_mlme.rx_rate.n++;
	}
	if (!ieee80211_is_data(fc) || ieee80211_is_nullfunc(fc) || ieee80211_is_qos_nullfunc(fc)) return;
	if (!ieee80211_has_fromds(fc) || ieee80211_has_tods(fc)) return;
	if (memcmp(h->addr2, g_bssid, 6)) return;
	size_t hdrlen = ieee80211_hdrlen(fc);
	if (skb->len < hdrlen) return;
	u8 *payload = skb->data + hdrlen;
	size_t plen = skb->len - hdrlen;
	bool amsdu = false;
	if (ieee80211_is_data_qos(fc)) {
		u16 qc = le16_to_cpu(*(__le16 *)ieee80211_get_qos_ctl(h));
		amsdu = qc & IEEE80211_QOS_CTL_A_MSDU_PRESENT;
	}
	if (ieee80211_has_protected(fc)) {
		if (!(st->flag & RX_FLAG_DECRYPTED)) {
			st_rx_undecrypted++;
			if (is_multicast_ether_addr(h->addr1)) st_rx_undec_group++; else st_rx_undec_uni++;
			return;
		}
		if (!(st->flag & RX_FLAG_IV_STRIPPED)) {
			if (plen < 8 + 8) return;
			payload += 8; plen -= 8;
		}
		if (!(st->flag & RX_FLAG_MIC_STRIPPED)) {
			if (plen < 8) return;
			plen -= 8;
		}
	}
	st_rx_data++; st_rx_bytes += skb->len;
	const u8 *da = h->addr1, *sa = h->addr3;
	if (amsdu) {
		while (plen >= 14) {
			u16 l = (payload[12] << 8) | payload[13];
			if (14 + l > plen) break;
			/* subframe is already an Ethernet header followed by an LLC/SNAP-encapsulated payload */
			const u8 *llc = payload + 14;
			if (l >= 8 && !memcmp(llc, rfc1042_header, 6)) {
				u8 eth[2048];
				if (6 + 6 + 2 + (l - 8) > sizeof eth) break;
				memcpy(eth, payload, 12);
				memcpy(eth + 12, llc + 6, 2);
				memcpy(eth + 14, llc + 8, l - 8);
				net_rx_eth(eth, 14 + l - 8);
			}
			size_t adv = (14 + l + 3) & ~3u;
			if (adv > plen) break;
			payload += adv; plen -= adv;
		}
		return;
	}
	if (plen < 8) return;
	if (memcmp(payload, rfc1042_header, 6) && memcmp(payload, bridge_tunnel_header, 6)) return;
	u16 ethertype = (payload[6] << 8) | payload[7];
	if (ethertype == ETH_P_PAE) { supplicant_rx_eapol(sa, payload + 8, plen - 8); return; }
	if (!g_mlme.connected) return;
	u8 eth[2048];
	if (14 + plen - 8 > sizeof eth) return;
	memcpy(eth, da, 6); memcpy(eth + 6, sa, 6);
	eth[12] = payload[6]; eth[13] = payload[7];
	memcpy(eth + 14, payload + 8, plen - 8);
	net_rx_eth(eth, 14 + plen - 8);
}

void mlme_rx(struct sk_buff *skb)
{
	st_rx_frames++;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc;
	if (skb->len < 10) { kfree_skb(skb); return; }
	fc = hdr->frame_control;
	if (g_rx_debug && !ieee80211_is_beacon(fc) && !ieee80211_is_probe_resp(fc) && g_mlme.state >= MLME_AUTH && skb->len >= 24 && !memcmp(hdr->addr2, g_bssid, 6)) {
		struct ieee80211_rx_status *rs = IEEE80211_SKB_RXCB(skb);
		fprintf(stderr, "[rx] fc=0x%04x len=%u flag=0x%x:", le16_to_cpu(fc), skb->len, rs->flag);
		for (unsigned i = 0; i < skb->len && i < 56; i++) fprintf(stderr, " %02x", skb->data[i]);
		fprintf(stderr, "\n");
	}
	if (skb->len >= 24 && (ieee80211_is_beacon(fc) || ieee80211_is_probe_resp(fc))) {
		handle_beacon(skb);
	} else if (g_mlme.state >= MLME_AUTH && skb->len >= 24 && ieee80211_is_mgmt(fc) && !memcmp(hdr->addr2, g_bssid, 6)) {
		struct ieee80211_mgmt *m = (void *)skb->data;
		if (ieee80211_is_auth(fc) && skb->len >= offsetof(struct ieee80211_mgmt, u.auth.variable)) {
			if (le16_to_cpu(m->u.auth.auth_alg) == WLAN_AUTH_SAE) {
				sae_push(le16_to_cpu(m->u.auth.auth_transaction), le16_to_cpu(m->u.auth.status_code),
					 m->u.auth.variable, skb->len - offsetof(struct ieee80211_mgmt, u.auth.variable));
			} else {
				g_mlme.auth_status = le16_to_cpu(m->u.auth.status_code);
				g_mlme.auth_seq = le16_to_cpu(m->u.auth.auth_transaction);
			}
			complete_all(&g_mlme.auth_done);
		} else if (ieee80211_is_assoc_resp(fc) && skb->len >= offsetof(struct ieee80211_mgmt, u.assoc_resp.variable)) {
			g_mlme.assoc_status = le16_to_cpu(m->u.assoc_resp.status_code);
			g_mlme.assoc_aid = le16_to_cpu(m->u.assoc_resp.aid) & 0x3fff;
			g_mlme.assoc_capab = le16_to_cpu(m->u.assoc_resp.capab_info);
			size_t l = skb->len - offsetof(struct ieee80211_mgmt, u.assoc_resp.variable);
			if (l > sizeof g_mlme.assoc_resp_ies) l = sizeof g_mlme.assoc_resp_ies;
			memcpy(g_mlme.assoc_resp_ies, m->u.assoc_resp.variable, l);
			g_mlme.assoc_resp_ies_len = l;
			complete_all(&g_mlme.assoc_done);
		} else if (ieee80211_is_action(fc)) {
			handle_action(skb);
		} else if (ieee80211_is_deauth(fc) || ieee80211_is_disassoc(fc)) {
			fprintf(stderr, "[mlme] %s from AP (reason %u)\n", ieee80211_is_deauth(fc) ? "deauth" : "disassoc", le16_to_cpu(m->u.deauth.reason_code));
			g_mlme.deauthed = true;
			complete_all(&g_mlme.auth_done);
			complete_all(&g_mlme.assoc_done);
			complete_all(&g_mlme.eapol_done);
			if (g_mlme.connected && g_mlme.on_disconnect) g_mlme.on_disconnect();
		}
	} else if (g_mlme.state >= MLME_ASSOC && ieee80211_is_data(fc)) {
		handle_data(skb);
	}
	kfree_skb(skb);
}

/* ---------------- connect ---------------- */
/* BSSIDs that already refused us during this connect() call (several APs can share one SSID) */
static u8 tried_bss[8][6];
static int n_tried;

static bool already_tried(const u8 *bssid)
{
	for (int i = 0; i < n_tried; i++) if (!memcmp(tried_bss[i], bssid, 6)) return true;
	return false;
}

static struct bss_entry *find_bss(const char *ssid)
{
	struct bss_entry *best = NULL;
	pthread_mutex_lock(&bss_lock);
	for (int i = 0; i < bss_n; i++) {
		struct bss_entry *b = &bss_tab[i];
		if (b->ssid_len != strlen(ssid) || memcmp(b->ssid, ssid, b->ssid_len)) continue;
		if (already_tried(b->bssid)) continue;
		if (g_mlme.secure != b->rsn) continue;
		if (g_mlme.secure && !b->akm_psk && !b->akm_sae) continue;		/* only AKMs we implement */
		if (!best || b->signal > best->signal) best = b;
	}
	pthread_mutex_unlock(&bss_lock);
	return best;
}

static int wait_cpl(struct completion *c, int ms) { return wait_for_completion_timeout(c, ms) != 0; }

static void reset_completions(void)
{
	init_completion(&g_mlme.auth_done);
	init_completion(&g_mlme.assoc_done);
	init_completion(&g_mlme.eapol_done);
	init_completion(&g_mlme.eapol_tx_done);
	g_mlme.deauthed = false;
}

void mlme_lock(void) { pthread_mutex_lock(&bss_lock); }
void mlme_unlock(void) { pthread_mutex_unlock(&bss_lock); }

extern int g_scan_only_chan;

/* quick scan of one channel for one network, used right after start to rejoin the previous network */
int mlme_scan_quick(const char *ssid, int chan)
{
	g_scan_only_chan = chan;
	int r = mlme_scan(ssid);
	g_scan_only_chan = 0;
	return r;
}

int mlme_scan(const char *ssid)
{
	unsigned long t0 = jiffies;
	int r = mm_hw_scan(ssid ? (const u8 *)ssid : NULL, ssid ? strlen(ssid) : 0);
	if (r) return r;
	wait_for_completion_timeout(&g_mm->scan_done, 30000);
	/* age out access points that were not heard during this scan (keep the one we are connected to) */
	pthread_mutex_lock(&bss_lock);
	int w = 0;
	for (int i = 0; i < bss_n; i++) {
		bool cur = g_mlme.state >= MLME_AUTH && !memcmp(bss_tab[i].bssid, g_bssid, 6);
		if (bss_tab[i].seen + 1 >= t0 || cur) bss_tab[w++] = bss_tab[i];
	}
	bss_n = w;
	pthread_mutex_unlock(&bss_lock);
	return 0;
}

#include "supplicant.h"
void mlme_set_authorized(void)
{
	sta_set_state(g_mlme.sta, IEEE80211_STA_ASSOC, IEEE80211_STA_AUTHORIZED);
	g_mlme.state = MLME_CONNECTED;
	mlme_ba_reset();
	g_mlme.connected = true;
	fprintf(stderr, "[mlme] CONNECTED (authorized)\n");
	mlme_ba_start_tx(0);
}

static int mlme_connect_once(const char *ssid, const char *psk);

int mlme_connect(const char *ssid, const char *psk)
{
	int r = -ECONNREFUSED;
	n_tried = 0;
	for (int i = 0; i < 4 && r == -ECONNREFUSED; i++) {
		r = mlme_connect_once(ssid, psk);
		if (r == -ECONNREFUSED && n_tried < 8) {
			memcpy(tried_bss[n_tried++], g_bssid_last, 6);
			fprintf(stderr, "[mlme] that access point did not accept us; trying another one with the same name\n");
		}
	}
	return r;
}

static int mlme_connect_once(const char *ssid, const char *psk)
{
	bool secure = psk != NULL;
	struct mm *m = g_mm;
	struct ieee80211_hw *hw = &m->hw;
	struct bss_entry *b;
	g_mlme.secure = secure;
	g_mlme.ssid_len = strlen(ssid);
	memcpy(g_mlme.ssid, ssid, g_mlme.ssid_len);

	b = find_bss(ssid);
	/* a channel can be missed in a single sweep (DFS channels are listened to passively): try again before giving up */
	for (int tries = 0; !b && tries < 4; tries++) { mlme_scan(ssid); b = find_bss(ssid); if (!b) fprintf(stderr, "[mlme] \"%s\" not seen in scan %d\n", ssid, tries + 1); }
	if (!b) { fprintf(stderr, "[mlme] SSID \"%s\" not found\n", ssid); return -ENOENT; }
	struct bss_entry bss = *b;
	fprintf(stderr, "[mlme] selected %02x:%02x:%02x:%02x:%02x:%02x ch%d %d MHz %d dBm\n", bss.bssid[0], bss.bssid[1], bss.bssid[2], bss.bssid[3], bss.bssid[4], bss.bssid[5], bss.chan, bss.freq, bss.signal);

	memcpy(g_bssid, bss.bssid, 6);
	memcpy(g_bssid_last, bss.bssid, 6);
	g_mlme.chan = mm_find_channel(bss.freq);
	if (!g_mlme.chan) return -EINVAL;
	struct ieee80211_supported_band *sb = hw->wiphy->bands[g_mlme.chan->band];
	reset_completions();
	g_mlme.state = MLME_AUTH;
	g_mlme.connected = false;

	/* channel context, BSSID */
	u32 width, center;
	pick_width(&bss, sb, g_mlme.chan->center_freq, &width, &center);
	g_mlme.width = width;
	fprintf(stderr, "[mlme] operating width %s, centre %u MHz\n", width == NL80211_CHAN_WIDTH_80 ? "80" : width == NL80211_CHAN_WIDTH_40 ? "40" : "20", center);
	setup_chanctx(g_mlme.chan, width, center);
	struct ieee80211_vif *vif = m->vif;
	memcpy(vif->cfg.ap_addr, g_bssid, 6);
	vif->bss_conf.bssid = g_bssid;
	vif->bss_conf.beacon_int = bss.beacon_int ? bss.beacon_int : 100;
	vif->bss_conf.dtim_period = 1;
	{
		const u8 *tim = ie_find(bss.ies, bss.ie_len, WLAN_EID_TIM);
		if (tim && tim[1] >= 3) vif->bss_conf.dtim_period = tim[3] ? tim[3] : 1;
	}
	memcpy(vif->cfg.ssid, ssid, g_mlme.ssid_len);
	vif->cfg.ssid_len = g_mlme.ssid_len;
	vif->cfg.idle = false;
	wiphy_lock(hw->wiphy);
	m->ops->link_info_changed(hw, vif, &vif->bss_conf, BSS_CHANGED_BSSID | BSS_CHANGED_BEACON_INT);
	wiphy_unlock(hw->wiphy);

	/* AP station: NOTEXIST -> NONE -> AUTH */
	struct ieee80211_sta *sta = sta_alloc(g_bssid);
	g_mlme.sta = sta;
	struct mm_sta *ms = calloc(1, sizeof(*ms));
	ms->sta = sta;
	list_add_tail(&ms->list, &m->stas);
	sta_set_state(sta, IEEE80211_STA_NOTEXIST, IEEE80211_STA_NONE);
	sta_set_state(sta, IEEE80211_STA_NONE, IEEE80211_STA_AUTH);

	/* authentication: open system, or SAE for WPA3 networks that do not offer WPA2-PSK */
	int ok = 0;
	g_mlme.sae_mode = secure && bss.akm_sae && (!bss.akm_psk || getenv("RTW89_PREFER_SAE") || g_prefer_sae);
	if (g_mlme.sae_mode) {
		fprintf(stderr, "[mlme] WPA3-Personal (SAE) network%s\n", bss.mfpr ? ", PMF required" : "");
		g_mlme.deauthed = false;
		sta->mfp = true;
		int sr = do_sae(psk, g_bssid);
		if (sr) { fprintf(stderr, "[mlme] SAE failed (%d)\n", sr); goto fail; }
		ok = 1;
	} else {
		for (int i = 0; i < 4 && !ok; i++) {
			reinit_completion(&g_mlme.auth_done);
			send_auth();
			if (wait_cpl(&g_mlme.auth_done, 600) && !g_mlme.deauthed && g_mlme.auth_seq == 2) ok = 1;
		}
		if (!ok || g_mlme.auth_status) {
			fprintf(stderr, "[mlme] authentication failed (status %d)\n", g_mlme.auth_status);
			goto fail;
		}
	}
	fprintf(stderr, "[mlme] authenticated\n");

	/* association */
	g_mlme.state = MLME_ASSOC;
	if (secure) { if (g_mlme.sae_mode) supplicant_start_sae(g_mlme.sae_pmk); else supplicant_start(ssid, psk); }
	ok = 0;
	fill_peer_caps(sta, sb, bss.ies, bss.ie_len, &vif->bss_conf);	/* provisional: rates for the request */
	for (int i = 0; i < 4 && !ok; i++) {
		reinit_completion(&g_mlme.assoc_done);
		send_assoc_req();
		if (wait_cpl(&g_mlme.assoc_done, 800) && !g_mlme.deauthed) ok = 1;
	}
	if (!ok || g_mlme.assoc_status) {
		fprintf(stderr, "[mlme] association failed (status %d)\n", g_mlme.assoc_status);
		goto fail;
	}
	fprintf(stderr, "[mlme] associated, AID %d\n", g_mlme.assoc_aid);

	/* peer capabilities from the association response (falls back to beacon IEs) */
	{
		u8 ies[2048]; size_t l = 0;
		memcpy(ies, g_mlme.assoc_resp_ies, g_mlme.assoc_resp_ies_len); l = g_mlme.assoc_resp_ies_len;
		struct ieee80211_bss_conf *bc = &vif->bss_conf;
		/* take HT/VHT/HE capability IEs from the beacon (assoc resp usually carries only operation IEs) */
		memcpy(ies + l, bss.ies, bss.ie_len < sizeof ies - l ? bss.ie_len : sizeof ies - l); l += bss.ie_len < sizeof ies - l ? bss.ie_len : sizeof ies - l;
		memset(&sta->deflink.ht_cap, 0, sizeof sta->deflink.ht_cap);
		memset(&sta->deflink.vht_cap, 0, sizeof sta->deflink.vht_cap);
		memset(&sta->deflink.he_cap, 0, sizeof sta->deflink.he_cap);
		fill_peer_caps(sta, sb, ies, l, bc);
		bc->assoc_capability = g_mlme.assoc_capab;
		bc->use_short_slot = !!(g_mlme.assoc_capab & WLAN_CAPABILITY_SHORT_SLOT_TIME) || g_mlme.chan->band != NL80211_BAND_2GHZ;
		bc->use_short_preamble = !!(g_mlme.assoc_capab & WLAN_CAPABILITY_SHORT_PREAMBLE);
		bc->qos = true;
		bc->txpower = 20;
		/* operating width: use what the AP's operation IEs permit within our hardware capability */
		sta->deflink.bandwidth = g_mlme.width == NL80211_CHAN_WIDTH_80 ? IEEE80211_STA_RX_BW_80 : g_mlme.width == NL80211_CHAN_WIDTH_40 ? IEEE80211_STA_RX_BW_40 : IEEE80211_STA_RX_BW_20;
	}
	sta->aid = g_mlme.assoc_aid;
	vif->cfg.aid = g_mlme.assoc_aid;
	vif->cfg.assoc = true;
	g_mlme.state = MLME_ASSOCIATED;

	sta_set_state(sta, IEEE80211_STA_AUTH, IEEE80211_STA_ASSOC);
	wiphy_lock(hw->wiphy);
	{
		struct ieee80211_bss_conf *bc = &vif->bss_conf;
		u64 ch = BSS_CHANGED_BSSID | BSS_CHANGED_BEACON_INT | BSS_CHANGED_BASIC_RATES | BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_ERP_PREAMBLE |
			 BSS_CHANGED_ERP_SLOT | BSS_CHANGED_HT | BSS_CHANGED_QOS | BSS_CHANGED_HE_OBSS_PD | BSS_CHANGED_HE_BSS_COLOR | BSS_CHANGED_BANDWIDTH;
		m->ops->link_info_changed(hw, vif, bc, ch);
		m->ops->vif_cfg_changed(hw, vif, BSS_CHANGED_ASSOC | BSS_CHANGED_IDLE | BSS_CHANGED_SSID);
		/* default WMM EDCA parameters (the AP's WMM parameter element is not parsed yet) */
		static const struct ieee80211_tx_queue_params qp[4] = {
			[IEEE80211_AC_VO] = { .txop = 47, .cw_min = 3, .cw_max = 7, .aifs = 2, .acm = false },
			[IEEE80211_AC_VI] = { .txop = 94, .cw_min = 7, .cw_max = 15, .aifs = 2, .acm = false },
			[IEEE80211_AC_BE] = { .txop = 0, .cw_min = 15, .cw_max = 1023, .aifs = 3, .acm = false },
			[IEEE80211_AC_BK] = { .txop = 0, .cw_min = 15, .cw_max = 1023, .aifs = 7, .acm = false },
		};
		for (int ac = 0; ac < 4; ac++)
			m->ops->conf_tx(hw, vif, 0, ac, &qp[ac]);
		if (m->ops->link_sta_rc_update)
			m->ops->link_sta_rc_update(hw, vif, &sta->deflink, IEEE80211_RC_BW_CHANGED | IEEE80211_RC_SMPS_CHANGED | IEEE80211_RC_SUPP_RATES_CHANGED | IEEE80211_RC_NSS_CHANGED);
	}
	wiphy_unlock(hw->wiphy);

	if (!secure) {
		mlme_set_authorized();
	} else {
		if (!wait_cpl(&g_mlme.eapol_done, 12000) || g_mlme.deauthed || !g_mlme.connected) {
			fprintf(stderr, "[mlme] 4-way handshake failed or timed out\n");
			supplicant_stop();
			send_deauth(WLAN_REASON_DEAUTH_LEAVING);
			mlme_disconnect();
			return -EACCES;
		}
	}
	return 0;

fail:
	send_deauth(WLAN_REASON_DEAUTH_LEAVING);
	mlme_disconnect();
	return -ECONNREFUSED;
}

void mlme_disconnect(void)
{
	/* tell the AP we are leaving (best effort; with PMF an unprotected deauth is ignored, which is fine) */
	if (g_mlme.state >= MLME_ASSOCIATED && g_mlme.sta && !g_mlme.deauthed) { send_deauth(WLAN_REASON_DEAUTH_LEAVING); usleep(60000); }
	supplicant_stop();
	dp_remove_keys_if_any();
	struct mm *m = g_mm;
	struct ieee80211_vif *vif = m->vif;
	struct ieee80211_sta *sta = g_mlme.sta;
	g_mlme.connected = false;
	if (g_mlme.state >= MLME_AUTH && sta) {
		if (g_mlme.state >= MLME_ASSOCIATED) {
			vif->cfg.assoc = false;
			sta_set_state(sta, IEEE80211_STA_AUTHORIZED, IEEE80211_STA_ASSOC);
			sta_set_state(sta, IEEE80211_STA_ASSOC, IEEE80211_STA_AUTH);
			wiphy_lock(m->hw.wiphy);
			m->ops->vif_cfg_changed(&m->hw, vif, BSS_CHANGED_ASSOC);
			wiphy_unlock(m->hw.wiphy);
		}
		sta_set_state(sta, IEEE80211_STA_AUTH, IEEE80211_STA_NONE);
		sta_set_state(sta, IEEE80211_STA_NONE, IEEE80211_STA_NOTEXIST);
		struct mm_sta *s, *n;
		list_for_each_entry_safe(s, n, &m->stas, list)
			if (s->sta == sta) { list_del(&s->list); free(s); }
		g_mlme.sta = NULL;
	}
	teardown_chanctx();
	memset(g_bssid, 0, 6);
	g_mlme.state = MLME_IDLE;
}

static void dp_remove_keys_if_any(void)
{
	if (dp_ptk || dp_gtk[0] || dp_gtk[1] || dp_gtk[2] || dp_gtk[3])
		dp_remove_keys();
}

int mlme_cur_info(char *bssid, size_t n, int *chan, int *signal)
{
	int r = -1;
	pthread_mutex_lock(&bss_lock);
	for (int i = 0; i < bss_n; i++)
		if (!memcmp(bss_tab[i].bssid, g_bssid, 6)) {
			snprintf(bssid, n, "%02x:%02x:%02x:%02x:%02x:%02x", g_bssid[0], g_bssid[1], g_bssid[2], g_bssid[3], g_bssid[4], g_bssid[5]);
			*chan = bss_tab[i].chan; *signal = bss_tab[i].signal; r = 0; break;
		}
	pthread_mutex_unlock(&bss_lock);
	return r;
}

/* among the SSIDs in ssids[0..n), return in *idx the index of the one with the strongest visible BSS, or -1 */
void mlme_best_known(const char **ssids, int n, int *idx)
{
	int best = -1, best_sig = -1000;
	pthread_mutex_lock(&bss_lock);
	for (int i = 0; i < n; i++)
		for (int j = 0; j < bss_n; j++)
			if (bss_tab[j].ssid_len == strlen(ssids[i]) && !memcmp(bss_tab[j].ssid, ssids[i], bss_tab[j].ssid_len) && bss_tab[j].signal > best_sig) {
				best_sig = bss_tab[j].signal; best = i;
			}
	pthread_mutex_unlock(&bss_lock);
	*idx = best;
}

void mlme_rx_rate_str(char *buf, size_t n)
{
	static const char *enc[] = { "legacy", "HT", "VHT", "HE", "EHT" };
	static const char *bw[] = { "20", "40", "80", "160", "320", "HE-RU" };
	if (!g_mlme.rx_rate.n) { snprintf(buf, n, "-"); return; }
	unsigned e = g_mlme.rx_rate.enc, b = g_mlme.rx_rate.bw;
	if (e == RX_ENC_LEGACY) snprintf(buf, n, "legacy idx %u", g_mlme.rx_rate.idx);
	else snprintf(buf, n, "%s MCS%u NSS%u %sMHz", e < 5 ? enc[e] : "?", g_mlme.rx_rate.idx, g_mlme.rx_rate.nss, b < 6 ? bw[b] : "?");
}

/* ---- counters ---- */
unsigned long st_rx_frames, st_rx_data, st_rx_bytes, st_rx_undecrypted, st_rx_nodecrypt_drop, st_rx_undec_group, st_rx_undec_uni;

/* ---------------- Block Ack (A-MPDU) ---------------- */
static bool tx_ba_pending[16], tx_ba_ready_[16];
static u8 ba_token;
bool mlme_tx_ba_ready(int tid) { return tid >= 0 && tid < 16 && tx_ba_ready_[tid]; }

static void ampdu_op(enum ieee80211_ampdu_mlme_action act, u16 tid, u16 ssn, u16 buf, u16 timeout, bool amsdu)
{
	struct mm *m = g_mm;
	struct ieee80211_ampdu_params p = { .action = act, .sta = g_mlme.sta, .tid = tid, .ssn = ssn, .buf_size = buf, .amsdu = amsdu, .timeout = timeout };
	wiphy_lock(m->hw.wiphy);
	int r = m->ops->ampdu_action(&m->hw, m->vif, &p);
	wiphy_unlock(m->hw.wiphy);
	if (r < 0) fprintf(stderr, "[ba] ampdu_action %d tid %u failed: %d\n", act, tid, r);
}

static void send_addba_resp(u8 token, u8 tid, u16 buf, u16 timeout, bool amsdu, u16 status)
{
	struct sk_buff *skb = mgmt_alloc(64);
	struct ieee80211_mgmt *m = skb_put_zero(skb, offsetofend(struct ieee80211_mgmt, u.action.addba_resp.timeout));
	mgmt_hdr(m, IEEE80211_STYPE_ACTION);
	m->u.action.category = WLAN_CATEGORY_BACK;
	m->u.action.action_code = WLAN_ACTION_ADDBA_RESP;
	m->u.action.addba_resp.dialog_token = token;
	m->u.action.addba_resp.status = cpu_to_le16(status);
	m->u.action.addba_resp.capab = cpu_to_le16((amsdu ? 1 : 0) | IEEE80211_ADDBA_PARAM_POLICY_MASK | (tid << 2) | (buf << 6));
	m->u.action.addba_resp.timeout = cpu_to_le16(timeout);
	mlme_tx_mgmt(skb);
}

static void send_addba_req(u8 tid, u16 buf, u16 ssn)
{
	struct sk_buff *skb = mgmt_alloc(64);
	struct ieee80211_mgmt *m = skb_put_zero(skb, offsetofend(struct ieee80211_mgmt, u.action.addba_req.start_seq_num));
	mgmt_hdr(m, IEEE80211_STYPE_ACTION);
	m->u.action.category = WLAN_CATEGORY_BACK;
	m->u.action.action_code = WLAN_ACTION_ADDBA_REQ;
	m->u.action.addba_req.dialog_token = ++ba_token ? ba_token : ++ba_token;
	m->u.action.addba_req.capab = cpu_to_le16(IEEE80211_ADDBA_PARAM_POLICY_MASK | (tid << 2) | (buf << 6));
	m->u.action.addba_req.timeout = 0;
	m->u.action.addba_req.start_seq_num = cpu_to_le16(ssn << 4);
	mlme_tx_mgmt(skb);
}

static void send_delba(u8 tid, bool initiator, u16 reason)
{
	struct sk_buff *skb = mgmt_alloc(64);
	struct ieee80211_mgmt *m = skb_put_zero(skb, offsetofend(struct ieee80211_mgmt, u.action.delba.reason_code));
	mgmt_hdr(m, IEEE80211_STYPE_ACTION);
	m->u.action.category = WLAN_CATEGORY_BACK;
	m->u.action.action_code = WLAN_ACTION_DELBA;
	m->u.action.delba.params = cpu_to_le16((initiator ? 1 << 11 : 0) | (tid << 12));
	m->u.action.delba.reason_code = cpu_to_le16(reason);
	mlme_tx_mgmt(skb);
}

/* start a TX aggregation session towards the AP for this TID (ADDBA request) */
void mlme_ba_start_tx(int tid)
{
	if (tid < 0 || tid > 7 || tx_ba_pending[tid] || tx_ba_ready_[tid] || !g_mlme.sta) return;
	struct ieee80211_hw *hw = &g_mm->hw;
	u16 buf = hw->max_tx_aggregation_subframes ? hw->max_tx_aggregation_subframes : 64;
	if (buf > 64) buf = 64;				/* HT/VHT immediate BA window */
	struct ieee80211_ampdu_params p = { .action = IEEE80211_AMPDU_TX_START, .sta = g_mlme.sta, .tid = tid, .ssn = 0, .buf_size = buf };
	wiphy_lock(hw->wiphy);
	int r = g_mm->ops->ampdu_action(hw, g_mm->vif, &p);
	wiphy_unlock(hw->wiphy);
	if (r < 0) { fprintf(stderr, "[ba] TX_START refused: %d\n", r); return; }
	tx_ba_pending[tid] = true;
	send_addba_req(tid, buf, 0);
}

void mlme_ba_reset(void)
{
	memset(tx_ba_pending, 0, sizeof tx_ba_pending);
	memset(tx_ba_ready_, 0, sizeof tx_ba_ready_);
}

static void handle_action(struct sk_buff *skb)
{
	struct ieee80211_mgmt *m = (void *)skb->data;
	if (skb->len < offsetofend(struct ieee80211_mgmt, u.action.action_code) || m->u.action.category != WLAN_CATEGORY_BACK) return;
	if (!g_mlme.connected) return;
	switch (m->u.action.action_code) {
	case WLAN_ACTION_ADDBA_REQ: {
		if (skb->len < offsetofend(struct ieee80211_mgmt, u.action.addba_req.start_seq_num)) return;
		u16 capab = le16_to_cpu(m->u.action.addba_req.capab);
		u8 tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;
		u16 buf = (capab & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK) >> 6;
		u16 timeout = le16_to_cpu(m->u.action.addba_req.timeout);
		u16 ssn = le16_to_cpu(m->u.action.addba_req.start_seq_num) >> 4;
		bool amsdu = capab & IEEE80211_ADDBA_PARAM_AMSDU_MASK;
		u16 maxbuf = g_mm->hw.max_rx_aggregation_subframes ? g_mm->hw.max_rx_aggregation_subframes : 64;
		if (!buf || buf > maxbuf) buf = maxbuf;
		if (tid > 7) { send_addba_resp(m->u.action.addba_req.dialog_token, tid, buf, timeout, false, WLAN_STATUS_REQUEST_DECLINED); return; }
		ampdu_op(IEEE80211_AMPDU_RX_START, tid, ssn, buf, timeout, amsdu);
		send_addba_resp(m->u.action.addba_req.dialog_token, tid, buf, timeout, amsdu, 0);
		fprintf(stderr, "[ba] RX aggregation started tid %u buf %u\n", tid, buf);
		break;
	}
	case WLAN_ACTION_ADDBA_RESP: {
		if (skb->len < offsetofend(struct ieee80211_mgmt, u.action.addba_resp.timeout)) return;
		u16 capab = le16_to_cpu(m->u.action.addba_resp.capab);
		u8 tid = (capab & IEEE80211_ADDBA_PARAM_TID_MASK) >> 2;
		u16 buf = (capab & IEEE80211_ADDBA_PARAM_BUF_SIZE_MASK) >> 6;
		if (tid > 7 || !tx_ba_pending[tid]) return;
		tx_ba_pending[tid] = false;
		if (le16_to_cpu(m->u.action.addba_resp.status)) {
			ampdu_op(IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0, 0, false);
			fprintf(stderr, "[ba] AP declined TX aggregation tid %u\n", tid);
			return;
		}
		if (!buf || buf > 64) buf = 64;
		ampdu_op(IEEE80211_AMPDU_TX_OPERATIONAL, tid, 0, buf, le16_to_cpu(m->u.action.addba_resp.timeout), capab & IEEE80211_ADDBA_PARAM_AMSDU_MASK);
		tx_ba_ready_[tid] = true;
		fprintf(stderr, "[ba] TX aggregation operational tid %u buf %u\n", tid, buf);
		break;
	}
	case WLAN_ACTION_DELBA: {
		u16 params = le16_to_cpu(m->u.action.delba.params);
		u8 tid = (params >> 12) & 0xf;
		bool initiator = params & (1 << 11);
		if (tid > 7) return;
		if (initiator) ampdu_op(IEEE80211_AMPDU_RX_STOP, tid, 0, 0, 0, false);
		else if (tx_ba_ready_[tid]) { tx_ba_ready_[tid] = false; ampdu_op(IEEE80211_AMPDU_TX_STOP_CONT, tid, 0, 0, 0, false); }
		fprintf(stderr, "[ba] DELBA tid %u (%s)\n", tid, initiator ? "rx" : "tx");
		break;
	}
	}
}

/* describe the RSN element of a scanned BSS: AKM suites, group/pairwise ciphers, MFP bits (for the `rsn` control command) */
void mlme_rsn_info(const char *ssid, char *out, size_t n)
{
	out[0] = 0;
	pthread_mutex_lock(&bss_lock);
	for (int i = 0; i < bss_n; i++) {
		struct bss_entry *b = &bss_tab[i];
		if (b->ssid_len != strlen(ssid) || memcmp(b->ssid, ssid, b->ssid_len)) continue;
		const u8 *rsn = ie_find(b->ies, b->ie_len, WLAN_EID_RSN);
		const u8 *rsnx = ie_find(b->ies, b->ie_len, 244);
		char line[200] = "";
		if (rsn && rsn[1] >= 8) {
			const u8 *p = rsn + 2 + 2 + 4;			/* skip version + group cipher */
			int npair = p[0] | (p[1] << 8); p += 2 + 4 * npair;
			int nakm = p[0] | (p[1] << 8); p += 2;
			char akms[80] = "";
			for (int k = 0; k < nakm && k < 6; k++) { char t[12]; snprintf(t, sizeof t, "%d ", p[4 * k + 3]); strlcat(akms, t, sizeof akms); }
			p += 4 * nakm;
			unsigned caps = p[0] | (p[1] << 8);
			snprintf(line, sizeof line, "%02x:%02x:%02x:%02x:%02x:%02x ch%d AKM[%s] MFPC=%u MFPR=%u RSNXE=%s\n", b->bssid[0], b->bssid[1], b->bssid[2], b->bssid[3], b->bssid[4], b->bssid[5], b->chan, akms, !!(caps & 0x80), !!(caps & 0x40), rsnx ? "yes" : "no");
		}
		strlcat(out, line, n);
	}
	pthread_mutex_unlock(&bss_lock);
}
