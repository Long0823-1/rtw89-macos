#!/bin/bash
# Joins a preferred network through macOS itself (networksetup) when the adapter is not connected.
# Runs as the logged-in user (it needs that user's Wi-Fi passwords). macOS's own auto-join does not work with this driver
# ("driver not available"), and joining through networksetup keeps System Settings consistent.
IF=en3
SOCK=/var/run/rtw89d.sock
state() { echo status | nc -U $SOCK 2>/dev/null | sed -n 's/.*"state":"\([a-z_]*\)".*/\1/p'; }
idle=0
nap=1
while true; do
	sleep $nap
	[ "$(networksetup -getairportpower $IF 2>/dev/null | awk '{print $NF}')" = "On" ] || { idle=0; continue; }
	status=$(echo status | nc -U $SOCK 2>/dev/null)
	st=$(echo "$status" | sed -n 's/.*"state":"\([a-z_]*\)".*/\1/p')
	ds=$(echo "$status" | sed -n 's/.*"dstart":\([0-9]*\).*/\1/p')
	# poll fast while the daemon is young, slowly afterwards
	if [ -n "$ds" ] && [ "$ds" -lt 90 ]; then nap=1; else nap=8; fi
	if [ "$st" = connected ] && echo "$status" | grep -q '"sys":0'; then
		# the daemon joined by itself (e.g. before login) and macOS does not know the link: let macOS join the same network once
		ssid=$(echo "$status" | sed -n 's/.*"ssid":"\([^"]*\)".*/\1/p')
		[ -n "$ssid" ] && networksetup -setairportnetwork $IF "$ssid" >/dev/null 2>&1
		idle=0; sleep 10; continue
	fi
	case "$st" in
		idle|failed) idle=$((idle + 1)) ;;
		*) idle=0; continue ;;
	esac
	[ $idle -ge 2 ] || continue		# idle for two polls: macOS had its chance
	joined=0
	while IFS= read -r ssid; do
		[ -n "$ssid" ] || continue
		out=$(networksetup -setairportnetwork $IF "$ssid" 2>&1)
		[ -z "$out" ] || continue		# macOS could not even start the join (network not in range): next one
		# the join is asynchronous: wait for the daemon to report it before giving up on this network
		for _ in 1 2 3 4 5 6 7; do
			sleep 2
			[ "$(state)" = connected ] && { joined=1; break; }
		done
		[ $joined = 1 ] && break
	done < <(networksetup -listpreferredwirelessnetworks $IF 2>/dev/null | tail -n +2 | sed 's/^[[:space:]]*//')
	idle=0
	[ $joined = 1 ] || sleep 20
done
