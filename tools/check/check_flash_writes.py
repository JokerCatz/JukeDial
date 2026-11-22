#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Flash/NVS write guard (make lint-app, sim-test). A flash write freezes the cache; a task whose stack is in PSRAM (the LVGL task, most app tasks) asserts
there (cache_utils.c esp_task_stack_is_sane_cache_disabled; peer board 2026-11-15, the USB 功能 cell). So direct calls of nvs_set_*, nvs_commit, nvs_erase_*,
nvs_flash_erase, esp_partition_write/erase*, esp_flash_write/erase*, spi_flash_write/erase*, esp_rom_spiflash_write/erase*, bootloader_flash_write/erase*, esp_efuse_write*, esp_ota_* are allowed only in the (file, function) pairs of ALLOW
below, each of which runs on an internal-stack task; everything else must call flash_safe_call (app/main/flash_safe.h) with a callback listed here.
Writes done inside IDF, not seen by this scan: Bluedroid bond / BT data storage (nvs from the BT task: internal stack, fine), PHY calibration and Wi-Fi
config (nvs from the wifi/main task, fine), nvs_flash_init's own migration (app_main, below). The FATFS / SD card path is not flash. A new writer: add the
function here with a reason only if its task is known to have an internal stack; otherwise use flash_safe_call.
usage: check_flash_writes.py [--selftest]"""
import os, re, sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOTS = ("app/main", "components")
SKIP_DIRS = {"build", "build-host", "build-host-asan", "managed_components", "host_test", "fixtures"}
ALLOW = {
    ("app/main/usb_mode.c", "combo_write"): "the flash_safe_call callback of usb_mode_set_combo (internal-stack task)",
    ("app/main/ext_ble.c", "own_addr_load"): "ext_ble_init <- ext_xport_start <- usb_mode_run, which runs on the main task (internal stack)",
    ("app/main/app_main.c", "app_main"): "nvs_flash_erase on the recovery path of nvs_flash_init; app_main is the main task (internal stack)",
}
CALL = re.compile(r'\b(nvs_set_\w+|nvs_commit|nvs_erase_\w+|nvs_flash_erase\w*|esp_partition_(?:write|erase)\w*|esp_flash_(?:write|erase)\w*|spi_flash_(?:write|erase)\w*|esp_rom_spiflash_(?:write|erase|unlock)\w*|bootloader_flash_(?:write|erase)\w*|esp_efuse_(?:write|batch_write|set)\w*|esp_ota_\w+)\s*\(')
FUNC = re.compile(r'^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;]*$')

def strip(s):
    out, i, n, q = [], 0, len(s), None
    while i < n:
        c = s[i]
        if q:
            out.append(c if c == q else " ")                 # string contents are blanked
            if c == "\\" and i + 1 < n: out.append(" "); i += 1
            elif c == q: q = None
        elif c in "\"'": q = c; out.append(c)
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("\n" * s.count("\n", i, j)); i = j; continue
        elif s.startswith("//", i):
            j = s.find("\n", i); i = n if j < 0 else j; continue
        else: out.append(c)
        i += 1
    return "".join(out)

def findings(rel, src):
    res, func = [], None
    for no, line in enumerate(strip(src).split("\n"), 1):
        m = FUNC.match(line)
        if m and not line.startswith(("#", "typedef")): func = m.group(1)
        for c in CALL.finditer(line):
            if c.group(1) == "nvs_flash_init": continue
            if (rel, func) not in ALLOW: res.append(f"{rel}:{no}: {c.group(1)} in {func or '?'}: direct flash write outside the allowlist (use flash_safe_call)")
    return res

def selftest():
    assert findings("x.c", 'static void f(void *a)\n{\n    nvs_commit(h);\n}')
    assert findings("x.c", 'void g(void)\n{\n  esp_partition_write(p, 0, b, 4);\n}')
    assert findings("x.c", 'void g(void)\n{\n  nvs_flash_erase();\n}')
    assert not findings("x.c", '/* nvs_commit(h); */ void g(void) { puts("nvs_set_u8("); }')
    assert not findings("app/main/usb_mode.c", 'static void combo_write(void *a)\n{\n    nvs_set_u8(h, "usb", 1); nvs_commit(h);\n}')
    assert findings("app/main/usb_mode.c", 'static void combo_write(void *a)\n{\n}\nvoid other(void)\n{\n    nvs_commit(h);\n}')
    assert findings("x.c", 'void g(void)\n{\n  esp_rom_spiflash_erase_sector(1);\n}')
    assert findings("x.c", 'void g(void)\n{\n  esp_efuse_write_field_blob(a, b, 1);\n}')
    assert findings("x.h", 'static inline void g(void)\n{\n  spi_flash_write(0, b, 4);\n}')
    print("check_flash_writes selftest ok")

def main():
    if "--selftest" in sys.argv: return selftest()
    bad = []
    for r in ROOTS:
        for dp, dn, fn in os.walk(os.path.join(REPO, r)):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                if not f.endswith((".c", ".cpp", ".h")): continue
                rel = os.path.relpath(os.path.join(dp, f), REPO)
                bad += findings(rel, open(os.path.join(dp, f), encoding="utf-8", errors="replace").read())
    if bad: sys.exit("check_flash_writes: " + str(len(bad)) + " finding(s)\n" + "\n".join(bad))
    print("check_flash_writes ok")

if __name__ == "__main__": main()
