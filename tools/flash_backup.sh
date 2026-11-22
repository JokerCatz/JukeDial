#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Read-only eFuse/security check, then full 16MB flash backup.
# Usage: tools/flash_backup.sh /dev/ttyACM0
set -euo pipefail
PORT=${1:?port}
cd "$(dirname "$0")/.."
export PATH="$HOME/.espressif/tools/python/v6.1/venv/bin:$PATH"  # esptool/espefuse from IDF v6.1 venv
mkdir -p backup
D=$(date +%F)
OUT="backup/factory_full_$D.bin"
# Never overwrite an existing image: after hwtest is flashed, a second run would replace the
# only copy of the factory firmware. Checked before touching the device.
if [ "${CHECK_ONLY:-0}" != 1 ] && [ -e "$OUT" ]; then
  echo "STOP: $OUT already exists; move it away first"; exit 3
fi
espefuse.py --chip esp32s31 -p "$PORT" summary > "backup/efuse_summary_$D.txt" 2>&1
esptool.py --chip esp32s31 -p "$PORT" get_security_info > "backup/security_info_$D.txt" 2>&1 || true
echo "--- relevant eFuse lines ---"
grep -iE 'crypt|secure_boot|dis_download|dis_usb' "backup/efuse_summary_$D.txt" || true
echo "--- security info ---"
grep -iE 'encrypt|secure boot|flags' "backup/security_info_$D.txt" || true
if grep -qiE 'Flash encryption: *Enabled|Secure boot: *Enabled' "backup/security_info_$D.txt" \
   || grep -qiE 'SECURE_BOOT_EN .*= *True' "backup/efuse_summary_$D.txt" \
   || grep -qiE 'SPI_BOOT_CRYPT_CNT .*= *(Enable|0b0*1|[1-7] )' "backup/efuse_summary_$D.txt"; then
  echo "STOP: encryption/secure boot enabled"; exit 2
fi
[ "${CHECK_ONLY:-0}" = 1 ] && { echo "check only, no backup"; exit 0; }
esptool.py --chip esp32s31 -p "$PORT" -b 921600 read_flash 0 0x1000000 "$OUT"
sha256sum "$OUT" | tee "$OUT.sha256"
