#!/bin/bash
# Installs the RTW89 Wi-Fi driver daemon + menu bar app. Run with: sudo ./install.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$(id -u)" != 0 ]; then echo "run with sudo"; exit 1; fi
USER_NAME="${SUDO_USER:-$(stat -f %Su /dev/console)}"
USER_HOME="$(dscl . -read /Users/$USER_NAME NFSHomeDirectory | awk '{print $2}')"

install -d /usr/local/libexec/rtw89 /usr/local/share/rtw89 /var/db/rtw89d
if [ "$1" = "--dev" ]; then
  # development mode: the root daemon runs the user-writable build directly (NOT for everyday use)
  ln -sf "$HERE/port/rtw89d" /usr/local/libexec/rtw89/rtw89d
  echo "DEV MODE: /usr/local/libexec/rtw89/rtw89d -> $HERE/port/rtw89d"
else
  rm -f /usr/local/libexec/rtw89/rtw89d
  install -m 755 "$HERE/port/rtw89d" /usr/local/libexec/rtw89/rtw89d
fi
install -m 644 "$HERE/rtw89/rtw8852b_fw-2.bin" /usr/local/share/rtw89/
install -m 644 "$HERE/rtw89/rtw8852b_fw-1.bin" /usr/local/share/rtw89/ 2>/dev/null || true
install -m 644 "$HERE/rtw89/rtw8852b_fw.bin" /usr/local/share/rtw89/ 2>/dev/null || true
rm -rf "/Applications/RTW89 Wi-Fi.app"
cp -R "$HERE/app/RTW89WiFi.app" "/Applications/RTW89 Wi-Fi.app"
codesign -s - --force "/Applications/RTW89 Wi-Fi.app" >/dev/null 2>&1 || true

cat > /Library/LaunchDaemons/local.rtw89d.plist <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>local.rtw89d</string>
  <key>ProgramArguments</key><array><string>/usr/local/libexec/rtw89/rtw89d</string><string>daemon</string></array>
  <key>EnvironmentVariables</key><dict><key>RTW89_FW_DIR</key><string>/usr/local/share/rtw89</string></dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>5</integer>
  <key>StandardErrorPath</key><string>/var/log/rtw89d.log</string>
  <key>StandardOutPath</key><string>/var/log/rtw89d.log</string>
</dict></plist>
PLIST
chown root:wheel /Library/LaunchDaemons/local.rtw89d.plist
chmod 644 /Library/LaunchDaemons/local.rtw89d.plist

mkdir -p "$USER_HOME/Library/LaunchAgents"
cat > "$USER_HOME/Library/LaunchAgents/local.rtw89.wifi.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>local.rtw89.wifi</string>
  <key>ProgramArguments</key><array><string>/Applications/RTW89 Wi-Fi.app/Contents/MacOS/RTW89WiFi</string></array>
  <key>RunAtLoad</key><true/>
</dict></plist>
PLIST
chown "$USER_NAME" "$USER_HOME/Library/LaunchAgents/local.rtw89.wifi.plist"

launchctl bootout system /Library/LaunchDaemons/local.rtw89d.plist 2>/dev/null || true
launchctl bootstrap system /Library/LaunchDaemons/local.rtw89d.plist
sudo -u "$USER_NAME" launchctl bootout gui/$(id -u "$USER_NAME") "$USER_HOME/Library/LaunchAgents/local.rtw89.wifi.plist" 2>/dev/null || true
sudo -u "$USER_NAME" launchctl bootstrap gui/$(id -u "$USER_NAME") "$USER_HOME/Library/LaunchAgents/local.rtw89.wifi.plist" || true
echo "Installed. Driver log: /var/log/rtw89d.log   Menu bar app: Wi-Fi icon in the menu bar."
