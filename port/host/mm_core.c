// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Minimal mac80211/cfg80211 emulation: the subset of kernel symbols rtw89 imports, plus device bring-up.
#include "mm.h"

struct mm *g_mm;

/* ---------------- wiphy ---------------- */
struct ieee80211_hw *wiphy_to_ieee80211_hw(struct wiphy *wiphy) { return &g_mm->hw; }
int regulatory_hint(struct wiphy *wiphy, const char *alpha2)
{
	if (wiphy->reg_notifier) {
		struct regulatory_request req = { .initiator = NL80211_REGDOM_SET_BY_DRIVER, .alpha2 = { alpha2[0], alpha2[1] }, .dfs_region = NL80211_DFS_UNSET };
		wiphy->reg_notifier(wiphy, &req);
	}
	return 0;
}
void wiphy_rfkill_start_polling(struct wiphy *wiphy) {}
void wiphy_rfkill_set_hw_state_reason(struct wiphy *wiphy, bool blocked, enum rfkill_hard_block_reasons reason) {}

/* wiphy works run on one dedicated thread, holding the wiphy mutex (like cfg80211's wiphy_work). */
static struct list_head wiphy_works = LIST_HEAD_INIT(wiphy_works);
static pthread_mutex_t ww_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ww_cond = PTHREAD_COND_INITIALIZER;
static pthread_t ww_thr;
static pthread_once_t ww_once = PTHREAD_ONCE_INIT;
static struct wiphy_work *ww_running;
static struct wiphy *ww_wiphy;

static void *ww_thread(void *arg)
{
	pthread_mutex_lock(&ww_lock);
	for (;;) {
		while (list_empty(&wiphy_works))
			pthread_cond_wait(&ww_cond, &ww_lock);
		struct wiphy_work *w = list_first_entry(&wiphy_works, struct wiphy_work, entry);
		list_del_init(&w->entry);
		ww_running = w;
		pthread_mutex_unlock(&ww_lock);
		wiphy_lock(ww_wiphy);
		w->func(ww_wiphy, w);
		wiphy_unlock(ww_wiphy);
		pthread_mutex_lock(&ww_lock);
		ww_running = NULL;
		pthread_cond_broadcast(&ww_cond);
	}
	return NULL;
}

static void ww_start(void) { pthread_create(&ww_thr, NULL, ww_thread, NULL); }

void wiphy_work_queue(struct wiphy *wiphy, struct wiphy_work *work)
{
	extern void kc_runtime_init(void);
	kc_runtime_init();
	pthread_once(&ww_once, ww_start);
	ww_wiphy = wiphy;
	pthread_mutex_lock(&ww_lock);
	if (list_empty(&work->entry)) {
		list_add_tail(&work->entry, &wiphy_works);
		pthread_cond_signal(&ww_cond);
	}
	pthread_mutex_unlock(&ww_lock);
}

void wiphy_work_cancel(struct wiphy *wiphy, struct wiphy_work *work)
{
	pthread_mutex_lock(&ww_lock);
	if (!list_empty(&work->entry))
		list_del_init(&work->entry);
	pthread_mutex_unlock(&ww_lock);
}

void wiphy_work_flush(struct wiphy *wiphy, struct wiphy_work *work)
{
	pthread_mutex_lock(&ww_lock);
	while (!list_empty(&work->entry) || (ww_running == work && pthread_self() != ww_thr))
		pthread_cond_wait(&ww_cond, &ww_lock);
	pthread_mutex_unlock(&ww_lock);
}

void wiphy_delayed_work_timer(struct timer_list *t)
{
	struct wiphy_delayed_work *dwork = from_timer(dwork, t, timer);
	wiphy_work_queue(dwork->wiphy, &dwork->work);
}

void wiphy_delayed_work_queue(struct wiphy *wiphy, struct wiphy_delayed_work *dwork, unsigned long delay)
{
	if (!delay) {
		del_timer(&dwork->timer);
		wiphy_work_queue(wiphy, &dwork->work);
		return;
	}
	dwork->wiphy = wiphy;
	mod_timer(&dwork->timer, jiffies + delay);
}

void wiphy_delayed_work_cancel(struct wiphy *wiphy, struct wiphy_delayed_work *dwork)
{
	del_timer_sync(&dwork->timer);
	wiphy_work_cancel(wiphy, &dwork->work);
}

void wiphy_delayed_work_flush(struct wiphy *wiphy, struct wiphy_delayed_work *dwork)
{
	if (del_timer_sync(&dwork->timer))
		wiphy_work_queue(wiphy, &dwork->work);
	wiphy_work_flush(wiphy, &dwork->work);
}

/* ---------------- hw alloc/register ---------------- */
struct ieee80211_hw *ieee80211_alloc_hw_nm(size_t priv_data_len, const struct ieee80211_ops *ops, const char *requested_name)
{
	size_t priv_off = ALIGN(sizeof(struct mm), 64);
	struct mm *m = calloc(1, priv_off + priv_data_len);
	struct wiphy *wiphy = calloc(1, sizeof(struct wiphy) + 256);
	if (!m || !wiphy) return NULL;
	kc_mutex_init(&wiphy->mtx.m);
	INIT_LIST_HEAD(&m->stas);
	INIT_LIST_HEAD(&m->keys);
	skb_queue_head_init(&m->rxq);
	pthread_mutex_init(&m->rx_lock, NULL);
	pthread_cond_init(&m->rx_cond, NULL);
	init_completion(&m->scan_done);
	m->ops = ops;
	m->hw.wiphy = wiphy;
	m->hw.priv = (u8 *)m + priv_off;
	m->hw.queues = 4;
	g_mm = m;
	return &m->hw;
}

int ieee80211_register_hw(struct ieee80211_hw *hw)
{
	struct wiphy *wiphy = hw->wiphy;
	g_mm->registered = true;
	/* cfg80211 notifies drivers of the initial (world, "00") regulatory domain right after registration */
	if (wiphy->reg_notifier) {
		struct regulatory_request req = { .initiator = NL80211_REGDOM_SET_BY_CORE, .alpha2 = {'0', '0'}, .dfs_region = NL80211_DFS_UNSET };
		wiphy->reg_notifier(wiphy, &req);
	}
	return 0;
}
void ieee80211_unregister_hw(struct ieee80211_hw *hw) { g_mm->registered = false; }
void ieee80211_free_hw(struct ieee80211_hw *hw) {}
void ieee80211_restart_hw(struct ieee80211_hw *hw) { fprintf(stderr, "[mm] ieee80211_restart_hw requested (ignored)\n"); }

void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work) { queue_work(NULL, work); }
void ieee80211_queue_delayed_work(struct ieee80211_hw *hw, struct delayed_work *dwork, unsigned long delay) { queue_delayed_work(NULL, dwork, delay); }

/* ---------------- frequency helpers ---------------- */
int ieee80211_freq_khz_to_channel(u32 freq)
{
	freq /= 1000;
	if (freq == 2484) return 14;
	if (freq < 2484) return (freq - 2407) / 5;
	if (freq >= 4910 && freq <= 4980) return (freq - 4000) / 5;
	if (freq < 5925) return (freq - 5000) / 5;
	if (freq == 5935) return 2;
	if (freq <= 45000) return (freq - 5950) / 5;
	return 0;
}

u32 ieee80211_channel_to_freq_khz(int chan, enum nl80211_band band)
{
	switch (band) {
	case NL80211_BAND_2GHZ: return (chan == 14 ? 2484 : 2407 + chan * 5) * 1000;
	case NL80211_BAND_5GHZ: return (5000 + chan * 5) * 1000;
	case NL80211_BAND_6GHZ: return chan == 2 ? 5935000 : (5950 + chan * 5) * 1000;
	default: return 0;
	}
}

int cfg80211_get_ies_channel_number(const u8 *ie, size_t ielen, enum nl80211_band band)
{
	const u8 *p = ie, *end = ie + ielen;
	while (p + 2 <= end && p + 2 + p[1] <= end) {
		if (p[0] == 3 && p[1] >= 1) return p[2];			/* DS parameter set */
		if (p[0] == 61 && p[1] >= 1) return p[2];			/* HT operation: primary channel */
		p += 2 + p[1];
	}
	return -1;
}

const struct element *cfg80211_find_elem_match(u8 eid, const u8 *ies, unsigned int len, const u8 *match, unsigned int match_len, unsigned int match_offset)
{
	const u8 *p = ies, *end = ies + len;
	while (p + 2 <= end && p + 2 + p[1] <= end) {
		if (p[0] == eid) {
			if (!match || (p[1] >= match_offset + match_len && !memcmp(p + 2 + match_offset, match, match_len)))
				return (const struct element *)p;
		}
		p += 2 + p[1];
	}
	return NULL;
}

unsigned int ieee80211_hdrlen(__le16 fc)
{
	unsigned int hdrlen = 24;
	if (ieee80211_is_ext(fc)) { hdrlen = 24; goto out; }
	if (ieee80211_is_data(fc)) {
		if (ieee80211_has_a4(fc)) hdrlen = 30;
		if (ieee80211_is_data_qos(fc)) { hdrlen += 2; if (ieee80211_has_order(fc)) hdrlen += 4; }
		goto out;
	}
	if (ieee80211_is_mgmt(fc)) { if (ieee80211_has_order(fc)) hdrlen += 4; goto out; }
	if (ieee80211_is_ctl(fc)) {
		if (ieee80211_is_pspoll(fc)) { hdrlen = 16; goto out; }
		if (ieee80211_is_back_req(fc)) { hdrlen = 16; goto out; }
		hdrlen = 10;
	}
out:
	return hdrlen;
}

u32 cfg80211_calculate_bitrate(struct rate_info *rate)
{
	/* only legacy rates matter for our own logging; HT/VHT/HE approximated by mcs */
	if (!(rate->flags & (RATE_INFO_FLAGS_MCS | RATE_INFO_FLAGS_VHT_MCS | RATE_INFO_FLAGS_HE_MCS))) return rate->legacy;
	return 0;
}

void cfg80211_chandef_create(struct cfg80211_chan_def *chandef, struct ieee80211_channel *chan, enum nl80211_channel_type chantype)
{
	memset(chandef, 0, sizeof(*chandef));
	chandef->chan = chan;
	chandef->center_freq1 = chan->center_freq;
	chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
	switch (chantype) {
	case NL80211_CHAN_NO_HT: break;
	case NL80211_CHAN_HT20: chandef->width = NL80211_CHAN_WIDTH_20; break;
	case NL80211_CHAN_HT40PLUS: chandef->width = NL80211_CHAN_WIDTH_40; chandef->center_freq1 += 10; break;
	case NL80211_CHAN_HT40MINUS: chandef->width = NL80211_CHAN_WIDTH_40; chandef->center_freq1 -= 10; break;
	}
}

void cfg80211_bss_iter(struct wiphy *wiphy, struct cfg80211_chan_def *chandef,
		       void (*iter)(struct wiphy *, struct cfg80211_bss *, void *), void *iter_data) {}

/* ---------------- chanctx emulation helpers ---------------- */
int ieee80211_emulate_add_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx)
{
	hw->conf.radar_enabled = ctx->radar_enabled;
	hw->conf.chandef = ctx->def;
	return g_mm->ops->config(hw, -1, IEEE80211_CONF_CHANGE_CHANNEL);
}
void ieee80211_emulate_remove_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx) {}
void ieee80211_emulate_change_chanctx(struct ieee80211_hw *hw, struct ieee80211_chanctx_conf *ctx, u32 changed)
{
	hw->conf.radar_enabled = ctx->radar_enabled;
	hw->conf.chandef = ctx->def;
	g_mm->ops->config(hw, -1, IEEE80211_CONF_CHANGE_CHANNEL);
}
int ieee80211_emulate_switch_vif_chanctx(struct ieee80211_hw *hw, struct ieee80211_vif_chanctx_switch *vifs, int n_vifs, enum ieee80211_chanctx_switch_mode mode)
{
	if (n_vifs > 0) { hw->conf.chandef = vifs[0].new_ctx->def; g_mm->ops->config(hw, -1, IEEE80211_CONF_CHANGE_CHANNEL); }
	return 0;
}

/* ---------------- stubs for unused paths ---------------- */
void ieee80211_wake_queues(struct ieee80211_hw *hw) {}
void ieee80211_stop_queues(struct ieee80211_hw *hw) {}
void ieee80211_txq_schedule_start(struct ieee80211_hw *hw, u8 ac) {}
struct ieee80211_txq *ieee80211_next_txq(struct ieee80211_hw *hw, u8 ac) { return NULL; }
void __ieee80211_schedule_txq(struct ieee80211_hw *hw, struct ieee80211_txq *txq, bool force) {}
void ieee80211_txq_get_depth(struct ieee80211_txq *txq, unsigned long *frame_cnt, unsigned long *byte_cnt) { if (frame_cnt) *frame_cnt = 0; if (byte_cnt) *byte_cnt = 0; }
struct sk_buff *ieee80211_tx_dequeue(struct ieee80211_hw *hw, struct ieee80211_txq *txq) { return NULL; }
int ieee80211_start_tx_ba_session(struct ieee80211_sta *sta, u16 tid, u16 timeout) { return -EOPNOTSUPP; }
int ieee80211_stop_tx_ba_session(struct ieee80211_sta *sta, u16 tid) { return -EOPNOTSUPP; }
void ieee80211_stop_tx_ba_cb_irqsafe(struct ieee80211_vif *vif, const u8 *ra, u16 tid) {}
void ieee80211_sta_uapsd_trigger(struct ieee80211_sta *sta, u8 tid) {}
void ieee80211_sta_pspoll(struct ieee80211_sta *sta) {}
int ieee80211_sta_ps_transition(struct ieee80211_sta *sta, bool start) { return 0; }
void ieee80211_sta_recalc_aggregates(struct ieee80211_sta *sta) {}
int ieee80211_set_active_links(struct ieee80211_vif *vif, u16 active_links) { return 0; }
void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw) {}
void ieee80211_ready_on_channel(struct ieee80211_hw *hw) {}
void ieee80211_csa_finish(struct ieee80211_vif *vif, unsigned int link_id) {}
bool ieee80211_beacon_cntdwn_is_complete(struct ieee80211_vif *vif, unsigned int link_id) { return true; }
void ieee80211_cqm_rssi_notify(struct ieee80211_vif *vif, enum nl80211_cqm_rssi_threshold_event rssi_event, s32 rssi_level, gfp_t gfp) {}
void ieee80211_beacon_loss(struct ieee80211_vif *vif) { fprintf(stderr, "[mm] beacon loss\n"); }
void ieee80211_connection_loss(struct ieee80211_vif *vif) { fprintf(stderr, "[mm] connection loss\n"); }
struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw, struct ieee80211_vif *vif, u16 *tim_offset, u16 *tim_length, unsigned int link_id) { return NULL; }
struct sk_buff *ieee80211_pspoll_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif) { return NULL; }

void ieee80211_free_txskb(struct ieee80211_hw *hw, struct sk_buff *skb) { kfree_skb(skb); }

/* null function frame (used by rtw89 for ROC / scan keepalive) */
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw, struct ieee80211_vif *vif, int link_id, bool qos_ok)
{
	struct sk_buff *skb = alloc_skb(hw->extra_tx_headroom + 32, GFP_ATOMIC);
	struct ieee80211_hdr_3addr *h;
	if (!skb) return NULL;
	skb_reserve(skb, hw->extra_tx_headroom);
	h = skb_put_zero(skb, 24);
	h->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC | IEEE80211_FCTL_TODS);
	memcpy(h->addr1, vif->bss_conf.bssid, 6); memcpy(h->addr2, vif->addr, 6); memcpy(h->addr3, vif->bss_conf.bssid, 6);
	return skb;
}

struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw, const u8 *src_addr, const u8 *ssid, size_t ssid_len, size_t tailroom)
{
	struct sk_buff *skb = alloc_skb(hw->extra_tx_headroom + sizeof(struct ieee80211_hdr_3addr) + 2 + ssid_len + tailroom, GFP_KERNEL);
	struct ieee80211_hdr_3addr *h;
	u8 *pos;
	if (!skb) return NULL;
	skb_reserve(skb, hw->extra_tx_headroom);
	h = skb_put_zero(skb, sizeof(*h));
	h->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ);
	eth_broadcast_addr(h->addr1);
	memcpy(h->addr2, src_addr, 6);
	eth_broadcast_addr(h->addr3);
	pos = skb_put(skb, 2 + ssid_len);
	*pos++ = WLAN_EID_SSID;
	*pos++ = ssid_len;
	if (ssid_len) memcpy(pos, ssid, ssid_len);
	return skb;
}

/* ---------------- iterators ---------------- */
void ieee80211_iterate_active_interfaces_atomic(struct ieee80211_hw *hw, u32 iter_flags,
						void (*iterator)(void *data, u8 *mac, struct ieee80211_vif *vif), void *data)
{
	if (g_mm->vif) iterator(data, g_mm->vif->addr, g_mm->vif);
}

void ieee80211_iterate_stations_atomic(struct ieee80211_hw *hw, void (*iterator)(void *data, struct ieee80211_sta *sta), void *data)
{
	struct mm_sta *s, *n;
	list_for_each_entry_safe(s, n, &g_mm->stas, list)
		iterator(data, s->sta);
}

struct ieee80211_sta *__ieee80211_iterate_stations(struct ieee80211_hw *hw, struct ieee80211_sta *prev)
{
	struct mm_sta *s;
	bool take = (prev == NULL);
	list_for_each_entry(s, &g_mm->stas, list) {
		if (take) return s->sta;
		if (s->sta == prev) take = true;
	}
	return NULL;
}

void ieee80211_iter_keys_rcu(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     void (*iter)(struct ieee80211_hw *hw, struct ieee80211_vif *vif, struct ieee80211_sta *sta, struct ieee80211_key_conf *key, void *data), void *iter_data)
{
	struct mm_key *k, *n;
	list_for_each_entry_safe(k, n, &g_mm->keys, list)
		iter(hw, vif, k->sta, k->conf, iter_data);
}

struct ieee80211_sta *ieee80211_find_sta(struct ieee80211_vif *vif, const u8 *addr)
{
	struct mm_sta *s;
	list_for_each_entry(s, &g_mm->stas, list)
		if (!memcmp(s->sta->addr, addr, 6)) return s->sta;
	return NULL;
}

/* ---------------- rx / tx status / scan completion ---------------- */
void ieee80211_rx_napi(struct ieee80211_hw *hw, struct ieee80211_sta *sta, struct sk_buff *skb, struct napi_struct *napi)
{
	struct ieee80211_rx_status *st = IEEE80211_SKB_RXCB(skb);
	if (st->flag & RX_FLAG_FAILED_FCS_CRC) { kfree_skb(skb); return; }
	if (ieee80211_hw_check(hw, RX_INCLUDES_FCS) && skb->len >= 4)
		skb_trim(skb, skb->len - 4);
	skb_queue_tail(&g_mm->rxq, skb);
	pthread_mutex_lock(&g_mm->rx_lock);
	pthread_cond_signal(&g_mm->rx_cond);
	pthread_mutex_unlock(&g_mm->rx_lock);
}

void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool acked = info->flags & IEEE80211_TX_STAT_ACK;
	mlme_tx_status(skb, acked);
	kfree_skb(skb);
}

void ieee80211_scan_completed(struct ieee80211_hw *hw, struct cfg80211_scan_info *info)
{
	g_mm->scan_running = false;
	mlme_scan_done(info ? info->aborted : false);
	complete_all(&g_mm->scan_done);
}

static void *rx_thread(void *arg)
{
	for (;;) {
		struct sk_buff *skb = skb_dequeue(&g_mm->rxq);
		if (!skb) {
			pthread_mutex_lock(&g_mm->rx_lock);
			if (skb_queue_empty(&g_mm->rxq)) {
				struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 100000000L;
				if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
				pthread_cond_timedwait(&g_mm->rx_cond, &g_mm->rx_lock, &ts);
			}
			pthread_mutex_unlock(&g_mm->rx_lock);
			continue;
		}
		mlme_rx(skb);
	}
	return NULL;
}

/* ---------------- bring-up ---------------- */
struct ieee80211_channel *mm_find_channel(u32 freq_mhz)
{
	struct wiphy *w = g_mm->hw.wiphy;
	for (int b = 0; b < NUM_NL80211_BANDS; b++) {
		struct ieee80211_supported_band *sb = w->bands[b];
		if (!sb) continue;
		for (int i = 0; i < sb->n_channels; i++)
			if (sb->channels[i].center_freq == freq_mhz) return &sb->channels[i];
	}
	return NULL;
}

int mm_up(void)
{
	struct mm *m = g_mm;
	struct ieee80211_hw *hw = &m->hw;
	struct wiphy *wiphy = hw->wiphy;
	int ret;

	wiphy_lock(wiphy);
	ret = m->ops->start(hw);
	if (ret) { wiphy_unlock(wiphy); fprintf(stderr, "[mm] ops->start failed: %d\n", ret); return ret; }
	m->started = true;

	struct ieee80211_vif *vif = calloc(1, sizeof(*vif) + hw->vif_data_size + 64);
	vif->type = NL80211_IFTYPE_STATION;
	memcpy(vif->addr, wiphy->perm_addr, 6);
	vif->bss_conf.vif = vif;
	memcpy(vif->bss_conf.addr, vif->addr, 6);
	vif->bss_conf.link_id = 0;
	vif->bss_conf.beacon_int = 100;
	vif->bss_conf.dtim_period = 1;
	vif->link_conf[0] = &vif->bss_conf;
	vif->cab_queue = IEEE80211_INVAL_HW_QUEUE;
	for (int i = 0; i < IEEE80211_NUM_ACS; i++) vif->hw_queue[i] = i;
	vif->txq = calloc(1, sizeof(struct ieee80211_txq) + hw->txq_data_size + 16);
	vif->txq->vif = vif;
	vif->txq->tid = IEEE80211_NUM_TIDS;
	m->vif = vif;
	ret = m->ops->add_interface(hw, vif);
	if (ret) { wiphy_unlock(wiphy); fprintf(stderr, "[mm] add_interface failed: %d\n", ret); return ret; }
	{
		unsigned int flags = FIF_ALLMULTI;
		m->ops->configure_filter(hw, ~0u, &flags, 0);
	}
	hw->conf.flags &= ~IEEE80211_CONF_IDLE;
	m->ops->config(hw, -1, IEEE80211_CONF_CHANGE_IDLE);
	wiphy_unlock(wiphy);
	pthread_create(&m->rx_thr, NULL, rx_thread, NULL);
	return 0;
}

void mm_down(void)
{
	struct mm *m = g_mm;
	wiphy_lock(m->hw.wiphy);
	if (m->vif) m->ops->remove_interface(&m->hw, m->vif);
	if (m->started) m->ops->stop(&m->hw, false);
	wiphy_unlock(m->hw.wiphy);
}

u64 mm_stat_lock_ns, mm_stat_ops_ns, mm_stat_n;

int mm_tx(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct mm *m = g_mm;
	struct ieee80211_tx_control control = { .sta = sta };
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	info->control.vif = m->vif;
	u64 t0 = kc_now_ns();
	wiphy_lock(m->hw.wiphy);
	u64 t1 = kc_now_ns();
	m->ops->tx(&m->hw, &control, skb);
	u64 t2 = kc_now_ns();
	wiphy_unlock(m->hw.wiphy);
	mm_stat_lock_ns += t1 - t0; mm_stat_ops_ns += t2 - t1; mm_stat_n++;
	return 0;
}

int g_scan_only_chan;	/* 0 = every channel, otherwise only this channel number (quick directed scan) */

int mm_hw_scan(const u8 *ssid, int ssid_len)
{
	struct mm *m = g_mm;
	struct wiphy *wiphy = m->hw.wiphy;
	int n = 0;
	for (int b = 0; b < NUM_NL80211_BANDS; b++)
		if (wiphy->bands[b] && b != NL80211_BAND_6GHZ) n += wiphy->bands[b]->n_channels;
	struct ieee80211_scan_request *rq = calloc(1, sizeof(*rq) + n * sizeof(void *));
	static struct cfg80211_ssid ssids[1];
	int k = 0;
	for (int b = 0; b < NUM_NL80211_BANDS; b++) {
		struct ieee80211_supported_band *sb = wiphy->bands[b];
		if (!sb || b == NL80211_BAND_6GHZ) continue;
		for (int i = 0; i < sb->n_channels; i++) {
			if (sb->channels[i].flags & IEEE80211_CHAN_DISABLED) continue;
			if (g_scan_only_chan && sb->channels[i].hw_value != g_scan_only_chan) continue;
			rq->req.channels[k++] = &sb->channels[i];
		}
	}
	rq->req.n_channels = k;
	if (ssid_len) { memcpy(ssids[0].ssid, ssid, ssid_len); ssids[0].ssid_len = ssid_len; rq->req.ssids = ssids; rq->req.n_ssids = 1; }
	else { ssids[0].ssid_len = 0; rq->req.ssids = ssids; rq->req.n_ssids = 1; }	/* wildcard probe request: active scan, APs answer at once instead of waiting for a beacon */
	rq->req.wiphy = wiphy;
	memset(rq->req.rates, 0xff, sizeof(rq->req.rates));
	reinit_completion(&m->scan_done);
	m->scan_running = true;
	wiphy_lock(wiphy);
	int ret = m->ops->hw_scan(&m->hw, m->vif, rq);
	wiphy_unlock(wiphy);
	if (ret) { m->scan_running = false; return ret; }
	return 0;
}

void ieee80211_purge_tx_queue(struct ieee80211_hw *hw, struct sk_buff_head *skbs)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(skbs)))
		kfree_skb(skb);
}
