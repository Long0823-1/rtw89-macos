# macOS Tahoe(Hackintosh)で TP-Link Archer TX20U Nano を Wi-Fi として使えるようにした

**ドライバがない USB Wi-Fi 子機を、Linux の rtw89 をそのまま動かして「システム設定 > Wi-Fi」に出すまで**

<!-- アイキャッチ: システム設定のWi-Fi画面(TP-Link_xxxx に接続中)のスクリーンショット -->

## はじめに

Hackintosh の Wi-Fi は、Broadcom 系や Intel 系のカードを使う方法が定番です。ところが、USB の Wi-Fi 6 子機を使いたいときは、macOS 向けのドライバが存在しません。

この記事で紹介するのは、TP-Link Archer TX20U Nano(Realtek RTL8852B)を、macOS Tahoe の Hackintosh で使えるようにした自作ドライバです。

- システム設定の Wi-Fi に、普通の Wi-Fi として表示される
- ネットワークの一覧、接続、切り替え、切断が設定画面から行える
- 再起動後、デーモン起動から約 6 秒でつながる
- 実測で、下り約 140〜155 Mbit/s(USB 2.0 接続)
- IPv4 / IPv6 に対応、WPA2 で接続

最初に、できないことも書いておきます。

- **AirDrop は使えません**(AWDL に未対応)
- WPA3 は、設定画面からは接続できません(macOS がパスフレーズを渡さないため)
- Hackintosh のスリープは、別の理由でパニックします(この Wi-Fi とは無関係です)

実験的なプロジェクトで、無保証です。EFI を書き換える手順を含むので、**必ずバックアップを取ってから、自己責任で**試してください。

## 2つの方法がある

| | かんたん版(PKG) | システム設定版(kext) |
|---|---|---|
| 入れ方 | PKG をダブルクリック | kext のビルドと EFI の編集 |
| カーネル拡張 | なし | あり(Hackintosh 向け) |
| 操作 | メニューバーのアプリ | システム設定の Wi-Fi |
| 向いている人 | まず試したい、本物の Mac | Hackintosh で標準の見た目にしたい |
| リポジトリ | [rtw89-macos-tun](https://github.com/Long0823-1/rtw89-macos-tun) | [rtw89-macos](https://github.com/Long0823-1/rtw89-macos) |

まず試すなら、かんたん版がおすすめです。

## かんたん版(PKG)のインストール

1. [リリースページ](https://github.com/Long0823-1/rtw89-macos-tun/releases)から `RTW89-WiFi-0.1.0.pkg` をダウンロードします。
2. PKG を**右クリック > 開く**で起動します(署名していないため、ダブルクリックだけでは開けないことがあります。その場合は「システム設定 > プライバシーとセキュリティ」で許可します)。
3. 案内に従って進めると、デーモン、ファームウェア、メニューバーアプリが入ります。
4. 子機を USB に挿します。最初は「DISK」として見えますが、自動で Wi-Fi に切り替わります。
5. メニューバーの「RTW89 Wi-Fi」からネットワークを選び、パスワードを入力します。次回以降は自動で接続します。

通信は仮想インターフェース(utun)を通ります。そのため、システム設定の Wi-Fi 画面には出ず、操作はメニューバーのアプリで行います。

削除するときは、次のコマンドを使います。

```bash
sudo /usr/local/libexec/rtw89/uninstall.sh
```

> この PKG は、記事の執筆時点では、クリーンな環境での動作確認ができていません。Hackintosh 以外の Mac での動作報告をお待ちしています。

ここから先は、システム設定に表示させたい人向けの、kext 版の説明です。

## 動作環境

| 項目 | 内容 |
|---|---|
| OS | macOS Tahoe 26.7.1 |
| ブートローダ | OpenCore |
| 機種 | MacPro7,1(Xeon Gold 6138 ×2) |
| 子機 | TP-Link Archer TX20U Nano(USB ID は 35bc:0108。最初は 0bda:1a2b の「DISK」として見えます) |
| 差し込み位置 | 制限はありません(前面・背面どちらでも動きます) |

他の構成での動作は未確認です。

## 仕組み

Linux には、RTL8852B 用の `rtw89` ドライバが入っています。これを書き換えずに、macOS のユーザー空間で動かしています。

```
システム設定 > Wi-Fi
      │
RTW89Airport.kext(en3 を作る IO80211 ドライバ)
      │  共有キュー / IORegistry
rtw89d(root で動く常駐デーモン)
      ├─ rtw89(Linux 本家のソース、無改変)
      ├─ Linux カーネル API の互換層、簡易 mac80211、MLME、WPA2 / WPA3 supplicant
      └─ IOKit USB ── TX20U Nano
```

部品は 3 つです。

1. **rtw89d**: 常駐デーモン。チップの制御、ファームウェア投入、接続処理を担当します。
2. **RTW89Airport.kext**: macOS に Wi-Fi インターフェース(en3)を見せるカーネル拡張。通信は macOS が en3 で行うので、DHCP や IPv6 も macOS 任せです。
3. **RTW89Hold.kext**: 子機が最初に見せる「仮想 CD」を、macOS の標準ドライバに横取りされないようにするための小さな kext です。

## 事前準備

ここが一番の難所です。macOS 12 以降は Apple が Wi-Fi の仕組みを変えていて、サードパーティの Wi-Fi ドライバが普通には動きません。そこで、次を済ませておく必要があります。

1. **EFI のバックアップ**
   - EFI パーティション全体を、別の場所にコピーしておきます。
   - 起動できなくなった場合に備えて、**USB メモリ等に安全な EFI を用意しておく**ことを強くおすすめします。
2. **OCLP の Modern Wireless ルートパッチ**
   - macOS が Wi-Fi の仕組みに使うユーザー空間のフレームワークを、古い形式に戻すパッチです。
3. **IOSkywalkFamily(パッチ済み)と IO80211FamilyLegacy を OpenCore から注入**
   - 標準の IOSkywalkFamily は、Kernel > Block で無効にします。
   - これらは Apple のバイナリなので、このリポジトリには入っていません。OCLP のパッチ用バイナリから取り出して使います。

この準備は OCLP と OpenCore に慣れた方向けです。うまくいかないときは、先に Intel/Broadcom カードを使う手順で Modern Wireless の動作を確認してください。

> 私の環境では、OCLP が Modern Wireless のパッチを提示しなかったため、OpenCore の DeviceProperties でパッチ対象になる PCI デバイスに見せかける方法を使いました。この部分は環境ごとに違うので、記事では詳しく扱いません。

## インストール手順

### 1. リポジトリを取得してビルド

```bash
git clone https://github.com/Long0823-1/rtw89-macos.git
cd rtw89-macos
scripts/fetch-firmware.sh      # Realtek のファームウェアを取得(リポジトリには含まれません)
(cd port && make)              # rtw89d をビルド
(cd airport && ./build.sh)     # RTW89Airport.kext をビルド
(cd kext && ./build.sh)        # RTW89Hold.kext をビルド
```

Xcode(フル版)が必要です。x86_64 の macOS 26 で確認しています。

### 2. kext を EFI に入れる

EFI パーティションをマウントします。

```bash
diskutil list | grep EFI
sudo diskutil mount disk0s1     # 上の結果に合わせて番号を変えてください
```

> EFI のディスク番号は、起動のたびに変わることがあります。毎回 `diskutil list` で確認してください。

config.plist を**必ずバックアップ**してから、kext をコピーします。

```bash
EFI="/Volumes/EFI/EFI/OC"       # ボリューム名は環境で違います
cp "$EFI/config.plist" "$EFI/config.plist.backup"
ditto kext/RTW89Hold.kext "$EFI/Kexts/RTW89Hold.kext"
ditto airport/RTW89Airport.kext "$EFI/Kexts/RTW89Airport.kext"
```

config.plist の Kernel > Add に、次の順で追加します(IOSkywalkFamily と IO80211FamilyLegacy の後ろ)。

| BundlePath | ExecutablePath |
|---|---|
| RTW89Hold.kext | Contents/MacOS/RTW89Hold |
| RTW89Airport.kext | Contents/MacOS/RTW89Airport |

どちらも `PlistPath` は `Contents/Info.plist`、`MinKernel` は `23.0.0`、`Arch` は `x86_64` です。

### 3. デーモンを入れる

```bash
sudo install/install.sh
```

次のものが入ります。

- LaunchDaemon の rtw89d(起動時に自動で動きます)
- ファームウェア
- ログイン後にネットワークを自動で選ぶ LaunchAgent

### 4. 再起動して確認

再起動して、次のコマンドで状態を見ます。

```bash
echo status | nc -U /var/run/rtw89d.sock
```

`"state":"connected"` になっていれば成功です。システム設定の Wi-Fi にも、ネットワーク一覧が出ます。

初回だけは、設定画面からネットワークを選んでパスワードを入力してください。2 回目以降は、再起動後に自動でつながります。

## 使い方

- システム設定 > Wi-Fi から、普通の Wi-Fi と同じように接続・切り替えができます。
- 起動後は、前回のネットワークに約 6 秒でつながります。
- ログは `/var/log/rtw89d.log` にあります。
- 削除は `sudo install/uninstall.sh` です。EFI の kext と config.plist の項目は、手動で戻してください。

## トラブルシューティング

**ネットワーク一覧が空になる**
数秒待つか、Wi-Fi を一度オフ・オンしてください。ログに `[airport] system disconnect` などが出ていないか確認します。

**起動が「abort commands」で止まる**
EFI の変更が原因のことがあります。用意しておいた安全な EFI で USB から起動し、`config.plist.backup` を書き戻してください。

**子機が「DISK」のまま、Wi-Fi にならない**
RTW89Hold.kext が読み込まれていない可能性があります。次で確認します。

```bash
ioreg -p IOUSB -l -w0 | grep -A30 "DISK@" | grep -E "IOService|IOUSBMassStorage"
```

`IOUSBMassStorageInterfaceNub` が出ていたら、Hold kext が効いていません。config.plist の項目を見直してください。

**デーモンの状態を見る**

```bash
echo status | nc -U /var/run/rtw89d.sock
sudo tail -f /var/log/rtw89d.log
```

## 開発で苦労したところ

- **USB の「ゼロ長パケット」の順序**: データ送信の直後に ZLP を送らないとチップが転送を結合して、ファームウェアが止まりました。高負荷時の受信停止の原因でした。
- **標準ドライバの横取り**: 仮想 CD を `IOUSBMassStorageDriver` が掴んで、Wi-Fi モードへの切り替えが失敗しました。RTW89Hold.kext で解決しました。
- **airportd の癖**: ネットワーク一覧の更新方法(通知にデータを付けると空になる、スキャン中は要求を拒否するなど)は、試行錯誤で見つけました。
- **起動の速さ**: 前回のチャンネルを覚えて、そのチャンネルだけを短くスキャンすることで、起動からつながるまでを約 6 秒にしました。

## 既知の制限と今後

- AirDrop は使えません。AWDL は Apple 非公開で、チップのファームウェアも想定していないため、実装は現実的ではありません。
- WPA3 は、設定画面から接続できません。
- Hackintosh のスリープは、IOPCIFamily でパニックします。Wi-Fi 関連の kext を外しても再現するため、この実装とは無関係と判断しています。`sudo pmset -a sleep 0` などでスリープを無効にして使っています。
- 省電力、ローミング、USB 3.0 での速度は未検証です。

## ライセンス

rtw89 と Linux のヘッダは GPL-2.0(rtw89 は GPL-2.0 OR BSD-3-Clause)です。リンクした成果物は GPL-2.0 として扱ってください。自作部分は BSD-3-Clause としても使えます。詳細はリポジトリの `COPYING` と `THIRD_PARTY.md` を見てください。

## おわりに

ソースコードは GitHub で公開しています。

- かんたん版(PKG): https://github.com/Long0823-1/rtw89-macos-tun
- システム設定版(kext): https://github.com/Long0823-1/rtw89-macos

動作報告や改善の提案(Issue / PR)は歓迎です。自己責任で、バックアップを取ってから試してください。
