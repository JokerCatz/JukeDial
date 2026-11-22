#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# The difference between the esp_tinyusb override (app/components/esp_tinyusb, 外接模式 T4) and the managed copy it was made from.
# Expected: only the CFG_TUD_AUDIO block in include/tusb_config.h and UPSTREAM.txt. Run it when the managed version moves (app/managed_components must exist: make build once).
cd "$(dirname "$0")/.."
M=app/managed_components/espressif__esp_tinyusb
[ -d "$M" ] || { echo "esp_tinyusb_diff: $M is missing (it is only there after a build on a branch without the override, e.g. viz-page: the component manager drops it while override_path is set)"; exit 2; }
ver=$(sed -n 's/^version: *//p' "$M/idf_component.yml")
echo "managed esp_tinyusb version: $ver (the override was made from 2.4.0)"
diff -ru -x test_apps -x CHECKSUMS.json -x UPSTREAM.txt "$M" app/components/esp_tinyusb
