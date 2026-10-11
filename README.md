# rtw89-macos — TP-Link Archer TX20U Nano (RTL8852B) Wi-Fi 6 for macOS / Hackintosh

TP-Link **Archer TX20U Nano**(Realtek **RTL8852B**、USB、Wi-Fi 6)を macOS Tahoe(OpenCore の Hackintosh)で使うための、
**ユーザー空間のドライバ**です。Linux 本家の `rtw89` ドライバを**改変せずに**、Linux カーネル API の互換層(kcompat)の上で動かしています。

> **Status: experimental.** 個人の実験プロジェクトです。無保証で、自己責任で使ってください。

## できること(実機で確認済み)
* USB 接続の認識(最初は `0bda:1a2b` の仮想 CD として見えるため、Wi-Fi モードへの切り替えを自動で行います)
* スキャン(2.4GHz / 5GHz)、WPA2-PSK 接続、**WPA3-Personal(SAE)**、DHCP、IPv4 / **IPv6(SLAAC)**
* 40/80MHz 帯域、ブロック ACK(A-MPDU)。USB 2.0 接続で下り約 160Mbit/s、上り約 260Mbit/s
* 常駐デーモンと、macOS 標準の「システム設定 > Wi-Fi」からの操作(スキャン、接続、切断)。通信は `en3` を通り、DHCP/IPv6 は macOS が行います。有線が切れたときは macOS が Wi-Fi に切り替えます

## 仕組み
```
システム設定 > Wi-Fi ─ RTW89Airport.kext(en3) ─ 共有キュー/レジストリ ─▶ rtw89d(root デーモン)
                                              ├─ rtw89 (Linux 本家のソース、無改変)
                                              ├─ kcompat(Linux カーネル API の互換層) + 簡易 mac80211 + MLME + WPA2/WPA3 supplicant
                                              └─ IOKit USB ── TX20U Nano
                       macOS のネットワーク ◀── utun(L3) + DHCP / ARP / IPv6 ND をデーモン内で処理
```
* 「システム設定 > Wi-Fi」に出ます。Apple 非公開の IO80211(OCLP の Modern Wireless パッチと、OpenCore で注入する kext)が必要です(`kext/`、`airport/` を参照)。

## ビルドと実行
必要なもの: Xcode(コマンドラインツールでなくフル版、DriverKit は不要)、macOS 26 / x86_64 で確認。

```bash
scripts/fetch-firmware.sh                 # Realtek のファームウェアを取得(リポジトリには含みません)
cd port && make                           # rtw89d をビルド
cd ../airport && ./build.sh    # RTW89Airport.kext(システム設定に出すためのドライバ。kext/ と airport/README.md を参照)
```

動作確認(root でなくても USB は開けますが、utun の作成には root が必要です):
```bash
sudo RTW89_FW_DIR=$PWD/rtw89 ./port/rtw89d daemon      # デーモン(制御ソケット: /var/run/rtw89d.sock)
echo status | nc -U /var/run/rtw89d.sock
```

インストール(LaunchDaemon): `sudo install/install.sh`、削除: `sudo install/uninstall.sh`。
`install.sh --dev` は root のデーモンがビルド成果物を直接実行する**開発用**の設定です。普段使いには向きません。

## 環境変数・制御コマンド(デバッグ)
`RTW89_DEBUG=<hex>`(ドライバのデバッグマスク)、`RTW89_USBDBG=1`、`RTW89_BT=1`。
制御ソケット: `status` `scan` `list` `connect <hex ssid> <hex psk>` `use <hex ssid>` `disconnect` `forget` `route on|off`
`autoconnect on|off` `wpa3 on|off` `rxdebug on|off` `bench` `rsn <ssid>` `quit`。

## 既知の制限
* WPA3: ハンティング・アンド・ペッキングのみ(H2E 専用の AP は未対応)。WPA3 専用 AP は未検証(混在モードの AP で検証済み)。
* SA Query への応答や、管理フレームの保護送信(ソフトウェア CCMP)は未実装。
* IPv6 は SLAAC のみ(DHCPv6 なし)、DNS は IPv4 のものを使います。
* 省電力、ローミング、複数 AP の最適化は未実装。USB 3.0 ポートでの速度は未検証。

## ライセンス
`rtw89` と Linux のヘッダーは GPL-2.0(rtw89 は GPL-2.0 OR BSD-3-Clause)。リンクした成果物は **GPL-2.0** として扱ってください(`COPYING`、`THIRD_PARTY.md`)。
自作部分(`port/host/`、`airport/src/`、`kext/src/`、`install/`、`tools/`)は BSD-3-Clause としても利用できます。

---
## English summary
Userspace Wi-Fi driver for the TP-Link Archer TX20U Nano (RTL8852B, USB) on macOS Tahoe / Hackintosh. It builds the **unmodified upstream Linux
`rtw89` driver** on top of a Linux-kernel-API compatibility layer, with IOKit USB, a minimal mac80211/MLME, a WPA2/WPA3 (SAE) supplicant,
DHCP/ARP/IPv6-ND handling and a `utun` interface, plus a launchd daemon and an IO80211 kext (RTW89Airport) that makes the adapter appear in System Settings > Wi-Fi. Verified on hardware: scan, WPA2-PSK, WPA3-SAE
(mixed-mode AP), IPv4/IPv6, 80 MHz with Block Ack (~160 Mbit/s down / ~260 Mbit/s up over USB 2.0). It does **not** appear in System Settings > Wi-Fi.
Experimental; no warranty. Build: `scripts/fetch-firmware.sh && make -C port`. Licence: GPL-2.0 combined work, see `COPYING` and `THIRD_PARTY.md`.
