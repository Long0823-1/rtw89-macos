/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Minimal mac80211 emulation core: shared definitions between mm_core.c / mlme.c / net.c */
#ifndef MM_H
#define MM_H
#include <kcompat.h>
#include <net/mac80211.h>

struct mm_key {
	struct list_head list;
	struct ieee80211_key_conf *conf;
	struct ieee80211_sta *sta;
};

struct mm_sta {
	struct list_head list;
	struct ieee80211_sta *sta;
};

struct mm {
	struct ieee80211_hw hw;
	const struct ieee80211_ops *ops;
	struct ieee80211_vif *vif;
	struct list_head stas;
	struct list_head keys;
	bool registered;
	bool started;
	/* rx path */
	struct sk_buff_head rxq;
	pthread_t rx_thr;
	pthread_mutex_t rx_lock;
	pthread_cond_t rx_cond;
	/* scan */
	struct completion scan_done;
	bool scan_running;
	/* chanctx */
	struct ieee80211_chanctx_conf *chanctx;
};

extern struct mm *g_mm;

/* hooks implemented by mlme.c */
void mlme_rx(struct sk_buff *skb);			/* 802.11 frame from driver, FCS stripped, rx status in cb */
void mlme_tx_status(struct sk_buff *skb, bool acked);
void mlme_scan_done(bool aborted);

/* mm_core API for the host program */
int mm_up(void);					/* ops->start + add interface */
void mm_down(void);
int mm_tx(struct sk_buff *skb, struct ieee80211_sta *sta);	/* hand a fully built 802.11 frame to the driver */
int mm_hw_scan(const u8 *ssid, int ssid_len);
int ieee80211_channel_khz_to_idx(u32 khz);
struct ieee80211_channel *mm_find_channel(u32 freq_mhz);
#endif
