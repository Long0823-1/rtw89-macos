# Third-party code and licences

* **rtw89/src/** -- unmodified copy of the Linux kernel's `drivers/net/wireless/realtek/rtw89` (GPL-2.0 OR BSD-3-Clause,
  (c) Realtek Corporation and contributors).  A tiny build-time patch lives in `port/patches/`.
* **port/compat/** -- `kcompat.h`, `kcompat2.h` and the stub `linux/*.h` files are ours.  The following are verbatim copies of
  Linux kernel headers (GPL-2.0 or ISC where marked in the files): `linux/ieee80211*.h`, `linux/nl80211.h`, `linux/average.h`,
  `net/mac80211.h`, `net/cfg80211.h`, `net/regulatory.h`, `net/codel.h`, `net/fq*.h`, `net/ieee80211_radiotap.h`, `uapi/linux/nl80211.h`.
* **Firmware** (`rtw8852b_fw*.bin`) is Realtek's, distributed through linux-firmware under its own licence; it is **not** stored here
  -- run `scripts/fetch-firmware.sh`.
* `host/crypto.c` uses Apple's CommonCrypto; `host/ecp256.c`, `host/sae.c` are our own implementations.

The daemon links the GPL kernel code and headers, so the combined binary must be treated as **GPL-2.0**; see `COPYING`.
Our own sources (`port/host/*`, `app/`, `install/`, `tools/`) may also be used under BSD-3-Clause.
