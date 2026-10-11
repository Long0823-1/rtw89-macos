# RTW89Airport.kext — Wi-Fi interface for System Settings (experimental)

An `IO80211Controller` subclass built against the *legacy* IO80211 family (from macOS 12, injected by OpenCore, see `../kext/`).
Goal: show the rtw89d adapter in System Settings > Wi-Fi by forwarding apple80211 requests to the user-space daemon.

Status: milestone 0 — registers an `IO80211Interface` only; every request returns "unsupported".

* `build.sh` builds the kext (needs Xcode). `include/Airport/` holds Apple's private-class headers as reverse-engineered by
  OpenIntelWireless/itlwm (GPL-2.0, see `LICENSE.itlwm-headers`).
* Requires `IOSkywalkFamily.kext` (patched) and `IO80211FamilyLegacy.kext` injected by OpenCore, and OCLP's Modern Wireless root patch
  (user-space frameworks). Those Apple binaries are not in this repository.

## Starting the driver
The personality matches `IOResources` with `IOResourceMatch = RTW89Airport`, so nothing starts at boot (a boot-time start hung the
machine with XHCI "abort commands"). Once the system is up, publish the resource to start it:
`ioreg`-side trigger from root: set the `RTW89Airport` property on `IOResources` (done by `rtw89d` or by `tools/airport-start`).

## Data path (stage B)
`RTW89AirportUserClient` gives rtw89d a shared TX queue (frames macOS sends through en3) and an `rx` external method (frames
received from the air). With the data path active macOS runs DHCP/ARP/IPv6 on en3 itself; rtw89d then skips its own utun/DHCP.
If the kext or the user client is missing, rtw89d falls back to the old utun path (`RTW89_UTUN=1` forces it).
rtw89d publishes the `RTW89Airport` resource with the adapter MAC when the adapter is ready, which also starts the kext
(no manual `airport-start` needed; the interface takes that MAC).
