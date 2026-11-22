#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Safe mode stays on core 0: check_safe_mode.py   (lint-app-checks runs it; --selftest tests it).
Safe mode (app/main/guard.c guard_safe_mode, entered after 3 consecutive crashes) must start only the display, whose LVGL task the
BSP pins to core 0, and loop. Core 1 is left idle, NOT stalled (a stall held longer than the 800 ms interrupt watchdog would itself
reset the board: see the comment in guard.c). This check reads the sources, follows every call from guard_safe_mode() into the
functions defined in app/main and in the two BSPs (components/jc3636f118_bsp, components/fakepod_bsp), and fails when that path
 1. creates a task without pinning it to core 0 (xTaskCreate*, a core argument other than 0, tskNO_AFFINITY), or the BSP display
    start does not set `task_core_id = 0` for the LVGL adapter's task;
 2. calls core_park_* (no stall in safe mode), or audio / Bluetooth / Wi-Fi / USB / card code (FORBIDDEN prefixes below);
 3. calls a function defined nowhere in those trees that is not in KNOWN (IDF / LVGL leaf calls checked by hand): a new external
    call must be looked at and added, so nothing new slips in unnoticed."""
import os, re, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC_DIRS = ["app/main", "app/main/ui", "components/jc3636f118_bsp/src", "components/fakepod_bsp/src"]
ENTRY = ("app/main/guard.c", "guard_safe_mode")
DISPLAY_START = "bsp_display_start_ex"
FORBIDDEN = ("core_park_", "audio_", "bsp_audio", "player_", "pc_player", "esp_bt", "esp_bluedroid", "esp_a2d", "esp_avrc", "esp_hf",
             "bt_", "ble_", "esp_wifi", "wifi_", "esp_netif", "tinyusb", "tusb_", "usb_", "ext_uac", "sd_", "sdmmc", "esp_vfs_fat",
             "card_", "nvs_", "cfg_", "fopen", "esp_flash_", "i2s_")
# external leaf calls reviewed for this path (IDF driver / LVGL / libc / FreeRTOS delay): none creates a task off core 0 or touches
# audio, radio, USB or the card. esp_lv_adapter_init creates the LVGL task on acfg.task_core_id (checked by rule 1).
KNOWN = {
    "xTaskCreatePinnedToCore", "xTaskCreate", "xTaskCreateStatic", "xTaskCreateStaticPinnedToCore",   # rule 1 checks their core
    "printf", "snprintf", "vTaskDelay", "pdMS_TO_TICKS", "gpio_set_direction", "gpio_set_level", "gpio_config",
    "spi_bus_initialize", "esp_lcd_new_panel_io_spi", "esp_lcd_new_panel_st77916", "esp_lcd_new_panel_co5300", "esp_lcd_panel_reset",
    "esp_lcd_panel_init", "esp_lcd_panel_mirror", "esp_lcd_panel_disp_on_off", "esp_lcd_panel_io_tx_param", "esp_lcd_panel_swap_xy",
    "esp_lv_adapter_init", "esp_lv_adapter_register_display", "esp_lv_adapter_fps_stats_enable", "esp_lv_adapter_start",
    "esp_lv_adapter_lock", "esp_lv_adapter_unlock", "ledc_timer_config", "ledc_channel_config", "ledc_set_duty", "ledc_update_duty",
    "i2c_new_master_bus", "i2c_master_bus_add_device", "i2c_master_transmit", "i2c_master_transmit_receive", "i2c_master_probe",
    "pc_guard_safe_screen", "pc_rotate_map", "pc_lcdclk_default", "pc_lcdclk_norm", "pc_lab_lcd_ok", "pc_lab_lcd_internal_bytes",   # player_core: pure functions (host-tested), no tasks, no I/O
    "calloc", "free", "malloc",
    "esp_rom_delay_us", "memset", "memcpy", "strlen", "lv_display_get_screen_active", "lv_obj_set_style_bg_color", "lv_color_black",
    "lv_obj_set_style_bg_opa", "lv_obj_set_style_text_color", "lv_color_white", "lv_label_set_text", "lv_label_create",
    "lv_timer_create", "lv_timer_set_repeat_count", "lv_obj_create", "lv_screen_load", "lv_obj_set_style_text_font",
    "lv_obj_set_style_text_align", "lv_obj_set_width", "lv_obj_set_size", "lv_obj_align", "lv_obj_set_pos", "lv_obj_center",
    "lv_label_set_long_mode", "lv_obj_set_style_text_line_space", "lv_display_get_horizontal_resolution",
    "lv_display_get_vertical_resolution", "lv_obj_remove_flag", "lv_obj_add_flag", "lv_obj_set_style_pad_all",
    "lv_obj_set_style_border_width", "lv_obj_set_style_radius", "lv_obj_set_align", "lv_obj_set_x", "lv_obj_set_y",
    "esp_err_to_name", "ESP_LOGE", "ESP_LOGW", "ESP_LOGI", "ESP_RETURN_ON_ERROR", "ESP_RETURN_ON_FALSE", "ESP_ERROR_CHECK",
    "ST77916_PANEL_BUS_QSPI_CONFIG", "ST77916_PANEL_IO_QSPI_CONFIG", "ESP_LV_ADAPTER_DEFAULT_CONFIG",
    "ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG", "sizeof", "MIN", "MAX", "BIT", "abs",
}
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "defined", "__attribute__", "noreturn", "typeof"}


def strip(src):
    """comments, string and char literals -> blanks (line numbers kept)"""
    src = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m[0]), src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', '""', src)


def definitions(src):
    """stripped C source -> {function name: (is_static, body text)}"""
    out = {}
    for m in re.finditer(r"^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\(([^;{}()]|\([^()]*\))*\)\s*(?:__attribute__\s*\(\([^)]*\)\))?\s*\{", src, re.M):
        name, i, depth = m[1], m.end(), 1
        if name in KEYWORDS: continue
        while i < len(src) and depth:
            depth += {"{": 1, "}": -1}.get(src[i], 0); i += 1
        out.setdefault(name, (bool(re.search(r"\bstatic\b", m[0].split(name)[0])), src[m.end():i - 1]))
    return out


def calls(body):
    return {c for c in re.findall(r"\b([A-Za-z_]\w*)\s*\(", body) if c not in KEYWORDS}


def task_problems(fn, body):
    p = []
    for m in re.finditer(r"\b(xTaskCreate\w*)\s*\(([^;]*)\)\s*;", body):
        if m[1] != "xTaskCreatePinnedToCore": p.append(f"{fn}: {m[1]} without a core (safe mode creates tasks on core 0 only)")
        elif not re.search(r",\s*0\s*$", m[2]): p.append(f"{fn}: xTaskCreatePinnedToCore with a core other than 0")
    if "tskNO_AFFINITY" in body: p.append(f"{fn}: tskNO_AFFINITY on the safe-mode path")
    return p


def resolve(defs, here, name):
    """call `name` from file `here` -> [(file, name)]: the file's own (static or not) definition, else every non-static one
    (both BSPs define the same public names: both boards' bodies are checked)"""
    if (here, name) in defs: return [(here, name)]
    return [k for k, (st, _) in defs.items() if k[1] == name and not st]


def allowed_leaf(c):
    return c in KNOWN or c.isupper() or (c.startswith("lv_") and not c.startswith("lv_fs")) or c.startswith(("heap_caps_", "__"))


def check(defs, entry):
    """defs: {(file, name): (is_static, body)}; entry: (file, name) -> (problems, visited)"""
    probs, seen, todo = [], set(), [entry]
    while todo:
        key = todo.pop()
        if key in seen: continue
        seen.add(key)
        fn, body = key[1], defs[key][1]
        probs += task_problems(fn, body)
        if fn == DISPLAY_START and not re.search(r"task_core_id\s*=\s*0\s*;", body):
            probs.append(f"{fn} does not pin the LVGL adapter task to core 0 (acfg.task_core_id = 0)")
        for c in sorted(calls(body)):
            if c.startswith(FORBIDDEN):
                probs.append(f"{key[0]} {fn} calls {c}: safe mode starts no audio / Bluetooth / Wi-Fi / USB / card code and never parks core 1")
                continue
            r = resolve(defs, key[0], c)
            if r: todo += r
            elif not allowed_leaf(c):
                probs.append(f"{key[0]} {fn} calls {c}, defined outside app/main and the BSPs and not reviewed: check it (tasks, cores, radio, card) and add it to KNOWN in tools/check/check_safe_mode.py")
    return probs, seen


def load(paths):
    """-> {(file, name): (is_static, body)}"""
    defs = {}
    for p in paths:
        rel = os.path.relpath(p, ROOT)
        for n, v in definitions(strip(open(p, errors="replace").read())).items(): defs[(rel, n)] = v
    return defs


def main():
    paths = []
    for d in SRC_DIRS:
        full = os.path.join(ROOT, d)
        paths += sorted(os.path.join(full, f) for f in os.listdir(full) if f.endswith(".c"))
    defs = load(paths)
    if ENTRY not in defs: print(f"check_safe_mode: FAIL {ENTRY[1]} not found in {ENTRY[0]}"); return 1
    if not re.search(r"core 1|second core|core_park", open(os.path.join(ROOT, ENTRY[0])).read()):
        print("check_safe_mode: FAIL guard.c lost the comment on why safe mode does not stall core 1"); return 1
    probs, seen = check(defs, ENTRY)
    if probs:
        print("check_safe_mode: FAIL"); [print("  " + x) for x in probs]; return 1
    print(f"check_safe_mode: ok ({len(seen)} functions on the safe-mode path, tasks on core 0 only, no core_park / audio / radio / USB / card)")
    return 0


def selftest():
    src = strip('''void guard_safe_mode(const char *why)
{
    helper(1); /* xTaskCreate(f, "c", 1, 0, 1, 0); in a comment */
    printf("audio_start(");
}
static int helper(int x)
{
    xTaskCreatePinnedToCore(f, "t", 4096, NULL, 5, NULL, 0);
    return 0;
}
esp_err_t bsp_display_start_ex(lv_disp_t **o, uint8_t b)
{
    acfg.task_core_id = 0;
    return esp_lv_adapter_init(&acfg);
}
''')
    d0 = definitions(src)
    assert set(d0) == {"guard_safe_mode", "helper", "bsp_display_start_ex"}, d0
    assert d0["helper"][0] and not d0["guard_safe_mode"][0]
    d = {("g.c", n): v for n, v in d0.items()}
    d[("other.c", "helper")] = (True, "esp_wifi_start();")          # another file's static of the same name: not followed
    E = ("g.c", "guard_safe_mode")
    def chk(m): return check(m, E)[0]
    def w(name, body): m = dict(d); m[("g.c", name)] = (d[("g.c", name)][0], body); return m
    assert chk(d) == [], chk(d)
    assert len(chk(w("helper", d[("g.c", "helper")][1].replace(", 0);", ", 1);")))) == 1
    assert len(chk(w("helper", "xTaskCreate(f, 0, 1, 0, 1, 0);"))) == 1
    assert len(chk(w("helper", "core_park_begin();"))) == 1
    assert len(chk(w("helper", "esp_bluedroid_enable(); audio_svc_start();"))) == 2
    assert "not reviewed" in chk(w("helper", "mystery_init();"))[0]
    m = w("guard_safe_mode", "bsp_display_start_ex(0, 1);"); m[("g.c", "bsp_display_start_ex")] = (False, "acfg.task_core_id = 1;")
    assert any("core 0" in x for x in chk(m))
    print("check_safe_mode: selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]: selftest(); sys.exit(0)
    sys.exit(main())
