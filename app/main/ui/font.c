// SPDX-License-Identifier: LGPL-2.1-or-later
#include "font.h"
#include "ui_geom.h"
#include "pc/pc_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cfg.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "fontpak.h"
#include "memlog.h"
#include "pc/pc_fontpak.h"

/* ui-tc.ttf = Noto Sans TC subset, ui-sc.ttf = Noto Sans SC subset (only glyphs ui-tc.ttf lacks),
 * ui-ext.ttf = Hangul, symbols, emoji, kaomoji marks (tools/gen/make_font_subset.py, make_font_fallback.py).
 * Each comes from the card (/sdcard/_player/fonts) or the flash pack (/flashfont), spec 2026-11-11-flash-font-design.md. */
#define NFILES 4
#define ICON_FILE 3                       /* ui-icon.ttf: the icons as private-use glyphs (tools/gen/make_icons.py, app/main/ui/icons.def), last in the chain */
static const char *const NAMES[NFILES] = { "ui-tc.ttf", "ui-sc.ttf", "ui-ext.ttf", "ui-icon.ttf" };
static char s_path[NFILES][40];
static pc_font_src_t s_src[NFILES];
static const lv_font_t *s_main = &lv_font_montserrat_20, *s_large = &lv_font_montserrat_28;
static bool s_cjk;

/* A glyph no font of the chain has is recorded, not just drawn as LVGL's box. The chain ends in RAM copies of the built-in Montserrat
 * (same dsc and callbacks, so they draw exactly as the const ones) whose fallback is s_miss20 / s_miss28: a font that has nothing, records the code
 * point and returns false, which is what LVGL treats as "no glyph" (the box stays as before). Called from LVGL's draw and layout code (possibly from more than one thread: no lock, the store goes through a local index), never when every glyph is found: no cost per frame.
 * Invisible characters are not misses: U+00A0 (no-break space) answers a blank (nothing to draw) with the advance of the ASCII space of the font
 * being asked, zero-width characters (U+200B..200D, U+2060, U+FEFF, U+FE00..FE0F) answer a blank of advance 0; neither is counted or listed.
 * The tail font gets no size from LVGL, so there are two of them (20 and 28 px) and each one's dsc points at the variable that holds the head of its chain
 * (s_main / s_large), whose ASCII space is looked up when needed (the head always has it, so no recursion into s_miss).
 * RAM: 16 x 4 + 12 = 76 bytes of list and counters, 2 lv_font_t copies (about 36 each; s_miss20 / s_miss28 are const, in flash). */
#define MISS_N 16
static uint32_t s_miss_cp[MISS_N];      /* the FIRST MISS_N distinct code points since boot / fontmiss clear: never evicted, so nothing is printed twice */
static uint32_t s_miss_n;               /* entries used */
static uint32_t s_miss_total;           /* lookups that found no glyph (every redraw counts again) */
static uint32_t s_miss_more;            /* lookups of a further distinct code point once the list was full (not stored, not printed) */
static lv_font_t s_tail20, s_tail28;
static bool miss_glyph(const lv_font_t *f, lv_font_glyph_dsc_t *d, uint32_t letter, uint32_t next);
static const lv_font_t s_miss20 = { .get_glyph_dsc = miss_glyph, .dsc = &s_main };
static const lv_font_t s_miss28 = { .get_glyph_dsc = miss_glyph, .dsc = &s_large };

/* LVGL never draws these: line breaks, other controls, direction marks, the byte-order mark (zero-width ones are handled in miss_glyph) */
static bool undrawn(uint32_t c)
{
    return c < 0x20 || (c >= 0x7f && c < 0xa0) || (c >= 0x200e && c <= 0x200f) || (c >= 0x2028 && c <= 0x202e);
}

static bool zero_width(uint32_t c)
{
    return (c >= 0x200b && c <= 0x200d) || c == 0x2060 || c == 0xfeff || (c >= 0xfe00 && c <= 0xfe0f);
}

/* a glyph with no pixels: LVGL draws nothing for box_w == 0 (like the space) and moves on by adv */
static bool blank_glyph(lv_font_glyph_dsc_t *d, uint16_t adv)
{
    d->adv_w = adv;
    d->box_w = d->box_h = 0;
    d->ofs_x = d->ofs_y = 0;
    d->format = LV_FONT_GLYPH_FORMAT_NONE;
    d->is_placeholder = false;
    return true;
}

static bool miss_glyph(const lv_font_t *f, lv_font_glyph_dsc_t *d, uint32_t letter, uint32_t next)
{
    (void)next;
    if (zero_width(letter)) return blank_glyph(d, 0);
    if (letter == 0xa0) {
        const lv_font_t *head = *(const lv_font_t *const *)f->dsc;
        lv_font_glyph_dsc_t sp;
        return blank_glyph(d, lv_font_get_glyph_dsc(head, &sp, ' ', 0) ? sp.adv_w : (uint16_t)(head->line_height / 4));
    }
    if (undrawn(letter)) return false;
    if (s_miss_total < UINT32_MAX) s_miss_total++;
    for (uint32_t i = 0; i < s_miss_n; i++)
        if (s_miss_cp[i] == letter) return false;
    if (s_miss_n >= MISS_N) { if (s_miss_more < UINT32_MAX) s_miss_more++; return false; }
    uint32_t n = s_miss_n;                  /* one read, one store: a second thread can never index past the array */
    if (n >= MISS_N) return false;
    s_miss_cp[n] = letter;
    s_miss_n = n + 1;
    printf("APP font miss U+%04X\n", (unsigned)letter);
    return false;
}

static const lv_font_t *with_miss_tail(lv_font_t *copy, const lv_font_t *builtin, const lv_font_t *miss)
{
    *copy = *builtin;
    copy->fallback = miss;
    return copy;
}

void font_miss_print(void)
{
    printf("APP fontmiss total=%u list=", (unsigned)s_miss_total);
    if (!s_miss_n) printf("-");
    for (uint32_t i = 0; i < s_miss_n; i++) printf("%sU+%04X", i ? "," : "", (unsigned)s_miss_cp[i]);
    printf(" more=%u\n", (unsigned)s_miss_more);
}

int font_miss_get(uint32_t *cp, int max, uint32_t *total, uint32_t *more)
{
    int n = (int)s_miss_n < max ? (int)s_miss_n : max;
    for (int i = 0; i < n; i++) cp[i] = s_miss_cp[i];
    if (total) *total = s_miss_total;
    if (more) *more = s_miss_more;
    return n;
}

void font_miss_clear(void)
{
    s_miss_n = 0;
    s_miss_total = 0;
    s_miss_more = 0;
}

const lv_font_t *font_main(void) { return s_main; }
const lv_font_t *font_large(void) { return s_large; }
bool font_has_cjk(void) { return s_cjk; }

/* LVGL 9's FreeType opens the file itself by path (glyphs read on demand and cached); "/flashfont/..." is a
 * memory stream on the mapped partition (fontpak.c), anything else FreeType's stdio */
static const lv_font_t *load(const char *path, uint16_t px)
{
    esp_lv_adapter_ft_font_config_t c = { .name = path, .size = px, .style = ESP_LV_ADAPTER_FT_FONT_STYLE_NORMAL };
    esp_lv_adapter_ft_font_handle_t h = NULL;
    esp_err_t e = esp_lv_adapter_ft_font_init(&c, &h);
    if (e != ESP_OK || !h) { printf("APP font init %s %upx: %s\n", path, px, esp_err_to_name(e)); return NULL; }
    return esp_lv_adapter_ft_font_get(h);
}

/* A font upload goes to <name>.new and is swapped in here, before FreeType opens the file: overwriting the
 * open font while glyphs are read from it crashed the board mid-upload (2026-11-10). */
static void swap_new(const char *path)
{
    char nw[64];
    struct stat st;
    snprintf(nw, sizeof nw, "%s.new", path);
    if (stat(nw, &st) || st.st_size < 1024) return;
    remove(path);
    printf("APP font swap %s size=%ld rc=%d\n", path, (long)st.st_size, rename(nw, path));
}

#define HEAD 512
/* the card's copy: size and the first bytes (its sfnt table directory) */
static size_t tf_head(const char *path, uint8_t *head, uint32_t *size)
{
    struct stat st;
    if (!g_sd_ok || stat(path, &st) || st.st_size <= 0) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(head, 1, HEAD, f);
    fclose(f);
    *size = (uint32_t)st.st_size;
    return n;
}

static void choose(int i, int pref, bool card)   /* card false: the flash pack only, /sdcard is never touched */
{
    char tf[40];
    snprintf(tf, sizeof tf, PC_FONTS_DIR "/%s", NAMES[i]);
    uint8_t head[HEAD];
    uint32_t tsz = 0, fsz = 0;
    const uint8_t *fl = NULL;
    size_t n = card ? tf_head(tf, head, &tsz) : 0;
    bool tf_ok = n && pc_sfnt_dir_len(head, n) > 0;
    bool fl_ok = fontpak_get(NAMES[i], &fl, &fsz) && pc_sfnt_dir_len(fl, fsz < HEAD ? fsz : HEAD) > 0;
    bool same = tf_ok && fl_ok && pc_sfnt_same(head, tsz, fl, fsz, n < fsz ? n : fsz);
    s_src[i] = pc_font_pick(pref, tf_ok, fl_ok, same);
    if (s_src[i] == PC_FONT_TF) snprintf(s_path[i], sizeof s_path[i], "%s", tf);
    else if (s_src[i] == PC_FONT_FLASH) snprintf(s_path[i], sizeof s_path[i], FONTPAK_PREFIX "%s", NAMES[i]);
    else s_path[i][0] = 0;
    printf("APP font %s src=%s tf=%d flash=%d same=%d\n", NAMES[i], pc_font_src_name(s_src[i]), tf_ok, fl_ok, same);
}

static void init(bool card)
{
    int64_t t0 = esp_timer_get_time();
    memlog("font_before");
    for (int i = 0; card && g_sd_ok && i < NFILES; i++) {
        char p[40];
        snprintf(p, sizeof p, PC_FONTS_DIR "/%s", NAMES[i]);
        swap_new(p);
    }
    fontpak_init();
    int pref = card ? cfg_int("font_src", PC_FONT_PREF_AUTO) : PC_FONT_PREF_FLASH;   /* test switch: 1 = flash only, 2 = card only */
    for (int i = 0; i < NFILES; i++) choose(i, pref, card);
    if (!s_path[0][0]) { printf("APP font fallback reason=no_file\n"); return; }
    const lv_font_t *m = load(s_path[0], UI_FONT_MAIN_PX), *l = load(s_path[0], UI_FONT_LARGE_PX);
    if (!m || !l) { printf("APP font fallback reason=freetype\n"); return; }
    /* chain: ui-tc -> ui-sc -> ui-ext -> the built-in font (LVGL's symbols: play, check, ... inside CJK labels) */
    lv_font_t *tm = (lv_font_t *)m, *tl = (lv_font_t *)l;
    for (int i = 1; i < NFILES; i++) {
        if (!s_path[i][0]) continue;
        const lv_font_t *m2 = load(s_path[i], UI_FONT_MAIN_PX), *l2 = load(s_path[i], UI_FONT_LARGE_PX);
        if (m2) { tm->fallback = m2; tm = (lv_font_t *)m2; }
        if (l2) { tl->fallback = l2; tl = (lv_font_t *)l2; }
    }
    tm->fallback = with_miss_tail(&s_tail20, &lv_font_montserrat_20, &s_miss20);
    tl->fallback = with_miss_tail(&s_tail28, &lv_font_montserrat_28, &s_miss28);
    s_main = m;
    s_large = l;
    s_cjk = true;
    printf("APP font ok ttf=1 src=%s ms=%lld\n", pc_font_src_name(s_src[0]), (esp_timer_get_time() - t0) / 1000);
    memlog("font_after");
}
void font_init(void) { init(true); }
void font_init_flash(void) { init(false); }

/* the icon file alone at px (menu cells: bigger than the text); eight sizes are kept. NULL when the file is not there (callers then show the name) */
const lv_font_t *font_icon(int px)
{
    static EXT_RAM_BSS_ATTR struct { int px; const lv_font_t *f; } c[8];   /* FIX4 M4: PSRAM, not internal .bss; 8 sizes (the square now-playing page adds 64 / 52 / 32) */
    if (!s_cjk || !s_path[ICON_FILE][0]) return NULL;
    for (int i = 0; i < 8; i++) if (c[i].px == px) return c[i].f;
    for (int i = 0; i < 8; i++) if (!c[i].px) { c[i].px = px; c[i].f = load(s_path[ICON_FILE], (uint16_t)px); return c[i].f; }
    return NULL;
}
/* FIX4 M2: present AND loadable: a file FreeType cannot open counts as no icon file, so the names stay (probed once at the menu-cell size) */
bool font_has_icons(void)
{
    static EXT_RAM_BSS_ATTR int ok;   /* 0 not probed, 1 loads, 2 does not */
    if (!s_cjk || !s_path[ICON_FILE][0]) return false;
    if (!ok) ok = font_icon(40) ? 1 : 2;
    return ok == 1;
}

const lv_font_t *font_clock(void)
{
    static const lv_font_t *f;
    if (!f) f = s_cjk ? load(s_path[0], 72) : NULL;
    return f ? f : &lv_font_montserrat_28;
}

const lv_font_t *font_huge(void)
{
    static const lv_font_t *f;
    if (!f) f = s_cjk ? load(s_path[0], 150) : NULL;
    return f ? f : font_clock();
}

void font_info(void)
{
    for (int i = 0; i < NFILES; i++) printf("APP fontinfo %s src=%s path=%s\n", NAMES[i], pc_font_src_name(s_src[i]), s_path[i]);
    printf("APP fontinfo cjk=%d pref=%d\n", s_cjk, cfg_int("font_src", 0));
}

/* test aid: cold glyph cost, card vs flash. A new pixel size per call (cold glyph cache): px = 21..99.
 * Glyphs = the CJK characters of the strings below that the font has. LVGL context. */
#include "font_bench.inc"

static uint32_t utf8_next(const char **pp)
{
    const uint8_t *p = (const uint8_t *)*pp;
    uint32_t c = *p++;
    int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (more) c &= 0x3f >> more;
    while (more-- && (*p & 0xc0) == 0x80) c = c << 6 | (*p++ & 0x3f);
    *pp = (const char *)p;
    return c;
}

void font_bench(int px)
{
    for (int i = 0; i < 2; i++) {
        bool flash = i == 1;
        char path[40];
        struct stat st;
        if (flash) {
            const uint8_t *d; uint32_t n;
            if (!fontpak_get("ui-tc.ttf", &d, &n)) { printf("APP fontbench flash none\n"); continue; }
            snprintf(path, sizeof path, FONTPAK_PREFIX "ui-tc.ttf");
        } else {
            snprintf(path, sizeof path, PC_FONTS_DIR "/ui-tc.ttf");
            if (!g_sd_ok || stat(path, &st)) { printf("APP fontbench tf none\n"); continue; }
        }
        size_t ib = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int64_t t0 = esp_timer_get_time();
        esp_lv_adapter_ft_font_config_t c = { .name = path, .size = (uint16_t)px, .style = ESP_LV_ADAPTER_FT_FONT_STYLE_NORMAL };
        esp_lv_adapter_ft_font_handle_t h = NULL;
        if (esp_lv_adapter_ft_font_init(&c, &h) != ESP_OK || !h) { printf("APP fontbench %s open failed\n", path); continue; }
        const lv_font_t *f = esp_lv_adapter_ft_font_get(h);
        int64_t t1 = esp_timer_get_time();
        int found = 0, n = 0;
        int64_t worst = 0;
        size_t imin = ib;
        for (const char *p = BENCH; *p;) {
            uint32_t cp = utf8_next(&p);
            n++;
            int64_t g0 = esp_timer_get_time();
            lv_font_glyph_dsc_t d;
            memset(&d, 0, sizeof d);
            if (lv_font_get_glyph_dsc(f, &d, cp, 0)) {
                if (lv_font_get_glyph_bitmap(&d, NULL)) found++;
                lv_font_glyph_release_draw_data(&d);
            }
            int64_t g = esp_timer_get_time() - g0;
            if (g > worst) worst = g;
            size_t fr = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            if (fr < imin) imin = fr;
        }
        int64_t t2 = esp_timer_get_time(), t3 = t2;
        /* warm: 100 glyphs (fewer than the 256-glyph cache), loaded once, then timed a second time */
        for (int pass = 0; pass < 2; pass++) {
            if (pass) t3 = esp_timer_get_time();
            int k = 0;
            for (const char *p = BENCH; *p && k < 100; k++) {
                uint32_t cp = utf8_next(&p);
                lv_font_glyph_dsc_t d;
                memset(&d, 0, sizeof d);
                if (lv_font_get_glyph_dsc(f, &d, cp, 0)) { lv_font_get_glyph_bitmap(&d, NULL); lv_font_glyph_release_draw_data(&d); }
            }
        }
        int64_t t4 = esp_timer_get_time();
        printf("APP fontbench src=%s px=%d open_us=%lld chars=%d glyphs=%d cold_us=%lld cold_per_glyph_us=%lld worst_us=%lld warm100_us=%lld int_drop=%d\n",
               flash ? "flash" : "tf", px, t1 - t0, n, found, t2 - t1, found ? (t2 - t1) / found : 0, worst, t4 - t3, (int)(ib - imin));
        esp_lv_adapter_ft_font_deinit(h);
    }
}

/* ---- UNSCII 8 at N times the size, pixel for pixel (the page bubble x2, the quick-settings numbers x3): every glyph of lv_font_unscii_8
 * with each pixel drawn as N x N, hard edges (not UNSCII 16, which is a different drawing). Metrics scaled; the bitmap is unpacked from UNSCII 8 into a small buffer and
 * enlarged into LVGL's A8 buffer. The scale lives in the font's user_data. ---- */
static bool xn_dsc(const lv_font_t *f, lv_font_glyph_dsc_t *d, uint32_t letter, uint32_t next)
{
    const lv_font_t *b = &lv_font_unscii_8;
    const int n = (int)(intptr_t)f->user_data;
    if (!b->get_glyph_dsc(b, d, letter, next)) return false;
    d->adv_w *= n; d->box_w *= n; d->box_h *= n; d->ofs_x *= n; d->ofs_y *= n;
    d->resolved_font = f;
    return true;
}
static const void *xn_bitmap(lv_font_glyph_dsc_t *g, lv_draw_buf_t *buf)
{
    static EXT_RAM_BSS_ATTR uint8_t src[16 * 16];   /* one UNSCII 8 glyph unpacked to A8 (8 x 8) */
    const int n = (int)(intptr_t)g->resolved_font->user_data;
    if (!buf || !buf->data || g->req_raw_bitmap || n < 1) return NULL;
    lv_font_glyph_dsc_t s = *g;
    s.adv_w /= n; s.box_w /= n; s.box_h /= n; s.ofs_x /= n; s.ofs_y /= n;
    s.resolved_font = &lv_font_unscii_8;
    uint32_t si = lv_draw_buf_width_to_stride(s.box_w, LV_COLOR_FORMAT_A8);
    if (s.box_w == 0 || s.box_h == 0 || si * s.box_h > sizeof src) return NULL;
    lv_draw_buf_t tmp = *buf;
    tmp.data = src;
    if (!lv_font_unscii_8.get_glyph_bitmap(&s, &tmp)) return NULL;
    uint32_t so = lv_draw_buf_width_to_stride(g->box_w, LV_COLOR_FORMAT_A8);
    uint8_t *o = buf->data;
    for (int y = 0; y < g->box_h; y++)
        for (int x = 0; x < g->box_w; x++) o[y * so + x] = src[(y / n) * si + x / n];
    return buf;
}
const lv_font_t *font_unscii8_xn(int n)
{
    static EXT_RAM_BSS_ATTR lv_font_t f[4];        /* x1 .. x4 */
    if (n < 1) n = 1;
    if (n > 4) n = 4;
    lv_font_t *p = &f[n - 1];
    if (!p->get_glyph_dsc) {
        p->line_height = lv_font_unscii_8.line_height * n;
        p->base_line = lv_font_unscii_8.base_line * n;
        p->underline_position = lv_font_unscii_8.underline_position * n;
        p->underline_thickness = lv_font_unscii_8.underline_thickness * n;
        p->user_data = (void *)(intptr_t)n;
        p->get_glyph_bitmap = xn_bitmap;
        p->get_glyph_dsc = xn_dsc;
    }
    return p;
}
