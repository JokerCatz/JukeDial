// SPDX-License-Identifier: LGPL-2.1-or-later
#include "cfg.h"
#include "pc/pc_paths.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_attr.h"
#include "esp_timer.h"
#include "ui/i18n.h"
#include "pc/pc_rotate.h"
#include "pc/pc_chipseq.h"
#include "pc/pc_backend.h"
#include "cpu_clock.h"
#include "lab.h"

EXT_RAM_BSS_ATTR pc_kv_t g_cfg[CFG_MAX];   /* 288 B per pair: 36 KB, PSRAM */
int g_cfg_n;
bool g_sd_ok;
static int64_t s_dirty_at;

/* Amiga tone: the backend takes the setting at once (not behind Experimental formats since 2026-11-18) */
static void amiga_sync(void) { pc_backend_xmp_set_amiga(cfg_int("amiga_mix", 0)); }
static bool exp_fmt_on(void) { return cfg_int("exp_fmt", 0) != 0; }   /* format tiers: the setting 實驗性格式, read at every scan / open */

void cfg_load(void)
{
    pc_tier_set_source(exp_fmt_on);
    g_cfg_n = g_sd_ok ? pc_kv_load(CFG_PATH, g_cfg, CFG_MAX) : 0;
    amiga_sync();
    printf("APP cfg keys=%d\n", g_cfg_n);
    lab_load();                                       /* LAB: the test-day knobs (a crash boot drops them first) */
    i18n_load();                                      /* the UI language byte follows the file */
}

static int split_urls(char *buf, const char *out[CFG_SETUP_URL_MAX])
{
    int k = 0;
    for (char *p = buf; *p && k < CFG_SETUP_URL_MAX;) {
        while (*p == ' ' || *p == ',' || *p == ';' || *p == '\t') p++;
        if (!*p) break;
        out[k++] = p;
        while (*p && *p != ' ' && *p != ',' && *p != ';' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return k;
}

int cfg_setup_urls(char *buf, size_t n, const char *out[CFG_SETUP_URL_MAX])
{
    snprintf(buf, n, "%s", cfg_str("setup_url", CFG_SETUP_URL_DEF));
    int k = split_urls(buf, out);
    if (!k) { snprintf(buf, n, "%s", CFG_SETUP_URL_DEF); k = split_urls(buf, out); }   /* an empty value = the default list (itself a list: GitHub, Gitee) */
    return k;
}

int cfg_rotation(void) { return pc_rotate_norm(cfg_int("rotate", 0)); }
int cfg_brightness(void) { return cfg_int("brightness", 60); }

bool cfg_has(const char *key) { return pc_kv_get(g_cfg, g_cfg_n, key) != NULL; }

int cfg_int(const char *key, int def) { return pc_kv_get_int(g_cfg, g_cfg_n, key, def); }

const char *cfg_str(const char *key, const char *def)
{
    const char *v = pc_kv_get(g_cfg, g_cfg_n, key);
    return v ? v : def;
}

void cfg_set_str(const char *key, const char *v)
{
    char copy[sizeof g_cfg[0].val];            /* v may point into g_cfg itself (cfg_str): no overlapping copy */
    snprintf(copy, sizeof copy, "%s", v ? v : "");
    if (pc_kv_set(g_cfg, &g_cfg_n, CFG_MAX, key, copy) < 0) printf("APP cfg full key=%s\n", key);   /* the table is full: this change does not stick */
    if (!strcmp(key, "amiga_mix")) amiga_sync();
    else if (!strcmp(key, "cpu_mhz")) cpu_clock_apply();      /* the experimental CPU clock: applies at once (after boot only) */
    cfg_save_soon();
}

void cfg_set_int(const char *key, int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    cfg_set_str(key, b);
}

void cfg_del(const char *key)
{
    for (int i = 0; i < g_cfg_n; i++)
        if (!strcmp(g_cfg[i].key, key)) {
            for (int j = i + 1; j < g_cfg_n; j++) g_cfg[j - 1] = g_cfg[j];
            g_cfg_n--;
            cfg_save_soon();
            return;
        }
}

void cfg_save_soon(void) { s_dirty_at = esp_timer_get_time(); }

int cfg_save_now(void)
{
    s_dirty_at = 0;
    if (!g_sd_ok) return -1;
    pc_int_mkdir(PC_CFG_DIR);
    int r = pc_kv_save(CFG_PATH, g_cfg, g_cfg_n);
    printf("APP cfg saved rc=%d\n", r);
    return r;
}

void cfg_tick(void)
{
    if (!s_dirty_at || esp_timer_get_time() - s_dirty_at < 3000000) return;
    cfg_save_now();
}

/* app_volume() / app_set_volume(): vol_out.c (BTVOL: the current output's volume) */

bool app_risky_enabled(void)
{
    struct stat st;
    return g_sd_ok && stat(PC_FLAG_RISKY, &st) == 0;
}

int cfg_haptic_mv(void) { return cfg_int("haptic_on", 1) ? cfg_int("haptic_mv", 1280) : 0; }
