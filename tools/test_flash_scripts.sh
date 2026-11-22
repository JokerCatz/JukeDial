#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Offline tests for flash_backup.sh / flash_restore.sh guards. Uses a bogus port, so any
# test that reaches esptool fails with esptool's error code (2), not the guard's code.
set -u
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail=0
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1 (want exit $2, got $3)"; fail=1; fi; }

# 1. backup refuses to overwrite an existing image of the same name, before touching the device
D=$(date +%F); mkdir -p backup
created=0; [ -e "backup/factory_full_$D.bin" ] || { : > "backup/factory_full_$D.bin"; created=1; }
bash tools/flash_backup.sh /dev/nonexistent >/dev/null 2>&1; check backup_refuses_overwrite 3 $?
[ $created = 1 ] && rm -f "backup/factory_full_$D.bin"

# 2. restore rejects a file whose content does not match its .sha256 hash
printf 'good' > "$T/img.bin"; sha256sum "$T/img.bin" > "$T/img.bin.sha256"; printf 'evil' > "$T/img.bin"
bash tools/flash_restore.sh /dev/nonexistent "$T/img.bin" >/dev/null 2>&1; check restore_rejects_mismatch 4 $?

# 3. restore checks the given file even when .sha256 names another path, and works with a relative path from elsewhere
mkdir -p "$T/a"; printf 'evil' > "$T/a/img.bin"; printf 'good' > "$T/good.bin"
( cd "$T" && sha256sum good.bin | sed 's/good.bin/elsewhere.bin/' > a/img.bin.sha256 )
( cd "$T" && bash "$OLDPWD/tools/flash_restore.sh" /dev/nonexistent a/img.bin >/dev/null 2>&1 ); check restore_checks_given_file_relative 4 $?

exit $fail
