/* SPDX-License-Identifier: BSD-3-Clause
 * Data exchanged between rtw89d (user space) and RTW89Airport.kext through IORegistry properties.
 *   kext -> daemon: "RTW89ScanSeq" (number, incremented for every scan request from the system)
 *   daemon -> kext: "RTW89ScanResults" (data: array of rtw_scan_ent), then "RTW89ScanDone" (true)
 */
#ifndef RTW89_AIRPORT_ABI_H
#define RTW89_AIRPORT_ABI_H
#include <stdint.h>

#define RTW_AP_MAX_SCAN 64
#define RTW_AP_MAX_IE 320

struct rtw_scan_ent {
	uint8_t bssid[6];
	uint8_t ssid_len;
	uint8_t ssid[32];
	uint16_t channel;
	uint16_t band5;		/* 1 when the channel is in the 5 GHz band */
	int16_t rssi;		/* dBm */
	int16_t noise;		/* dBm */
	uint16_t cap;
	uint16_t beacon_int;
	uint16_t ie_len;
	uint8_t ie[RTW_AP_MAX_IE];	/* RSN element (id 48) when the network has one */
} __attribute__((packed));


/* kext -> daemon: "RTW89AssocReq" (data: rtw_assoc_req) with "RTW89AssocSeq"; "RTW89DisassocSeq" (number)
 * daemon -> kext: "RTW89Link" (data: rtw_link) */
struct rtw_assoc_req {
	uint32_t seq;
	uint8_t ssid_len;
	uint8_t ssid[32];
	uint8_t bssid[6];
	uint16_t auth_lower, auth_upper;
	uint16_t key_len;
	uint8_t key[64];
	uint16_t rsn_len;
	uint8_t rsn[64];
} __attribute__((packed));

struct rtw_link {
	uint8_t up;
	uint8_t ssid_len;
	uint8_t ssid[32];
	uint8_t bssid[6];
	uint16_t channel;
	uint16_t band5;
	int16_t rssi;
} __attribute__((packed));

#endif
