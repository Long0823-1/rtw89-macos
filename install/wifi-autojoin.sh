#!/bin/bash
# Joins a preferred network through macOS itself (networksetup) when the adapter is not connected.
# Runs as the logged-in user (it needs that user's Wi-Fi passwords). macOS's own auto-join does not work with this driver
# ("driver not available"), and joining through networksetup keeps System Settings consistent.
IF=en3
SOCK=/var/run/rtw89d.sock
idle=0
while true; do
	sleep 8
	[ "$(networksetup -getairportpower $IF 2>/dev/null | awk '{print $NF}')" = "On" ] || { idle=0; continue; }
	st=$(echo status | nc -U $SOCK 2>/dev/null | sed -n 's/.*"state":"\([a-z_]*\)".*/\1/p')
	case "$st" in
		idle|failed) idle=$((idle + 1)) ;;
		*) idle=0; continue ;;
	esac
	[ $idle -ge 2 ] || continue		# idle for ~16 s: macOS had its chance
	networksetup -listpreferredwirelessnetworks $IF 2>/dev/null | tail -n +2 | sed 's/^[[:space:]]*//' | while IFS= read -r ssid; do
		[ -n "$ssid" ] || continue
		out=$(networksetup -setairportnetwork $IF "$ssid" 2>&1)
		[ -z "$out" ] && break		# empty output means the join was started
	done
	idle=0
	sleep 20
done
