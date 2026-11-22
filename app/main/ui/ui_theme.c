// SPDX-License-Identifier: LGPL-2.1-or-later
#include "esp_attr.h"
#include "ui_theme.h"
#include "i18n.h"
#include <math.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include "cfg.h"
#include "font.h"
#include "ui.h"
#include "ui_tokens.h"
#include "pc/pc_paths.h"
#include "psram_alloc.h"
#include "pc/pc_themefile.h"

static const pc_theme_t *s_cur;
static EXT_RAM_BSS_ATTR char s_nm[3][28];            /* a card theme's display names (name_tc / name_sc / name_en; PSRAM: 0 new internal statics); '_' shows as a space */
static lv_display_t *s_disp;
static EXT_RAM_BSS_ATTR pc_theme_t s_custom;   /* PSRAM: 0 new internal statics */            /* the theme of a card file (cfg theme=<name>): a copy of its base with the file's colours */
static EXT_RAM_BSS_ATTR char s_cname[24];
#define THEME_MAX 24576                  /* a theme file is read whole (PSRAM, boot only); the commented template is about 12 KB */

static const pc_theme_t *builtin(const char *name)
{
    for (int i = 0; name && i < pc_theme_count(); i++) if (!strcmp(pc_theme_at(i)->name, name)) return pc_theme_at(i);
    return NULL;
}
static uint32_t *core_slot(pc_theme_t *t, const char *k)
{
    static const char *const K[] = { "bg", "panel", "panel2", "fg", "dim", "a1", "a2", "a3", "on" };
    uint32_t *const v[] = { &t->bg, &t->panel, &t->panel2, &t->fg, &t->dim, &t->a1, &t->a2, &t->a3, &t->on };
    for (int i = 0; i < 9; i++) if (!strcmp(k, K[i])) return v[i];
    return NULL;
}
static int cb_base(void *c, const pc_themefile_kv_t *kv)
{
    if (strcmp(kv->key, "base")) return 0;                       /* pass 1 only looks for base; the rest is judged in pass 2 */
    const pc_theme_t *b = builtin(kv->val);
    if (!b) return 1;
    *(const pc_theme_t **)c = b;
    return 0;
}
static int cb_apply(void *c, const pc_themefile_kv_t *kv)
{
    uint32_t rgb, *slot;
    static const char *const NK[3] = { "name_tc", "name_sc", "name_en" };
    for (int i = 0; i < 3; i++) if (!strcmp(kv->key, NK[i])) {
        snprintf(s_nm[i], sizeof s_nm[i], "%s", kv->val);
        size_t n = strlen(s_nm[i]);
        if (n == sizeof s_nm[i] - 1 && ((unsigned char)kv->val[n] & 0xC0) == 0x80)   /* cut inside a UTF-8 character: drop it whole (back to its lead byte) */
            while (n > 0) { n--; if (((unsigned char)s_nm[i][n] & 0xC0) != 0x80) break; }
        s_nm[i][n] = 0;
        for (char *q = s_nm[i]; *q; q++) if (*q == '_') *q = ' ';
        return 0;
    }
    if (!strcmp(kv->key, "base")) return builtin(kv->val) ? 0 : 1;
    if (pc_themefile_rgb(kv->val, &rgb)) return 1;
    if ((slot = core_slot((pc_theme_t *)c, kv->key)) != NULL) { *slot = rgb; return 0; }
    return ui_tok_set_key(kv->key, rgb);
}
/* A theme by name: a built-in one, else /sdcard/_player/themes/<name>.txt on top of its `base` (default neon). A missing file or key falls back to the
 * built-in default, one log line, never a failure (pc_themefile.h for the syntax). */
static const pc_theme_t *load_theme(const char *name)
{
    ui_tok_clear();
    const pc_theme_t *b = builtin(name);
    if (b) return b;
    b = pc_theme_find(NULL);                                     /* the default */
    size_t nl = name ? strlen(name) : 0;
    bool ok = nl > 0 && nl < sizeof s_cname;
    for (size_t i = 0; ok && i < nl; i++) ok = (name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') || (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-';
    char *buf = NULL;
    size_t got = 0;
    if (ok) {
        char path[96];
        snprintf(path, sizeof path, PC_INT "/themes/%s.txt", name);
        FILE *f = fopen(path, "rb");
        if (f) {
            buf = psram_malloc(THEME_MAX + 1);
            if (buf) { got = fread(buf, 1, THEME_MAX, f); buf[got] = 0; }
            fclose(f);
        }
    }
    if (!buf) { printf("APP theme %s: no file, default theme\n", name ? name : "(null)"); return b; }
    const pc_theme_t *base = b;
    pc_themefile_parse(buf, got, cb_base, &base);
    s_custom = *base;
    memset(s_nm, 0, sizeof s_nm);
    snprintf(s_cname, sizeof s_cname, "%s", name);
    s_custom.name = s_custom.label = s_cname;
    int bad = pc_themefile_parse(buf, got, cb_apply, &s_custom);
    psram_free(buf);
    printf("APP theme %s: file loaded, %d bad line(s), %d token(s) set\n", name, bad, ui_tok_overrides());
    return &s_custom;
}

const pc_theme_t *ui_theme(void)
{
    if (!s_cur) s_cur = load_theme(cfg_str("theme", "neon"));
    return s_cur;
}

const char *ui_theme_label(void)
{
    const pc_theme_t *t = ui_theme();
    int l = (int)i18n_lang();
    return t == &s_custom && l >= 0 && l < 3 && s_nm[l][0] ? s_nm[l] : t->label;   /* a built-in: its one label */
}

bool ui_theme_is_light(void)
{
    return pc_theme_is_light(ui_theme());
}

static float rel_lum(lv_color_t c)   /* WCAG relative luminance, gamma 2.2 (the lint, tools/gen/gen_card_themes.py, uses the exact curve) */
{
    return 0.2126f * powf(c.red / 255.0f, 2.2f) + 0.7152f * powf(c.green / 255.0f, 2.2f) + 0.0722f * powf(c.blue / 255.0f, 2.2f);
}
static float contrast(lv_color_t a, lv_color_t b)
{
    float la = rel_lum(a) + 0.05f, lb = rel_lum(b) + 0.05f;
    return la > lb ? la / lb : lb / la;
}
lv_color_t ui_text_on(lv_color_t surface)
{
    lv_color_t f = UI_FG, b = UI_BG;
    const float cf = contrast(f, surface), cb = contrast(b, surface);
    const float best = cf >= cb ? cf : cb;
    if (best < 4.5f) {                                    /* a mid-tone fill that neither theme colour reads on well: black or white */
        lv_color_t k = contrast(lv_color_black(), surface) >= contrast(lv_color_white(), surface) ? lv_color_black() : lv_color_white();
        if (contrast(k, surface) > best) return k;
    }
    return cf >= cb ? f : b;
}
lv_color_t ui_ink_on(lv_color_t want, lv_color_t surface)
{
    return contrast(want, surface) >= 3.0f ? want : ui_text_on(surface);
}

lv_color_t ui_hue_on(lv_color_t want, lv_color_t surface)
{
    if (!ui_theme_is_light()) return want;                 /* a dark theme: unchanged (the fixed colours were tuned on dark) */
    for (int i = 0; i < 12 && contrast(want, surface) < 3.0f; i++) want = lv_color_mix(UI_FG, want, 48);   /* ink-darker toward the text colour, hue kept */
    return want;
}

lv_color_t ui_cell_used(void) { return ui_theme_is_light() ? lv_color_mix(UI_FG, UI_BG, 36) : UI_PANEL2; }
lv_color_t ui_cell_empty(void) { return ui_theme_is_light() ? lv_color_mix(UI_FG, UI_BG, 10) : UI_PANEL; }
lv_color_t ui_edge_on_page(void)
{
    lv_color_t e = UI_BG;
    if (!ui_theme_is_light()) return e;
    for (int i = 1; i <= 16 && contrast(e, UI_BG) < 3.0f; i++) e = lv_color_mix(UI_FG, UI_BG, (uint8_t)(i * 16));
    return e;
}
void ui_face_outline(lv_obj_t *o, int width)
{
    if (!ui_theme_is_light()) return;
    lv_obj_set_style_border_width(o, width, 0);
    lv_obj_set_style_border_color(o, ui_edge_on_page(), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
}

/* LVGL's light mode puts a grey drop shadow under every button; the dark look is flat, so a light theme is too (extension theme over the default one) */
static lv_theme_t *s_flat;                  /* made once (the type is opaque); lives for the whole run */
static void flat_apply(lv_theme_t *th, lv_obj_t *obj)
{
    (void)th;
    if (lv_obj_check_type(obj, &lv_button_class)) lv_obj_set_style_shadow_width(obj, 0, 0);
}

void ui_theme_apply(lv_display_t *disp)
{
    if (disp) s_disp = disp;
    if (!s_disp) return;
    lv_theme_t *th = lv_theme_default_init(s_disp, UI_A1, UI_A2, !ui_theme_is_light(), font_main());   /* a light theme: LVGL's light mode; the default look and every built-in are dark */
    if (!s_flat) s_flat = lv_theme_create();
    if (s_flat) {
        lv_theme_copy(s_flat, th);                                /* fonts, colours, display */
        lv_theme_set_parent(s_flat, th);
        lv_theme_set_apply_cb(s_flat, flat_apply);
        th = s_flat;
    }
    lv_display_set_theme(s_disp, th);
    lv_obj_set_style_bg_color(lv_screen_active(), UI_BG, 0);
}

void ui_theme_next(void) { ui_theme_step(1); }

/* The card themes (/sdcard/_player/themes/<name>.txt) a step can reach: valid names only, sorted, "template" excluded. */
#define CARD_MAX 16
static EXT_RAM_BSS_ATTR char s_cards[CARD_MAX][24];
static int card_scan(void)
{
    int n = 0;
    DIR *d = opendir(PC_INT "/themes");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < CARD_MAX) {
        size_t l = strlen(e->d_name);
        if (l < 5 || l - 4 >= sizeof s_cards[0] || strcmp(e->d_name + l - 4, ".txt")) continue;
        char nm[24];
        memcpy(nm, e->d_name, l - 4);
        nm[l - 4] = 0;
        bool ok = strcmp(nm, "template") != 0;
        for (const char *q = nm; ok && *q; q++) ok = (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '_' || *q == '-';
        if (!ok || builtin(nm)) continue;                  /* a file named like a built-in is never read (the built-in wins in load_theme) */
        int i = n++;
        while (i > 0 && strcmp(s_cards[i - 1], nm) > 0) { memmove(s_cards[i], s_cards[i - 1], sizeof s_cards[0]); i--; }   /* insertion sort: a stable order */
        memcpy(s_cards[i], nm, sizeof nm);
    }
    closedir(d);
    return n;
}
/* The 配色 row's order: the built-in themes, then the card themes by name. A step wraps and never drops the current one; a current theme that is in neither list counts as the last. */
void ui_theme_step(int d)
{
    const pc_theme_t *t = ui_theme();
    const int nb = pc_theme_count(), nc = card_scan(), n = nb + nc;
    int i = n - 1;
    for (int k = 0; k < nb; k++) if (pc_theme_at(k) == t) i = k;
    if (t == &s_custom) for (int k = 0; k < nc; k++) if (!strcmp(s_cards[k], s_cname)) i = nb + k;
    int j = ((i + d) % n + n) % n;
    if (j == i) return;
    const pc_theme_t *nt;
    if (j < nb) { nt = pc_theme_at(j); cfg_set_str("theme", nt->name); ui_tok_clear(); }   /* leaving a card theme: its token colours go with it */
    else { char nm[24]; memcpy(nm, s_cards[j - nb], sizeof nm); cfg_set_str("theme", nm); nt = load_theme(nm); }   /* load_theme clears the old tokens itself */
    s_cur = nt;
    ui_theme_apply(NULL);
    ui_rebuild_all();
}

pc_ramp_t ui_viz_ramp(void) { return pc_ramp_parse(cfg_str("viz_ramp", "theme")); }
void ui_viz_ramp_set(pc_ramp_t r) { cfg_set_str("viz_ramp", pc_ramp_name(r)); }
void ui_viz_ramp_next(void) { cfg_set_str("viz_ramp", pc_ramp_name((pc_ramp_t)((ui_viz_ramp() + 1) % PC_RAMP_COUNT))); }
pc_vd_map_t ui_viz_map(void) { return pc_vd_map_parse(cfg_str("viz_map", "height")); }
void ui_viz_map_set(pc_vd_map_t m) { cfg_set_str("viz_map", pc_vd_map_name(m)); }

/* Spectrum page settings (viz_style, viz_var_<kind>, viz_w, viz_floor). Old or garbled values read as the defaults. */
/* A stored "vu" (the VU kind was removed 2026-11-12) loads as bars with variant 0: it is rewritten once, here, so the rest of the page never sees it. Its own
 * remembered variant (viz_var_vu) is simply never read again; every other stored name keeps meaning what it did (the kinds are stored by name, not by number). */
pc_vd_kind_t ui_viz_kind(void)
{
    const char *s = cfg_str("viz_style", "bars");
    if (!strcmp(s, "vu")) { cfg_set_str("viz_style", "bars"); cfg_set_int("viz_var_bars", 0); return PC_VD_BARS; }
    return pc_vd_kind_parse(s);
}
void ui_viz_kind_set(pc_vd_kind_t k) { cfg_set_str("viz_style", pc_vd_kind_name(k)); }
static void var_key(pc_vd_kind_t k, char *b, size_t n) { snprintf(b, n, "viz_var_%s", pc_vd_kind_name(k)); }
int ui_viz_var(pc_vd_kind_t k)
{
    char key[24];
    var_key(k, key, sizeof key);
    int v = cfg_int(key, 0);
    return v >= 0 && v < pc_vd_variants(k) ? v : 0;
}
void ui_viz_var_set(pc_vd_kind_t k, int v)
{
    char key[24];
    int n = pc_vd_variants(k);
    var_key(k, key, sizeof key);
    cfg_set_int(key, n > 0 ? ((v % n) + n) % n : 0);
}
int ui_viz_bar_w(void)
{
    int w = cfg_int("viz_w", 7);
    return w == 5 || w == 7 || w == 9 ? w : 7;
}
void ui_viz_bar_w_set(int w) { if (w == 5 || w == 7 || w == 9) cfg_set_int("viz_w", w); }
void ui_viz_bar_w_next(void) { int w = ui_viz_bar_w(); cfg_set_int("viz_w", w == 5 ? 7 : w == 7 ? 9 : 5); }
bool ui_viz_peaks(void) { return cfg_int("viz_peaks", 1) != 0; }
void ui_viz_peaks_set(bool on) { cfg_set_int("viz_peaks", on ? 1 : 0); }
pc_vd_order_t ui_viz_order(void) { return pc_vd_order_parse(cfg_str("viz_order", "seq")); }
void ui_viz_order_set(pc_vd_order_t o) { cfg_set_str("viz_order", pc_vd_order_name(o)); }
int ui_viz_ring_gap(void)
{
    int g = cfg_int("viz_gap", 2);
    return g == 0 || g == 2 || g == 4 ? g : 2;
}
void ui_viz_ring_gap_set(int px) { if (px == 0 || px == 2 || px == 4) cfg_set_int("viz_gap", px); }
bool ui_viz_fall_full(void) { return PC_VD_ROUND_SCREEN && cfg_int("viz_fall", 0) == 1; }
void ui_viz_fall_full_set(bool full) { cfg_set_int("viz_fall", full ? 1 : 0); }
void ui_viz_floor_next(void) { int f = (int)ui_viz_floor_db(); cfg_set_int("viz_floor", f == -60 ? -48 : f == -48 ? -36 : -60); }
void ui_viz_floor_set(int db) { if (db == -60 || db == -48 || db == -36) cfg_set_int("viz_floor", db); }
bool ui_viz_zoom(void) { return cfg_int("viz_zoom", 1) != 0; }   /* the picture size setting: 1 = zoomed to 440 px (the default), 0 = the original size, no per-frame scaling; only the square board reads it (scr_viz.c) */
void ui_viz_zoom_set(bool on) { cfg_set_int("viz_zoom", on ? 1 : 0); }
float ui_viz_floor_db(void)
{
    int f = cfg_int("viz_floor", -48);
    return f == -60 || f == -48 || f == -36 ? (float)f : -48.0f;
}
const char *ui_viz_kind_label(pc_vd_kind_t k)
{
    switch (k) {
    case PC_VD_RING: return T(S_RING_2) /* Ring */;
    case PC_VD_SCOPE: return T(S_SCOPE) /* Waveform */;
    case PC_VD_FALL: return T(S_WATERFALL) /* Waterfall */;
    case PC_VD_PHASE: return T(S_PHASE) /* Phase */;
    case PC_VD_VOICES: return T(S_VOICES) /* Voices */;
    case PC_VD_ROWS: return T(S_VOICE_ROWS) /* Voice rows */;
    case PC_VD_ROLL: return T(S_PIANO_ROLL) /* Piano roll */;
    case PC_VD_LYRICS: return T(S_LYRICS) /* Lyrics */;
    default: return T(S_BARS_3) /* Bars */;
    }
}

const char *ui_ramp_label(pc_ramp_t r)
{
    static const char *const zh[] = { "跟隨配色", "火焰", "冰", "Winamp", "彩虹", "雙色", "單色" };
    static const char *const en[] = { "Theme", "Fire", "Ice", "Winamp", "Rainbow", "Duo", "Mono" };
    return r >= 0 && r < PC_RAMP_COUNT ? T_RAW(zh[r], en[r]) : "";
}

static const char *const STYLE[] = { "bubble", "ring" };   /* octagon dropped: its edges cut the labels (user 2026-11-09) */
int ui_menu_style(void)
{
    const char *v = cfg_str("ui_style", "ring");
    for (int i = 0; i < 2; i++) if (!strcmp(v, STYLE[i])) return i;
    return 1;
}
void ui_menu_style_next(void) { cfg_set_str("ui_style", STYLE[(ui_menu_style() + 1) % 2]); ui_rebuild_all(); }
const char *ui_menu_style_label(void)
{
    static const char *const zh[] = { "圓鈕", "分段環" }, *const en[] = { "Bubbles", "Ring" };
    return T_RAW(zh[ui_menu_style()], en[ui_menu_style()]);
}
