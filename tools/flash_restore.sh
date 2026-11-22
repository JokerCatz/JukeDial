#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Restore a full flash image after verifying its checksum.
# Usage: tools/flash_restore.sh /dev/ttyACM0 backup/factory_full_YYYY-MM-DD.bin
set -euo pipefail
PORT=${1:?port}; BIN=$(realpath "${2:?bin}")
cd "$(dirname "$0")/.."
export PATH="$HOME/.espressif/tools/python/v6.1/venv/bin:$PATH"  # esptool/espefuse from IDF v6.1 venv
# Compare the hash of the file we will write, not the path recorded inside the .sha256 file.
WANT=$(cut -d' ' -f1 "$BIN.sha256")
GOT=$(sha256sum "$BIN" | cut -d' ' -f1)
if [ -z "$WANT" ] || [ "$WANT" != "$GOT" ]; then
  echo "STOP: checksum mismatch for $BIN"; exit 4
fi
echo "$BIN: checksum OK"
esptool.py --chip esp32s31 -p "$PORT" -b 921600 write_flash 0 "$BIN"
