#!/bin/bash
# Downloads the Realtek RTL8852B firmware from linux-firmware into rtw89/ (not stored in this repository).
set -e
cd "$(dirname "$0")/../rtw89"
BASE="https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/rtw89"
for f in rtw8852b_fw-2.bin rtw8852b_fw-1.bin rtw8852b_fw.bin; do
  [ -s "$f" ] || { echo "fetching $f"; curl -fsSL -o "$f" "$BASE/$f"; }
done
echo "firmware ready in $(pwd)  (Realtek firmware licence: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/LICENCE.rtlwifi_firmware.txt)"
