# TX20U Nano (RTL8852AU) macOS port plan

## Facts
- Chip: RTL8852AU (rtw89 `rtw8852au`, upstream in Linux >= 6.19). Firmware: `rtw89/rtw8852a_fw.bin` (1.4MB, saved here: rtw89/rtw8852a_fw.bin).
- Upstream rtw89 is `GPL-2.0 OR BSD-3-Clause` -> can be ported under BSD terms.
- TP-Link vendor driver: 1.46M lines (core/phl/os_dep), vendor HAL. NOT used as base; only as reference for USB IDs/quirks.
- Upstream sources for 8852A: rtw89/src (52 files, 5.2MB). Table files rtw8852a_table.c is 1.7MB (data, portable as-is).
- Target: macOS 26 (Tahoe), OpenCore, x86_64.

## Layers
1. USB/DMA/firmware/MAC/PHY/RFK core (rtw89 core.c, fw.c, mac.c, phy.c, rtw8852a*.c, usb.c): port by replacing Linux API (skbuff, workqueue, usb, mac80211 ops) with a small shim.
2. 802.11 stack: macOS has none public. Options: net80211 from itlwm (OpenBSD-derived) as the mac80211 replacement.
3. Network interface: IOEthernetController-style (itlwm approach), shown as Ethernet in Settings; Wi-Fi UI via Heliport-like menubar app. True Wi-Fi pane needs IO80211 (AirportItlwm), broken on Tahoe, last stage.
4. Loader: DriverKit dext (USB) vs kext via OpenCore. itlwm precedent = kext; kext needs OpenCore injection.

## Blockers on this machine
- No Xcode / Command Line Tools installed (git, python3 are stubs). Install Xcode before any build.
- Need USB ID of actual adapter (one of the IDs in rtw8852au.c, probably 0x35bc/0x2357 TP-Link variants).

## Next steps
1. Install Xcode; confirm USB ID on the Hackintosh (`system_profiler SPUSBDataType`).
2. Build a minimal kext/dext: match device, claim interface, pipes, upload fw via rtw89_fw_download equivalent, read efuse (MAC address), print to log.
3. Port TX/RX path + net80211 glue (scan, auth, assoc, WPA2 via net80211 4-way).
4. Menubar app, OpenCore config doc.

## UPDATE (verified on real hardware)
- Chip is RTL8852B (NOT 8852A). TP-Link Linux source maps 35BC:0108 -> RTL8852B; upstream rtw8852bu.c lists 35bc:0108.
- Adapter first enumerates as 0bda:1a2b "DISK" (virtual CD). Sending Realtek vendor CBW (CBWCB len 0x10, opcode 0xff) via IOKit
  (tools/modeswitch3, class IOUSBHostDevice, USBInterfaceOpenSeize) makes it re-enumerate as 35bc:0108 "802.11ac WLAN Adapter".
- Link speed is USB 2.0 (480Mbps) on this host port. Try a USB3 port later.
- libusb 1.0.30 (MacPorts) enumerates 0 devices on this system; use IOKit IOUSBHostDevice directly.
- Sources: rtw89/src now contains 8852A + 8852B files. Firmware: rtw89/rtw8852b_fw.bin (1.0MB), rtw8852b_fw-1.bin (1.2MB; newer fw format).
- OCLP root patches were uninstalled by the user.

## Revised next steps
1. IOKit userspace prototype: claim 35bc:0108, replicate rtw89 usb.c register access (vendor control reads/writes), read chip id + efuse MAC. De-risks everything before touching kext/dext.
2. Firmware download via rtw89_fw_download path, power-on sequence (mac.c).
3. Then TX/RX + net80211 glue, then kext/dext packaging.

## Milestone 2: MAC power-on works (tools/poweron.c, rtw.h, rtw.c)
- Vendor-request register access over IOKit verified. Power-off/on cycle (rtw89_mac_power_switch port) succeeds on real RTL8852B:
  IC_PWR_STATE MAC=ON, DMAC_FUNC_EN=0x7fff8000, CMAC_FUNC_EN=0x7000803f.
- Next: efuse dump (physical -> logical map, MAC addr), then firmware download (fw.c/mac.c fwdl path via bulk EP + H2C).

## Milestone 3: efuse read works (tools/efuse.c)
- Physical efuse (1216B) dumped via DDV, logical map decoded (rtw89 algorithm). Outputs efuse_phy.bin / efuse_log.bin.
- Logical offsets (8852B): USB mac_addr @0x488, rfe_type @0x2ca, xtal_k @0x2b9, country_code @0x2cb.
- NOTE: rfe_type must feed pwr_sps_ana/pwr_sps_dig_off branches (rfe==5 differs) -> pass from efuse once known.

## Milestone 4: FIRMWARE RUNNING (tools/fwdl.c, prefw.h, fwdefs.h)
- rtw8852b_fw-2.bin (type 5 CE firmware, cv=2) downloaded over USB bulk (EP7 = H2C, bulkout_id 2); WCPU reports FW_INIT_RDY (FWCTRL=0xe2).
- Required pre-steps (all ported): hci dma trx en, DMAC func/clk, DLE init (DLFW quota), HFC H2C cfg, usb mac_pre_init.
- Run: `cd tools && ./modeswitch3 (once, if device is 0bda:1a2b) && ./fwdl run ../rtw89/rtw8852b_fw-2.bin`
- Remaining before it is a Wi-Fi NIC: mac post-init (rtw89_mac_init: trx cfg, DLE SCC, HFC full), BB/RF init (phy tables from fw elements,
  rfk), H2C/C2H command layer (add vif/sta, scan offload, join), RX bulk-in parsing (rx desc, ppdu), TX path (txwd, mgmt+data),
  crypto, 802.11 state machine (net80211 / own minimal MLME), then macOS integration (kext/dext + IO80211/Heliport app).

## Milestone 5: full upstream rtw89 driver running on macOS (port/)  -- SCAN WORKS (25 BSS seen)
Architecture (decided after M4): instead of hand-porting, build the UNMODIFIED upstream rtw89 (Linux 6.19+, GPL-2.0 OR BSD-3-Clause) in macOS
userspace via a Linux-kernel-API compat layer:
- port/compat/: kcompat.h, kcompat2.h (types, list, bitops, atomics, skb, workqueue, timers, completion, firmware, printk), real kernel
  headers (ieee80211.h, net/mac80211.h, net/cfg80211.h, nl80211.h, ...), stub linux/*.h, linux/usb.h (urb API subset).
- port/host/ksupport.c  runtime for the above (threads for work/timers, skb, request_firmware from $RTW89_FW_DIR, printk with %pM/%*ph).
- port/host/usb_shim.c  Linux USB API on IOKit (control msg, async bulk in/out via runloop thread, URB refcounts).
- port/host/mm_core.c   minimal mac80211/cfg80211 implementation (wiphy_work thread, alloc/register hw, reg notifier, iterators, rx/tx-status,
  chanctx emulation, vif creation, hw_scan request builder).  port/host/mlme.c: beacon/probe-resp table.  port/host/main.c: CLI.
- patches/regd_null.sed: tiny build-time fix for a NULL deref that only shows with CONFIG_RTW89_DEBUGMSG.
- poll timeouts are scaled x20 in compat read_poll_timeout (IOKit USB latency).
Run:  cd port && make && RTW89_FW_DIR=../rtw89 ./rtw89d     (env: RTW89_DEBUG=<hexmask>, RTW89_USBDBG=1, RTW89_BT=1)
NOTE: first run after power-up switches USB mode (PAD_CTRL2 write) and the device re-enumerates; run again.
Next: auth/assoc (open + WPA2-PSK 4-way via own supplicant using CommonCrypto), key install via ops->set_key, sta_state machine,
data path (802.11<->Ethernet), utun + DHCP/ARP, menubar app, auto modeswitch, launchd.

## Milestone 6: WPA2 CONNECTION + DHCP WORK  (verified on hardware)
- port/host/mlme.c      scan table, auth (open), assoc (HT/VHT/HE caps from hw bands), sta/chanctx/vif-cfg bookkeeping, rx dispatch, A-MSDU, CCMP hdr/MIC strip.
- port/host/supplicant.c + crypto.c (CommonCrypto)  WPA2-PSK/CCMP 4-way + GTK; PBKDF2 checked vs IEEE test vector, MIC checked vs openssl.
- port/host/datapath.c  802.11 QoS data + CCMP header + LLC/SNAP, set_key via driver (HW CCMP).  port/host/net.c  DHCP, ARP, utun, optional default route/DNS.
- Result: associated + authorized to <home-ap-5GHz> (HE AP, ch48), DHCP lease obtained over the encrypted link.
- Usage: cd port && sudo RTW89_FW_DIR=../rtw89 RTW89_PSK_FILE=~/.rtw89_psk ./rtw89d connect <ssid>   (utun needs root; connect itself does not)
  default route/DNS only with RTW89_SET_DEFAULT=1.  Debug: RTW89_MLMEDBG, RTW89_WPADBG, RTW89_USBDBG, RTW89_DEBUG=<hex>, RTW89_BT.
- Lesson: wrong passphrase looks like "msg1 repeated then deauth reason 2" (not a bug).
Remaining: utun data test with root, IPv6, 40/80MHz widths, WMM params, roaming/PS, auto mode-switch, menubar app + launchd, packaging.

## Milestone 7: DATA PATH VERIFIED  (ping -b utun4 192.168.0.1: 5/5, ~2.6 ms, via the TX20U)
Next: daemon (auto modeswitch, probe retry, control socket, autoconnect/reconnect, default route + DNS, DHCP renew), SwiftUI/AppKit menubar app, launchd + installer.

## Milestone 8: macOS integration (step 5)
- port/host/daemon.c: `rtw89d daemon` -- auto mode-switch (0bda:1a2b -> Wi-Fi), probe retry across USB re-enumeration, control socket
  (/var/run/rtw89d.sock: status/scan/list/connect/use/disconnect/forget/route/autoconnect), known networks in /var/db/rtw89d/known.conf (root 0600),
  autoconnect + reconnect/backoff, DHCP renewal, optional default route + DNS, adapter-removal watchdog (exit -> launchd restart).
- app/: Swift menu bar app (RTW89 Wi-Fi.app): status icon, network list with signal, password prompt, connect/disconnect, primary-connection toggle, forget.
- install/install.sh, uninstall.sh: /usr/local/libexec/rtw89, firmware in /usr/local/share/rtw89, LaunchDaemon + login LaunchAgent.
- NOT possible: native System Settings > Wi-Fi pane (needs private IO80211 family, unavailable/unstable on Tahoe).

## Milestone 9: DONE for the requested scope (verified by the user on the Hackintosh)
- Installed daemon + menu bar app; auto-connect; Wi-Fi registered in configd (State:/Network/Service/rtw89) so macOS fails over to it when Ethernet is removed.
- Not done / ideas: IPv6 forwarding, 40/80 MHz channel width (currently 20 MHz), WMM parameters from the AP, power save, signing/notarization,
  USB3 port test, native System Settings pane (needs private IO80211 -- not feasible), WPA3/SAE, PMF.

## Milestone 10: speed + IPv6 (verified)
- 40/80 MHz: width/centre chosen from AP HT/VHT operation IEs (pick_width); HT opmode parsing fixed.
- Block Ack: ADDBA req/resp + DELBA handling, driver ampdu_action RX_START/TX_START/TX_OPERATIONAL, TX frames flagged IEEE80211_TX_CTL_AMPDU.
  This was THE speed bug: no BA agreement => AP sent unaggregated frames. Result: dl 2.7 -> ~160 Mbit/s, ul ~29 -> ~260 Mbit/s (USB 2.0 port).
- IPv6: link-local + SLAAC from RA (EUI-64), neighbour discovery proxy (net6.c), async ARP/ND queues (pend.h) so TX never blocks.
- USB: bounded in-flight OUT window + queue (IOKit kIOReturnBusy 0xe00002d5), stall recovery.
- Build: Makefile now tracks header deps (-MMD); stale objects had corrupted struct layouts once.
- Dev mode: install.sh --dev symlinks the root daemon to port/rtw89d; `echo quit | nc -U /var/run/rtw89d.sock` restarts it.
- Test servers rate-limit (Cloudflare 403); use networkQuality -I utun4 sparingly.
Remaining: WPA3-SAE (+PMF), RX reorder buffer, kext (AirportItlwm-Tahoe based), USB3 port test.

## Milestone 11: WPA3-Personal (SAE) implemented -- NOT yet verified against a real AP
- host/ecp256.c  P-256 (verified vs NIST points), host/sae.c  SAE group 19 hunting-and-pecking (two-sided self-test ok), crypto.c: HMAC/KDF-SHA256, AES-CMAC (RFC 4493 vectors ok).
- mlme: AKM/PMF detection in beacons (akm_psk/akm_sae/mfpc/mfpr/h2e), SAE auth (commit/confirm, anti-clogging token), RSN IE with MFPR|MFPC + PMKID + BIP,
  supplicant SAE mode (KDF-SHA256 PTK, AES-CMAC MIC, key descriptor v0, IGTK install via dp_install_igtk).
- Auto mode: SAE only if the AP offers no PSK; `echo "wpa3 on" | nc -U /var/run/rtw89d.sock` prefers SAE on WPA2/WPA3 mixed APs.
- TODO: verify on real WPA3 AP; hash-to-element (RSNXE bit) for APs that require it; protected mgmt TX (SW CCMP) and SA Query reply.
- User's AP <home-ap> is WPA2-PSK only (AKM 2, no PMF) -> needs to be switched to WPA2/WPA3 mixed for the test.

## Milestone 12: WPA3-Personal (SAE) VERIFIED on hardware
- Tested against a <phone-hotspot> hotspot (WPA2/WPA3 mixed, MFPC=1, RSNXE present): SAE commit/confirm OK (hunting-and-pecking accepted),
  assoc with PMKID/MFP RSN IE, 4-way with KDF-SHA256 + AES-CMAC MIC, GTK + IGTK installed, data over HW CCMP: ping 1.1.1.1 via the phone ok, link stable >100 s.
- Multi-AP fallback: several APs can share an SSID (a second "<phone-hotspot>" on ch1 never answered auth); mlme_connect now tries the next BSSID.
- mlme_disconnect now sends a (best effort) deauth. Debug: `echo "rxdebug on" | nc -U /var/run/rtw89d.sock`, `echo "wpa3 on"` to prefer SAE on mixed APs.
- Seen: AP sends protected Action frames (probably SA Query) that we do not answer; harmless so far (needs SW CCMP for mgmt if it ever matters).
- Not done: hash-to-element (only matters for APs that refuse hunting-and-pecking), WPA3-only APs untested, SA Query reply.

## Milestone 13: robustness under load (field report)
- Symptom: under heavy load the adapter disappears from USB (first error: IOKit "no device" 0xe00002c0, then bulk timeouts 0xe0004051)
  and re-enumerates in the 0bda:1a2b "DISK" (virtual CD) state, i.e. it looks like a power loss/reset, not a firmware hang.
- The adapter was behind a chain of USB 2.0 hubs (Realtek 4-port hub -> another hub). Suspected cause: insufficient bus power
  through hubs at 80 MHz / high TX duty. Next step: connect to a port on the Mac itself (ideally USB 3.0) and repeat the load.
- Recovery: the daemon now logs the CSW of the mode-switch command and rotates variants (vendor 0xff -> SCSI START STOP UNIT eject -> allow removal),
  because after a reset the vendor command can answer "failed" while the standard eject works.

## Milestone 14 — load-induced chip halt fixed (ZLP ordering)
Under sustained load the firmware halted itself (HALT_C2H 0x1000, host-dispatcher payload overflow) and RX/TX stopped.
Cause: the zero-length packet that terminates a bulk OUT transfer of an exact multiple of 512 bytes was sent *after* the
completion callback, by which time the next queued transfer was already behind it, so the chip merged two transfers.
Fix: queue the ZLP right behind the data under the submit lock. A 240 s saturating soak ran with no stall.
Note: do not poll chip registers from another thread (the USB register-read buffer is not thread safe).

## Milestone 15 — mode switch hardening
The virtual-CD interface is often held by IOUSBMassStorageDriver (seize fails with 0xe00002c5, or the device vanishes while re-enumerating).
Daemon: retry seize, re-apply the configuration to rebuild the interface nodes. Cleaner fix: `kext/RTW89Hold.kext` (codeless, loaded
through OpenCore) keeps Apple's driver from binding to 0bda:1a2b. Do not re-enumerate/reset the device mid-switch.

Result: with the executable RTW89Hold.kext loaded (OpenCore Kernel > Add, ExecutablePath Contents/MacOS/RTW89Hold) the virtual-CD
interface is held by RTW89Hold instead of IOUSBMassStorageDriver, and the daemon's mode switch succeeds after replugging.

## Milestone 16 — System Settings > Wi-Fi integration (en3)
The adapter now appears in macOS System Settings > Wi-Fi and carries the traffic on **en3**; macOS runs DHCP/ARP/IPv6 itself.
Pieces (see `kext/` and `airport/`):
* OCLP "Modern Wireless" root patch (user-space frameworks) + OpenCore-injected `IOSkywalkFamily` (patched) and `IO80211FamilyLegacy` (macOS 12) kexts.
* `RTW89Hold.kext`: keeps IOUSBMassStorageDriver off the virtual CD so the mode switch is reliable (interface personality needs
  bConfigurationValue + bInterfaceNumber).
* `RTW89Airport.kext`: IO80211Controller subclass; scan/associate/link state via IORegistry properties, data via a shared TX queue
  (IOSharedDataQueue) and an `rx` external method. rtw89d (`airport_bridge.c`) is the user-space side.
Lessons: airportd drops scan results unless CHANNELS_INFO etc. are answered; ASSOCIATE_RESULT success is 1; macOS hands the WPA2 PMK (32 bytes)
in the associate request; failures must be reported (ASSOC_DONE) so macOS prompts again; IFF_RUNNING must be raised explicitly and enable() must
call the superclass; scans need wildcard probe requests (active) or APs flicker; results are merged over 45 s.
Open items: WPA3-SAE from System Settings (needs the passphrase, macOS only provides a PMK for WPA2), sleep/wake, throughput on en3, adapter stall under load.
