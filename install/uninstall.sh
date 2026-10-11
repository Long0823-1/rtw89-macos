#!/bin/bash
# Removes the RTW89 Wi-Fi driver daemon and app. Run with: sudo ./uninstall.sh
if [ "$(id -u)" != 0 ]; then echo "run with sudo"; exit 1; fi
USER_NAME="${SUDO_USER:-$(stat -f %Su /dev/console)}"
USER_HOME="$(dscl . -read /Users/$USER_NAME NFSHomeDirectory | awk '{print $2}')"
launchctl bootout system /Library/LaunchDaemons/local.rtw89d.plist 2>/dev/null
sudo -u "$USER_NAME" launchctl bootout gui/$(id -u "$USER_NAME") "$USER_HOME/Library/LaunchAgents/local.rtw89.wifi.plist" 2>/dev/null
rm -f /Library/LaunchDaemons/local.rtw89d.plist "$USER_HOME/Library/LaunchAgents/local.rtw89.wifi.plist"
rm -rf /usr/local/libexec/rtw89 /usr/local/share/rtw89 "/Applications/RTW89 Wi-Fi.app" /var/run/rtw89d.sock
echo "Removed. Saved networks remain in /var/db/rtw89d (delete it to forget them)."
