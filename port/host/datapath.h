#ifndef DATAPATH_H
#define DATAPATH_H
#include "mm.h"
extern struct ieee80211_key_conf *dp_ptk;
extern struct ieee80211_key_conf *dp_gtk[4];
int dp_install_key(const u8 *key, int keylen, int keyidx, bool pairwise, struct ieee80211_sta *sta);
void dp_remove_keys(void);
int dp_install_igtk(const u8 *key, int keyidx);
int dp_tx_frame(const u8 *da, u16 ethertype, const u8 *payload, size_t len, bool control_port);
#endif
