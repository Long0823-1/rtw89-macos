# rtw89-macos — TP-Link Archer TX20U Nano (RTL8852B) Wi-Fi 6 for macOS / Hackintosh

TP-Link **Archer TX20U Nano**(Realtek **RTL8852B**、USB、Wi-Fi 6)を macOS Tahoe(OpenCore の Hackintosh)で使うための、
**ユーザー空間のドライバ**です。Linux 本家の `rtw89` ドライバを**改変せずに**、Linux カーネル API の互換層(kcompat)の上で動かしています。

> **Status: experimental.** 個人の実験プロジェクトです。無保証で、自己責任で使ってください。

## できること(実機で確認済み)
* USB 接続の認識(最初は `0bda:1a2b` の仮想 CD として見えるため、Wi-Fi モードへの切り替えを自動で行います。macOS 標準の大容量ストレージドライバに横取りされないよう `RTW89Hold.kext` を使います)
* **「システム設定 > Wi-Fi」に表示**され、スキャン、接続、切り替え、切断を設定画面から行えます。通信は `en3` を通り、DHCP / IPv6(SLAAC)は macOS が行います
* WPA2-PSK での接続。WPA3-Personal(SAE)はデーモン側では実装済みですが、設定画面からの接続は macOS がパスフレーズを渡さないため未対応です
* **起動から約 6 秒で前回のネットワークに自動接続**(前回のチャンネルだけを素早くスキャンします)。ログイン後は LaunchAgent が `networksetup` で macOS に接続を認識させます
* 40/80MHz 帯域、ブロック ACK(A-MPDU)。USB 2.0 接続で実測 約 140〜155 Mbit/s
* 有線が切れたときは、macOS が Wi-Fi に切り替えます

## できないこと
* **AirDrop**(AWDL)は使えません
* macOS 標準の自動接続は、ドライバが「利用不可(37)」で拒否されるため動きません。代わりに LaunchAgent(`install/wifi-autojoin.sh`)が優先ネットワークへ接続します
* Hackintosh のスリープはカーネルパニックします(Wi-Fi 関連の kext を外しても再現し、このドライバとは無関係と判断しています)。`sudo pmset -a sleep 0` などで無効にしてください

## 仕組み
```
システム設定 > Wi-Fi ─ RTW89Airport.kext(en3) ─ 共有キュー/IORegistry ─▶ rtw89d(root デーモン)
                                              ├─ rtw89 (Linux 本家のソース、無改変)
                                              ├─ kcompat(Linux カーネル API の互換層) + 簡易 mac80211 + MLME + WPA2/WPA3 supplicant
                                              └─ IOKit USB ── TX20U Nano
RTW89Hold.kext: 仮想 CD(0bda:1a2b)を macOS の標準ドライバに掴ませない
```
* Apple 非公開の IO80211 を使うため、**OCLP の Modern Wireless パッチ**と、OpenCore で注入する **IOSkywalkFamily(パッチ済み)/ IO80211FamilyLegacy** が必要です。Apple のバイナリはこのリポジトリに含みません。
* `RTW89Airport.kext` は起動時には読み込まれず、アダプタの準備ができたときに `rtw89d` が `IOResources` に `RTW89Airport` を公開して開始させます(起動時の開始は、機種によってはブートが止まるため避けています)。
* 通信は kext の共有キューで受け渡します。utun を使う古い方式は `RTW89_UTUN=1` のときだけ動きます。

## ビルドとインストール
必要なもの: Xcode(フル版)、macOS 26 / x86_64 で確認。

```bash
scripts/fetch-firmware.sh      # Realtek のファームウェアを取得(リポジトリには含みません)
(cd port && make)              # rtw89d
(cd kext && ./build.sh)        # RTW89Hold.kext
(cd airport && ./build.sh)     # RTW89Airport.kext
```

1. **EFI をバックアップ**し、`kext/README.md` と `airport/README.md` に従って、2 つの kext を `EFI/OC/Kexts/` に置き、`config.plist` の Kernel > Add に追加します(IOSkywalkFamily と IO80211FamilyLegacy の後ろ)。
2. デーモンを入れます(LaunchDaemon と LaunchAgent を登録します):
   ```bash
   sudo install/install.sh
   ```
   削除は `sudo install/uninstall.sh`。`install.sh --dev` は開発用(root のデーモンがビルド成果物を直接実行)で、普段使いには向きません。
3. 再起動して、状態を確認します:
   ```bash
   echo status | nc -U /var/run/rtw89d.sock
   ```
   初回だけ、システム設定でネットワークを選んでパスワードを入力します。ログは `/var/log/rtw89d.log`、既知のネットワークは `/var/db/rtw89d/` にあります。

## 環境変数・制御コマンド(デバッグ)
`RTW89_DEBUG=<hex>`(ドライバのデバッグマスク)、`RTW89_USBDBG=1`、`RTW89_BT=1`。
制御ソケット: `status` `scan` `list` `connect <hex ssid> <hex psk>` `use <hex ssid>` `disconnect` `forget` `route on|off`
`autoconnect on|off` `wpa3 on|off` `rxdebug on|off` `bench` `rsn <ssid>` `quit`。

## 既知の制限
* WPA3: ハンティング・アンド・ペッキングのみ(H2E 専用の AP は未対応)。WPA3 専用 AP は未検証(混在モードの AP で検証済み)。
* SA Query への応答や、管理フレームの保護送信(ソフトウェア CCMP)は未実装。
* IPv6 は SLAAC のみ(DHCPv6 なし)、DNS は IPv4 のものを使います。
* 省電力、ローミング、複数 AP の最適化は未実装。USB 3.0 ポートでの速度は未検証。
* ネットワーク一覧が数秒空になることがあります(スキャン中の要求を macOS が拒否される間)。

## ライセンス
`rtw89` と Linux のヘッダーは GPL-2.0(rtw89 は GPL-2.0 OR BSD-3-Clause)。リンクした成果物は **GPL-2.0** として扱ってください(`COPYING`、`THIRD_PARTY.md`)。
自作部分(`port/host/`、`airport/src/`、`kext/src/`、`install/`、`tools/`)は BSD-3-Clause としても利用できます。

---
## English summary
Userspace Wi-Fi driver for the TP-Link Archer TX20U Nano (RTL8852B, USB) on macOS Tahoe / Hackintosh. It builds the **unmodified upstream Linux
`rtw89` driver** on top of a Linux-kernel-API compatibility layer, with IOKit USB, a minimal mac80211/MLME, a WPA2/WPA3 (SAE) supplicant,
and a launchd daemon plus an IO80211 kext (RTW89Airport, interface en3) that makes the adapter appear in System Settings > Wi-Fi; macOS itself does DHCP/IPv6. A small kext (RTW89Hold) keeps IOUSBMassStorageDriver off the virtual CD. Verified on hardware: scan/connect/switch from System Settings, WPA2-PSK, IPv4/IPv6, 80 MHz with Block Ack (~140-155 Mbit/s over USB 2.0), automatic reconnect ~6 s after start. No AirDrop (AWDL); WPA3 only works from the daemon, not from System Settings. Needs OCLP's Modern Wireless patch plus OpenCore-injected IOSkywalkFamily/IO80211FamilyLegacy.
Experimental; no warranty. Build: `scripts/fetch-firmware.sh && make -C port`. Licence: GPL-2.0 combined work, see `COPYING` and `THIRD_PARTY.md`.

## Support / 開発支援

開発・保守活動をKo-fiで支援できます。

[Ko-fiで支援する](https://ko-fi.com/long0823)
