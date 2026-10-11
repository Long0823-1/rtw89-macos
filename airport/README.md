# RTW89Airport.kext — Wi-Fi interface for System Settings (experimental)

An `IO80211Controller` subclass built against the *legacy* IO80211 family (from macOS 12, injected by OpenCore, see `../kext/`).
Goal: show the rtw89d adapter in System Settings > Wi-Fi by forwarding apple80211 requests to the user-space daemon.

Status: milestone 0 — registers an `IO80211Interface` only; every request returns "unsupported".

* `build.sh` builds the kext (needs Xcode). `include/Airport/` holds Apple's private-class headers as reverse-engineered by
  OpenIntelWireless/itlwm (GPL-2.0, see `LICENSE.itlwm-headers`).
* Requires `IOSkywalkFamily.kext` (patched) and `IO80211FamilyLegacy.kext` injected by OpenCore, and OCLP's Modern Wireless root patch
  (user-space frameworks). Those Apple binaries are not in this repository.
