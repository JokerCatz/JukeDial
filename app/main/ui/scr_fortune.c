// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fortune (spec an internal note): today's omikuji, moon blocks,
 * wheel, song gacha, tarot (the dice moved to tools, scr_tools.c). Logic in pc_fortune (host-tested), texts in fortune_text.c (flash).
 * Internal RAM: this file's statics are .bss (PSRAM via linker.lf); TF files and gacha tables are read into
 * PSRAM and freed when the screen goes. Graphics are LVGL objects and arcs, no bitmaps. Each screen's animation
 * is one lv_timer, deleted in destroy and idle while the backlight is off. */
#include <math.h>
#include "pc/pc_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "pc/pc_fortune.h"
#include "time/clock.h"
#include "cfg.h"
#include "clock_svc.h"
#include "font.h"
#include "icons_gen.h"
#include "fortune_text.h"
#include "library_svc.h"
#include "mp3_ctl.h"
#include "screens.h"
#include "sfx.h"
#include "stats_svc.h"
#include "thumb_svc.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "ui_kit.h"

#define FORTUNE_DIR PC_FORTUNE_DIR
#define FRAME_MS 33
#define Z ui_scale
#define BZ ui_scale_big     /* the centre objects of the square panel (SQ2): larger than the rest of the page */

/* ---- shared helpers ---- */
static lv_timer_t *s_anim;
static int s_frame;
static void (*s_action)(void);         /* the top fortune screen's main action (console "fortune draw") */

static void anim_stop(void) { if (s_anim) { lv_timer_delete(s_anim); s_anim = NULL; } }
static void anim_start(void (*step)(void))
{
    anim_stop();
    s_frame = 0;
    s_anim = lv_timer_create((lv_timer_cb_t)(void *)step, FRAME_MS, NULL);
}

static lv_obj_t *box(lv_obj_t *p, int w, int h, lv_color_t c, int radius)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* today's local date when the clock is synced: the same seed all day */
static bool day_seed(uint32_t salt, uint64_t *seed)
{
    if (!clock_time_valid()) return false;
    clock_civil_t v;
    clock_civil_from_utc(clock_now_utc(), clock_tz_min(), &v);
    *seed = pc_fortune_day_seed(v.year, v.month, v.day, salt);
    return true;
}

static char *read_text(const char *path, size_t max)   /* PSRAM, NUL-terminated; NULL when missing */
{
    FILE *f = g_sd_ok ? fopen(path, "rb") : NULL;
    if (!f) return NULL;
    char *b = heap_caps_malloc(max + 1, MALLOC_CAP_SPIRAM);
    size_t n = b ? fread(b, 1, max, f) : 0;
    fclose(f);
    if (b) b[n] = 0;
    return b;
}

/* ================= today's omikuji ================= */
/* /sdcard/fortune/omikuji.txt, one slip a line: "<rank>|<verse>", rank 大吉 吉 中吉 小吉 末吉 凶 (great luck ... bad luck) or * (any);
 * a line without '|' is a verse for any rank; '/' in a verse breaks the line; '#' lines are comments. */
#define KUJI_MAX 64
static char *s_kbuf;
static char *s_ktxt[KUJI_MAX];
static int8_t s_krank[KUJI_MAX];
static int s_kn;                        /* override verses; 0 = built-in */
static lv_obj_t *s_kcan, *s_kstick, *s_krank_l, *s_kverse, *s_kasp, *s_khint;
static int s_kshake;
static pc_omikuji_t s_kres;
#define KUJI_COL ui_pal(UI_PAL_kuji)

static void kuji_load(void)
{
    s_kn = 0;
    s_kbuf = read_text(FORTUNE_DIR "/omikuji.txt", 16 * 1024);
    if (!s_kbuf) return;
    char *lines[KUJI_MAX];
    int n = pc_fortune_lines(s_kbuf, lines, KUJI_MAX, 240);
    for (int i = 0; i < n; i++) {
        char *bar = strchr(lines[i], '|'), *v = lines[i];
        int r = -1;
        if (bar) { *bar = 0; r = pc_kuji_parse_rank(lines[i]); v = bar + 1; if (r == -2) { *bar = '|'; v = lines[i]; r = -1; } }
        while (*v == ' ') v++;
        if (!*v) continue;
        for (char *c = v; *c; c++) if (*c == '/') *c = '\n';
        s_ktxt[s_kn] = v;
        s_krank[s_kn++] = (int8_t)r;
    }
    printf("APP fortune omikuji.txt verses=%d\n", s_kn);
}

static void stars(char *b, size_t cap, int n)
{
    b[0] = 0;
    for (int i = 0; i < 5; i++) strlcat(b, i < n ? T(S_STR_2) /* ★ */ : T(S_STR_3) /* ☆ */, cap);
}

static void kuji_show(void)
{
    lv_obj_add_flag(s_kcan, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kstick, LV_OBJ_FLAG_HIDDEN);
    int r = s_kres.rank;
    lv_label_set_text(s_krank_l, T_RAW(fortune_kuji_zh[r], fortune_kuji_en[r]));
    lv_obj_set_style_text_color(s_krank_l, ui_c(KUJI_COL[r]), 0);
    const char *v = s_kres.verse < 0 ? "" : s_kn ? s_ktxt[s_kres.verse] : T_RAW(fortune_verses[s_kres.verse].zh, fortune_verses[s_kres.verse].en);
    lv_label_set_text(s_kverse, v);
    char b[200] = "", st[32];
    for (int a = 0; a < PC_KUJI_ASPECTS; a++) {
        stars(st, sizeof st, s_kres.stars[a]);
        char ln[64];
        snprintf(ln, sizeof ln, "%s%s  %s", a ? "\n" : "", T_RAW(fortune_aspect_zh[a], fortune_aspect_en[a]), st);
        strlcat(b, ln, sizeof b);
    }
    lv_label_set_text(s_kasp, b);
    lv_obj_remove_flag(s_kverse, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_kasp, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_khint, clock_time_valid() ? T(S_TODAY_S_SLIP_SAME_ALL_DAY) /* Today's slip: the same all day */ : T(S_NOT_SYNCED_RANDOM_EACH_TIME) /* Time not set: random each time */);
    lv_obj_align(s_khint, LV_ALIGN_CENTER, 0, Z(108));
    printf("APP fortune kuji rank=%d verse=%d stars=%d%d%d%d\n", r, s_kres.verse, s_kres.stars[0], s_kres.stars[1], s_kres.stars[2], s_kres.stars[3]);
}

static void kuji_step(void)
{
    if (!ui_screen_on()) return;
    int f = s_frame++;
    if (f < 24) {                                        /* shake the box */
        lv_obj_align(s_kcan, LV_ALIGN_CENTER, (int32_t)lroundf((float)Z(14) * sinf((float)f * 1.3f)), Z(10));
        lv_obj_align_to(s_kstick, s_kcan, LV_ALIGN_OUT_TOP_MID, 0, BZ(30));
        if (f % 4 == 0) ui_haptic_tick();
    } else if (f < 40) {                                 /* one slip slides out */
        lv_obj_align(s_kcan, LV_ALIGN_CENTER, 0, BZ(10));
        lv_obj_remove_flag(s_kstick, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align_to(s_kstick, s_kcan, LV_ALIGN_OUT_TOP_MID, 0, BZ(30 - (f - 24) * 5));
        lv_obj_set_style_bg_color(s_kstick, ui_c(KUJI_COL[s_kres.rank]), 0);
    } else {
        anim_stop();
        ui_haptic_tap();
        kuji_show();
    }
}

static void kuji_draw(void)
{
    if (s_anim) return;
    uint64_t seed;
    pc_rng_t r;
    pc_rng_seed(&r, day_seed(PC_SALT_OMIKUJI, &seed) ? seed : ((uint64_t)esp_random() << 32 | esp_random()));
    if (s_kn) pc_omikuji_draw(&r, s_krank, s_kn, &s_kres);
    else {
        int8_t vr[64];
        int n = fortune_nverses < 64 ? fortune_nverses : 64;
        for (int i = 0; i < n; i++) vr[i] = fortune_verses[i].rank;
        pc_omikuji_draw(&r, vr, n, &s_kres);
    }
    s_kshake = 0;
    lv_label_set_text(s_krank_l, T(S_TODAY_S_SLIP) /* Today's slip */);
    lv_obj_set_style_text_color(s_krank_l, UI_FG, 0);
    lv_obj_add_flag(s_kverse, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kasp, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kstick, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_kcan, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_khint, T(S_SHAKING) /* Shaking... */);
    anim_start(kuji_step);
}
static void kuji_tap(void) { kuji_draw(); }
static bool kuji_knob(int d)
{
    (void)d;
    if (s_anim) return true;
    s_kshake++;                                          /* each detent shakes the box; six draw */
    lv_obj_align(s_kcan, LV_ALIGN_CENTER, BZ(s_kshake & 1 ? 10 : -10), BZ(10));
    if (s_kshake >= 6) kuji_draw();
    return true;
}

static void kuji_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_timer_guard(p, &s_anim);
    kuji_load();
    s_kcan = box(p, BZ(84), BZ(120), UI_A2, 14);                 /* the slip box: a tube with slips in it */
    lv_obj_set_style_border_width(s_kcan, 3, 0);
    lv_obj_set_style_border_color(s_kcan, UI_FG, 0);
    lv_obj_align(s_kcan, LV_ALIGN_CENTER, 0, BZ(10));
    for (int i = 0; i < 5; i++) {
        lv_obj_t *s = box(s_kcan, BZ(6), BZ(30), ui_palc(UI_PAL_kuji_stick, 0), 2);
        lv_obj_align(s, LV_ALIGN_TOP_MID, BZ(-24 + i * 12), BZ(-6 - (i % 2) * 6));
    }
    lv_obj_t *t = ui_label(s_kcan, font_large(), BZ(14), BZ(70));
    lv_label_set_text(t, T(S_STR_4) /* Slip */);
    lv_obj_set_style_text_color(t, UI_BG, 0);
    s_kstick = box(p, BZ(14), BZ(90), ui_palc(UI_PAL_kuji_stick, 0), 3);
    lv_obj_add_flag(s_kstick, LV_OBJ_FLAG_HIDDEN);
    s_krank_l = ui_label_d(p, font_large(), -112, 240);
    lv_label_set_text(s_krank_l, T(S_TODAY_S_SLIP) /* Today's slip */);
    s_kverse = ui_label_d(p, NULL, -50, 280);
    s_kasp = ui_label_d(p, NULL, 40, 220);
    ui_text_airy(s_kverse);
    lv_obj_set_style_text_line_space(s_kasp, -2, 0);
    ui_text_airy(s_kasp);
    lv_obj_add_flag(s_kverse, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kasp, LV_OBJ_FLAG_HIDDEN);
    s_khint = ui_label(p, NULL, Z(96) + BZ(32) - Z(32), Z(280));      /* under the larger slip box */
    lv_obj_set_style_text_color(s_khint, UI_DIM, 0);
    if (clock_time_valid()) lv_label_set_text_fmt(s_khint, T(S_TAP_OR_TURN_TO_SHAKE) /* Tap or turn %s to shake */, ui_word_knob());
    else lv_label_set_text(s_khint, T(S_TAP_TO_SHAKE_NOT_SYNCED_RANDOM) /* Tap to shake (time not set: random) */);
    ui_back_button(p);
    ui_page_tap(p, kuji_tap);
    s_action = kuji_draw;
}
static void kuji_destroy(void)
{
    anim_stop();
    if (s_kbuf) heap_caps_free(s_kbuf);
    s_kbuf = NULL;
    s_kn = 0;
    s_action = NULL;
}
static const ui_screen_t scr_kuji = { .name = "fortune_kuji", .create = kuji_create, .destroy = kuji_destroy, .on_knob = kuji_knob };

/* ================= moon blocks ================= */
static lv_obj_t *s_jb[2], *s_jres, *s_jsub, *s_jstreak;
static pc_jiao_throw_t s_jt;
static bool s_jb_flat[2];
static int s_jstreak_n, s_jcount[3];

static void jiao_face(int i, bool flat_up)   /* flat side up: the chord on top (lower half-disc); round side up: upper half */
{
    s_jb_flat[i] = flat_up;
    lv_arc_set_bg_angles(s_jb[i], flat_up ? 0 : 180, flat_up ? 180 : 360);
    lv_obj_set_style_arc_color(s_jb[i], ui_c(ui_pal(UI_PAL_jiao)[flat_up ? 0 : 1]), 0);
}

static void jiao_pos(int i, int y)   /* y: where a flat-up block's centre is; a round-up block rests 52 px lower */
{
    lv_obj_align(s_jb[i], LV_ALIGN_CENTER, BZ(i ? 62 : -62), y + (s_jb_flat[i] ? 0 : BZ(52)));
}

static void jiao_show(void)
{
    static const char *const NZ[3] = { "聖筊", "笑筊", "陰筊" }, *const NE[3] = { "Yes (sheng)", "Laugh (xiao)", "No (yin)" };
    static const char *const MZ[3] = { "一平一凸：神明應允", "兩面朝上：神明笑了，再想想", "兩凸朝上：暫時不宜" };
    static const char *const ME[3] = { "One up, one down: yes", "Both flat up: ask again", "Both round up: not now" };
    int r = s_jt.result;
    s_jcount[r]++;
    s_jstreak_n = pc_jiao_streak(s_jstreak_n, (pc_jiao_t)r);
    lv_label_set_text(s_jres, T_RAW(NZ[r], NE[r]));
    lv_obj_set_style_text_color(s_jres, r == PC_JIAO_SHENG ? UI_GOLD : UI_FG, 0);
    lv_label_set_text(s_jsub, T_RAW(MZ[r], ME[r]));
    char b[96];
    if (s_jstreak_n >= 3) snprintf(b, sizeof b, "%s", T(S_THREE_YES_IN_A_ROW) /* Three holy blocks in a row! Wish granted */);
    else if (s_jstreak_n >= 2) snprintf(b, sizeof b, T(S_D_YES_IN_A_ROW) /* %d holy blocks in a row */, s_jstreak_n);
    else snprintf(b, sizeof b, T(S_YES_D_LAUGH_D_NO_D) /* Holy %d · Laugh %d · Yin %d */, s_jcount[0], s_jcount[1], s_jcount[2]);
    lv_label_set_text(s_jstreak, b);
    lv_obj_set_style_text_color(s_jstreak, s_jstreak_n >= 3 ? UI_GOLD : UI_DIM, 0);
    static const char *const SND[3] = { "jiao_sheng", "jiao_xiao", "jiao_yin" };   /* each result its own sound (sfx bank) */
    if (s_jstreak_n >= 3) { ui_haptic_tap(); sfx_play("win"); s_jstreak_n = 0; }   /* celebrated: the next throw starts a new run */
    else sfx_play(SND[r]);
    printf("APP fortune jiao result=%d flat=%d%d streak=%d\n", r, s_jt.flat_up[0], s_jt.flat_up[1], s_jstreak_n);
}

static void jiao_step(void)
{
    if (!ui_screen_on()) return;
    int f = s_frame++;
    for (int i = 0; i < 2; i++) {
        int land = 14 + 3 * i;                           /* the second block lands a moment later */
        int y;
        if (f < land) { float t = (float)f / (float)land; y = (int)(BZ(-150) + BZ(158) * t * t); jiao_face(i, (f / 2 + i) & 1); }
        else if (f < land + 4) { y = BZ(8) - (int)(BZ(10) * sinf((float)(f - land) * 0.785f)); jiao_face(i, s_jt.flat_up[i]); }   /* a small bounce */
        else { y = BZ(8); jiao_face(i, s_jt.flat_up[i]); }
        jiao_pos(i, y);
        if (f == land) { ui_haptic_tap(); sfx_play("clack"); }   /* each block lands with a clack */
    }
    if (f >= 24) { anim_stop(); jiao_show(); }
}

static void jiao_throw(void)
{
    if (s_anim) return;
    pc_jiao_throw(ui_rng(), &s_jt);
    lv_label_set_text(s_jres, "");
    lv_label_set_text(s_jsub, "");
    anim_start(jiao_step);
}
static void jiao_tap(void) { jiao_throw(); }
static bool jiao_knob(int d) { (void)d; jiao_throw(); return true; }

static void jiao_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_timer_guard(p, &s_anim);
    s_jstreak_n = 0;
    memset(s_jcount, 0, sizeof s_jcount);
    lv_obj_t *floor_ = box(p, BZ(240), 4, UI_PANEL2, 2);
    lv_obj_align(floor_, LV_ALIGN_CENTER, 0, BZ(62));
    for (int i = 0; i < 2; i++) {
        lv_obj_t *a = lv_arc_create(p);
        lv_obj_set_size(a, BZ(104), BZ(104));
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(a, BZ(52), LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
        lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
        s_jb[i] = a;
        jiao_face(i, i == 0);
        jiao_pos(i, BZ(8));
    }
    lv_obj_t *t = ui_label_d(p, font_large(), -120, 240);
    lv_label_set_text(t, T(S_MOON_BLOCKS) /* Moon blocks */);
    s_jres = ui_label_d(p, font_large(), -70, 260);
    s_jsub = ui_label(p, NULL, Z(86) + BZ(62) - Z(62), Z(280));      /* under the floor, whose line the larger blocks moved down */
    s_jstreak = ui_label(p, NULL, Z(112) + BZ(62) - Z(62), Z(280));
    ui_text_airy(s_jsub);
    ui_text_airy(s_jstreak);
    lv_label_set_text(s_jres, "");
    lv_label_set_text(s_jsub, T(S_THINK_OF_A_QUESTION_TAP_TO_THROW) /* Think of a question, tap to throw */);
    lv_label_set_text(s_jstreak, "");
    ui_back_button(p);
    ui_page_tap(p, jiao_tap);
    s_action = jiao_throw;
}
static void jiao_destroy(void) { anim_stop(); s_action = NULL; }
static const ui_screen_t scr_jiao = { .name = "fortune_jiao", .create = jiao_create, .destroy = jiao_destroy, .on_knob = jiao_knob };

/* dice moved to tools (scr_tools.c, 2026-11-11) */

/* ================= the wheel ================= */
/* /sdcard/fortune/wheel.txt: one choice a line (up to 12, '#' comments); without it the wheel holds up to 8
 * artist folders of the library, picked at random ("today, listen to..."), and the result plays that artist. */
#define WHEEL_MAX 12
#define W_RO Z(176)          /* the square panel's wheel fills the panel: every size scales with the content box */
#define W_RI Z(112)
static char *s_wbuf;
static const char *s_witem[WHEEL_MAX];
static int s_wfolder[WHEEL_MAX], s_wn, s_wcur, s_wresult;
static lv_obj_t *s_wcont, *s_wlabel[WHEEL_MAX], *s_wcenter;
static pc_wheel_spin_t s_wspin;
static double s_wangle;
#define WHEEL_FRAMES 110                                  /* ~3.6 s */

static void wheel_items(void)
{
    s_wn = 0;
    s_wbuf = read_text(FORTUNE_DIR "/wheel.txt", 4096);
    if (s_wbuf) {
        char *l[WHEEL_MAX];
        s_wn = pc_fortune_lines(s_wbuf, l, WHEEL_MAX, 48);
        for (int i = 0; i < s_wn; i++) { s_witem[i] = l[i]; s_wfolder[i] = -1; }
    }
    if (s_wn >= 2) return;
    s_wn = 0;
    int nf = g_lib.nfolders;
    for (int i = 0; i < nf && s_wn < 8; i++) {           /* a random sample of up to 8 folders */
        int left = nf - i, need = 8 - s_wn;
        if ((int)pc_rng_below(ui_rng(), (uint32_t)left) < need && g_lib.folders[i].count > 0) {
            s_witem[s_wn] = pc_str(&g_lib, g_lib.folders[i].name);
            s_wfolder[s_wn++] = i;
        }
    }
    if (s_wn >= 2) {                                     /* shuffle the order */
        for (int i = s_wn - 1; i > 0; i--) {
            int j = (int)pc_rng_below(ui_rng(), (uint32_t)(i + 1));
            const char *t = s_witem[i]; s_witem[i] = s_witem[j]; s_witem[j] = t;
            int f = s_wfolder[i]; s_wfolder[i] = s_wfolder[j]; s_wfolder[j] = f;
        }
        return;
    }
    static const char *const ZH[6] = { "是", "否", "再等等", "現在就做", "問朋友", "睡一覺再說" };
    static const char *const EN[6] = { "Yes", "No", "Wait", "Do it now", "Ask a friend", "Sleep on it" };
    for (s_wn = 0; s_wn < 6; s_wn++) { s_witem[s_wn] = T_RAW(ZH[s_wn], EN[s_wn]); s_wfolder[s_wn] = -1; }
}

static void wheel_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(s_wcont, &a);
    int32_t cx = a.x1 + lv_area_get_width(&a) / 2, cy = a.y1 + lv_area_get_height(&a) / 2;
    float seg = 360.0f / (float)s_wn;
    for (int i = 0; i < s_wn; i++) {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = W_RO;
        d.width = W_RO - W_RI;
        d.start_angle = 270.0f + seg * (float)i + 1.0f;  /* wheel 0 deg = up = LVGL 270; a small gap */
        d.end_angle = 270.0f + seg * (float)(i + 1) - 1.0f;
        d.color = i == s_wcur ? (s_wresult == i ? UI_GOLD : UI_SEL) : (i & 1) ? UI_PANEL2 : UI_PANEL;
        lv_draw_arc(layer, &d);
    }
    float r = (float)((s_wangle - 90.0) * M_PI / 180.0);   /* the needle: a line and a dot on the inner rim */
    lv_draw_line_dsc_t l;
    lv_draw_line_dsc_init(&l);
    l.color = UI_A1;
    l.width = Z(6);
    l.round_start = l.round_end = 1;
    l.p1.x = cx + Z(84) * cosf(r); l.p1.y = cy + Z(84) * sinf(r);
    l.p2.x = cx + (W_RI - 4) * cosf(r); l.p2.y = cy + (W_RI - 4) * sinf(r);
    lv_draw_line(layer, &l);
}

static void wheel_place_labels(void)
{
    float seg = 360.0f / (float)s_wn, rm = (W_RO + W_RI) / 2.0f - 4;
    for (int i = 0; i < WHEEL_MAX; i++) {
        if (i >= s_wn) { lv_obj_add_flag(s_wlabel[i], LV_OBJ_FLAG_HIDDEN); continue; }
        float m = (270.0f + seg * ((float)i + 0.5f)) * 0.0174533f;
        const lv_font_t *f = lv_obj_get_style_text_font(s_wlabel[i], 0);
        int lh = lv_font_get_line_height(f), w = Z(s_wn > 8 ? 64 : 84);
        lv_point_t sz;
        lv_text_get_size(&sz, s_witem[i], f, 0, 0, w, LV_TEXT_FLAG_NONE);
        /* a fixed height so the dots cut works: one line when there are more than 8 entries, two otherwise (one when the text needs only one: it stays centred) */
        lv_obj_set_size(s_wlabel[i], w, s_wn > 8 || sz.y <= lh ? lh : 2 * lh);
        lv_label_set_text(s_wlabel[i], s_witem[i]);
        lv_obj_align(s_wlabel[i], LV_ALIGN_CENTER, (int32_t)lroundf(rm * cosf(m)), (int32_t)lroundf(rm * sinf(m)));
    }
}

static void wheel_center_text(void)
{
    char b[160];
    if (s_anim) snprintf(b, sizeof b, "%s", T(S_SPINNING) /* Spinning... */);
    else if (s_wresult < 0) snprintf(b, sizeof b, "%s", T(S_TAP_TO_SPIN) /* Tap to spin */);
    else if (s_wfolder[s_wresult] >= 0) snprintf(b, sizeof b, "%s\n%s", s_witem[s_wresult], T(S_TAP_PLAY) /* Tap the middle to play */);
    else snprintf(b, sizeof b, "%s", s_witem[s_wresult]);
    lv_label_set_text(s_wcenter, b);
}

static void wheel_step(void)
{
    if (!ui_screen_on()) return;
    int f = ++s_frame;
    s_wangle = pc_wheel_angle_at(&s_wspin, (double)f / WHEEL_FRAMES);
    int i = pc_wheel_index(s_wangle, s_wn);
    if (i != s_wcur) { s_wcur = i; ui_haptic_tick(); }   /* a tick for every segment the needle passes */
    if (f >= WHEEL_FRAMES) {
        anim_stop();
        s_wresult = s_wcur;
        ui_haptic_tap();
        printf("APP fortune wheel result=%d item=%s folder=%d\n", s_wresult, s_witem[s_wresult], s_wfolder[s_wresult]);
        wheel_center_text();
    }
    lv_obj_invalidate(s_wcont);
}

static void wheel_spin(void)
{
    if (s_anim || s_wn < 1) return;
    pc_wheel_plan(ui_rng(), s_wn, fmod(s_wangle, 360.0), &s_wspin);
    s_wangle = s_wspin.start;
    s_wresult = -1;
    anim_start(wheel_step);
    wheel_center_text();
}

static void wheel_play(void)
{
    if (s_anim || s_wresult < 0 || s_wfolder[s_wresult] < 0) return;
    mp3_ctl_play_folder(s_wfolder[s_wresult]);
    now_open();
}

static void wheel_tap(lv_event_t *e)
{
    (void)e;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    int cx, cy;
    ui_design_centre(&cx, &cy);                         /* the wheel's centre (the 360 box sits in the middle of a bigger panel) */
    int dx = pt.x - cx, dy = pt.y - cy;
    bool centre = dx * dx + dy * dy < (W_RI - 8) * (W_RI - 8);
    if (centre && s_wresult >= 0 && s_wfolder[s_wresult] >= 0) wheel_play();   /* centre: play the artist; else spin */
    else wheel_spin();
}
static bool wheel_knob(int d) { (void)d; wheel_spin(); return true; }

static void wheel_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_timer_guard(p, &s_anim);
    wheel_items();
    s_wcur = -1;
    s_wresult = -1;
    s_wangle = 0;
    s_wcont = lv_obj_create(p);
    lv_obj_remove_style_all(s_wcont);
    ui_content_box(s_wcont);                            /* the wheel's box: the content box (360 round, 460 square) */
    lv_obj_remove_flag(s_wcont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wcont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wcont, wheel_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_wcont, wheel_tap, LV_EVENT_CLICKED, NULL);
    for (int i = 0; i < WHEEL_MAX; i++) {
        s_wlabel[i] = lv_label_create(s_wcont);
        lv_label_set_long_mode(s_wlabel[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(s_wlabel[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_remove_flag(s_wlabel[i], LV_OBJ_FLAG_CLICKABLE);
    }
    wheel_place_labels();
    s_wcur = pc_wheel_index(0, s_wn);
    s_wcenter = ui_label_d(s_wcont, NULL, -14, 150);
    ui_back_button_hub(s_wcont);         /* back inside the hub: the rim is all wheel */
    wheel_center_text();
    s_action = wheel_spin;
}
static void wheel_destroy(void)
{
    anim_stop();
    if (s_wbuf) heap_caps_free(s_wbuf);
    s_wbuf = NULL;
    s_wn = 0;
    s_wcont = NULL;
    s_action = NULL;
}
static const ui_screen_t scr_wheel = { .name = "fortune_wheel", .create = wheel_create, .destroy = wheel_destroy, .on_knob = wheel_knob };

/* ================= song gacha ================= */
/* rarity by play count quantile (pc_gacha_tiers); collection in /sdcard/fortune/cards.txt ("mp3:<path>=<times>") */
#define CARDS_PATH PC_CFG_CARDS
static uint8_t *s_gtier;                /* g_lib.n bytes, PSRAM, while the gacha screens are open */
static int s_gn, s_gcollected;
static int s_gpull[10], s_gpulled;
static uint32_t s_gcount[10];
static lv_obj_t *s_gring;               /* the gacha page: a menu ring (single, ten), the centre shows the collection */
#define RAR_COL ui_pal(UI_PAL_rarity)
static const char *const RAR_NAME[4] = { "N", "R", "SR", "SSR" };
static const ui_screen_t scr_card, scr_ten;

static void gacha_tiers(void)
{
    s_gn = g_lib.n;
    if (s_gn <= 0) return;
    uint32_t *plays = heap_caps_malloc((size_t)s_gn * 8, MALLOC_CAP_SPIRAM);   /* plays + the sort scratch */
    s_gtier = heap_caps_malloc((size_t)s_gn, MALLOC_CAP_SPIRAM);
    if (!plays || !s_gtier) { if (plays) heap_caps_free(plays); if (s_gtier) heap_caps_free(s_gtier); s_gtier = NULL; s_gn = 0; return; }
    char id[300];
    for (int i = 0; i < s_gn; i++) {
        snprintf(id, sizeof id, "mp3:%s", pc_str(&g_lib, g_lib.tracks[i].path));
        plays[i] = g_stats ? pc_stats_plays(g_stats, id) : 0;
    }
    pc_gacha_tiers(plays, s_gn, plays + s_gn, s_gtier);
    heap_caps_free(plays);
}

static int count_lines(const char *path)
{
    FILE *f = g_sd_ok ? fopen(path, "r") : NULL;
    int n = 0, c, prev = '\n';
    while (f && (c = fgetc(f)) != EOF) { if (c == '\n' && prev != '\n') n++; prev = c; }
    if (f) { if (prev != '\n') n++; fclose(f); }
    return n;
}

static void gacha_save(void)   /* raise the pulled tracks' counts in cards.txt (tmp + rename) */
{
    memset(s_gcount, 0, sizeof s_gcount);
    if (!g_sd_ok) return;
    mkdir(FORTUNE_DIR, 0777);
    static char ids[10][300];
    const char *p[10];
    for (int i = 0; i < s_gpulled; i++) { snprintf(ids[i], sizeof ids[i], "mp3:%s", pc_str(&g_lib, g_lib.tracks[s_gpull[i]].path)); p[i] = ids[i]; }
    FILE *in = fopen(CARDS_PATH, "r"), *out = fopen(FORTUNE_DIR "/cards.tmp", "w");
    int n = out ? pc_cards_update(in, out, p, s_gpulled, s_gcount) : -1;
    if (in) fclose(in);
    if (out) fclose(out);
    if (n >= 0) { remove(CARDS_PATH); rename(FORTUNE_DIR "/cards.tmp", CARDS_PATH); s_gcollected = n; }
    printf("APP fortune cards rc=%d collected=%d\n", n, s_gcollected);
}

static void gacha_info(void)
{
    if (!s_gring) return;
    char b[160];
    if (!s_gtier) { ui_ring_set_center_text(s_gring, T(S_THE_LIBRARY_IS_EMPTY) /* The library is empty */); return; }
    snprintf(b, sizeof b, T(S_COLLECTED_D_D_SSR_D_SR_D_R_D_N_D) /* Collection %d / %d songs SSR%d%% SR%d%%... */,
             s_gcollected, s_gn, pc_rarity_rate[PC_RAR_SSR], pc_rarity_rate[PC_RAR_SR], pc_rarity_rate[PC_RAR_R], pc_rarity_rate[PC_RAR_N]);
    ui_ring_set_center_text(s_gring, b);
}

static void gacha_pull(int count)
{
    if (!s_gtier || ui_top() == &scr_ten || ui_top() == &scr_card) return;
    s_gpulled = pc_gacha_pull(ui_rng(), s_gtier, s_gn, count, s_gpull);
    if (!s_gpulled) return;
    gacha_save();
    for (int i = 0; i < s_gpulled; i++) printf("APP fortune gacha %d rar=%s idx=%d count=%lu\n", i, RAR_NAME[s_gtier[s_gpull[i]]], s_gpull[i], (unsigned long)s_gcount[i]);
    gacha_info();
    if (count == 1) ui_push(&scr_card, (void *)(intptr_t)0); else ui_push(&scr_ten, NULL);
}
static void gacha_one(void) { gacha_pull(1); }
static void gacha_pick(int idx) { if (idx == 0) gacha_pull(1); else if (idx == 1) gacha_pull(10); }   /* the centre (-1) shows the collection and does nothing */
static void gacha_back(void) { ui_pop(); }

static void gacha_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    gacha_tiers();
    s_gcollected = count_lines(CARDS_PATH);
    const char *labels[2] = { T(S_X1) /* Single pull */, T(S_X10) /* Ten pulls */ };
    s_gring = ui_ring_create_menu(p, labels, s_gtier ? 2 : 0, gacha_pick, gacha_back);   /* an empty library: no pull cells, the centre says so, Back stays */
    gacha_info();
    s_action = gacha_one;
}
static void gacha_destroy(void)
{
    if (s_gtier) heap_caps_free(s_gtier);
    s_gtier = NULL;
    s_gn = 0;
    s_gring = NULL;
    s_action = NULL;
}
static const ui_screen_t scr_gacha = { .name = "fortune_gacha", .create = gacha_create, .destroy = gacha_destroy };

/* ---- a ten-pull: ten cards around the rim, revealed one by one ---- */
static lv_obj_t *s_tcard[10], *s_tsum;
static void ten_card_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_anim) return;
    ui_push(&scr_card, (void *)(intptr_t)i);
}
static void ten_summary(void)
{
    int c[4] = { 0 };
    for (int i = 0; i < s_gpulled; i++) c[s_gtier[s_gpull[i]]]++;
    char b[120];
    snprintf(b, sizeof b, "SSR %d · SR %d\nR %d · N %d\n%s", c[3], c[2], c[1], c[0], T(S_TAP_A_CARD) /* Tap a card to see the song */);
    lv_label_set_text(s_tsum, b);
}
static void ten_step(void)
{
    if (!ui_screen_on()) return;
    int f = s_frame++;
    if (f % 5 == 0 && f / 5 < s_gpulled) {
        int i = f / 5, r = s_gtier[s_gpull[i]];
        lv_obj_set_style_bg_color(s_tcard[i], ui_c(RAR_COL[r]), 0);
        lv_obj_set_style_text_color(s_tcard[i], ui_text_on(ui_c(RAR_COL[r])), 0);   /* readable on the rarity colour in a light theme too */
        lv_label_set_text(lv_obj_get_child(s_tcard[i], 0), RAR_NAME[r]);
        if (r >= PC_RAR_SR) ui_haptic_tap(); else ui_haptic_tick();
    }
    if (f / 5 >= s_gpulled) { anim_stop(); ten_summary(); }
}
static void ten_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_timer_guard(p, &s_anim);
    for (int i = 0; i < 10; i++) {
        float a = (float)(-90.0 + 36.0 * i) * 0.0174533f;
        lv_obj_t *l;
        lv_obj_t *b = ui_button(p, 62, 62, (int32_t)lroundf(142 * cosf(a)), (int32_t)lroundf(142 * sinf(a)), ten_card_cb, (void *)(intptr_t)i, &l);   /* 142: the lower side cards stay UI_TAP_GAP from the hub Back */
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_bg_color(b, UI_PANEL2, 0);
        lv_label_set_text(l, "?");
        if (i >= s_gpulled) lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        s_tcard[i] = b;
    }
    s_tsum = ui_label(p, NULL, -16, 170);
    lv_label_set_text(s_tsum, T(S_TEN_PULL) /* Ten pulls! */);
    ui_back_button_hub(p);                 /* back in the middle: the rim holds the cards */
    anim_start(ten_step);
}
static void ten_destroy(void) { anim_stop(); s_tsum = NULL; }
static const ui_screen_t scr_ten = { .name = "fortune_ten", .create = ten_create, .destroy = ten_destroy };

/* ---- one card: cover, rarity frame, title; tap plays it ---- */
static int s_cidx;
static uint32_t s_creq;
static uint16_t *s_cpx;
static lv_image_dsc_t s_cdsc;
static lv_obj_t *s_cimg;
static void card_thumb(uint32_t req, uint16_t *px, void *u)
{
    (void)u;
    if (req != s_creq || !s_cimg) { if (px) heap_caps_free(px); return; }
    if (!px) return;
    if (s_cpx) heap_caps_free(s_cpx);
    s_cpx = px;
    s_cdsc = (lv_image_dsc_t){ .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = 240, .h = 240, .stride = 480 },
                               .data_size = 240 * 240 * 2, .data = (const uint8_t *)px };
    lv_image_set_src(s_cimg, &s_cdsc);
    lv_obj_remove_flag(s_cimg, LV_OBJ_FLAG_HIDDEN);
}
static void card_play(void)
{
    if (s_cidx < 0 || s_cidx >= g_lib.n) return;
    printf("APP fortune play idx=%d\n", s_cidx);
    mp3_ctl_play_folder_track(pc_track_folder(&g_lib, s_cidx), s_cidx);
    now_open();
}
static void card_tap(lv_event_t *e) { (void)e; card_play(); }
static void card_create(lv_obj_t *p, void *arg)
{
    int slot = (int)(intptr_t)arg;
    if (slot < 0 || slot >= s_gpulled || !s_gtier) { s_cidx = -1; ui_back_button(p); return; }
    s_cidx = s_gpull[slot];
    int r = s_gtier[s_cidx];
    const pc_track_t *t = &g_lib.tracks[s_cidx];
    lv_obj_t *frame = box(p, 168, 168, ui_c(RAR_COL[r]), 16);   /* the rarity frame around the cover */
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, -40);
    lv_obj_t *inner = box(frame, 152, 152, UI_PANEL2, 10);
    lv_obj_center(inner);
    lv_obj_t *note = ui_label(inner, &lv_font_montserrat_20, 0, 60);
    lv_label_set_text(note, LV_SYMBOL_AUDIO);
    s_cimg = lv_image_create(inner);
    lv_image_set_scale(s_cimg, 256 * 152 / 240);
    lv_obj_center(s_cimg);
    lv_obj_add_flag(s_cimg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_cimg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *rl = ui_label(p, font_large(), -140, 200);
    lv_label_set_text(rl, RAR_NAME[r]);
    lv_obj_set_style_text_color(rl, ui_c(RAR_COL[r]), 0);
    lv_obj_t *tl = ui_label(p, NULL, 60, 280);
    lv_label_set_long_mode(tl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(tl, 26);
    lv_label_set_text(tl, pc_str(&g_lib, t->title));
    lv_obj_t *al = ui_label(p, NULL, 86, 260);
    lv_label_set_long_mode(al, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(al, 26);
    char b[200];
    uint32_t c = s_gcount[slot];
    snprintf(b, sizeof b, "%s · %s", pc_str(&g_lib, t->artist), c <= 1 ? T(S_NEW) : "");
    if (c > 1) snprintf(b, sizeof b, T(S_S_X_LU) /* %s · pull %lu */, pc_str(&g_lib, t->artist), (unsigned long)c);
    lv_label_set_text(al, b);
    lv_obj_set_style_text_color(al, UI_DIM, 0);
    lv_obj_t *h = ui_label(p, NULL, 110, 220);
    lv_label_set_text(h, T(S_TAP_TO_PLAY) /* Tap a card to play */);
    lv_obj_set_style_text_color(h, UI_DIM, 0);
    lv_obj_add_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(frame, card_tap, LV_EVENT_CLICKED, NULL);
    ui_back_button(p);
    if (r >= PC_RAR_SR) ui_haptic_tap();
    s_creq = thumb_svc_request(pc_str(&g_lib, t->path), t->apic_off, t->apic_len, card_thumb, NULL);   /* apic_len 0: an image file next to the track, if any */
    s_action = card_play;
}
static void card_destroy(void)
{
    s_creq = 0;
    s_cimg = NULL;
    if (s_cpx) heap_caps_free(s_cpx);
    s_cpx = NULL;
    s_cidx = -1;
    s_action = ui_top() == &scr_ten ? NULL : gacha_one;   /* below: the ten-pull or the gacha page */
}
static const ui_screen_t scr_card = { .name = "fortune_card", .create = card_create, .destroy = card_destroy };

/* ================= tarot ================= */
#define CARD_W ui_scale_pct(124, 118)       /* the square panel's card is larger (SQ2), the hint above and the meaning below it stay clear */
#define CARD_H ui_scale_pct(180, 118)
static lv_obj_t *s_tc, *s_tback, *s_tface, *s_tnum, *s_tname, *s_tpos, *s_tmean, *s_thint;
static pc_tarot_t s_tres;
static const char *const ROMAN[22] = { "0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
                                       "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX", "XXI" };

static void tarot_reveal(void)
{
    const fortune_tarot_t *c = &fortune_tarot[s_tres.card];
    lv_obj_add_flag(s_tback, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_tface, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_tnum, ROMAN[s_tres.card]);
    lv_label_set_text(s_tname, T_RAW(c->zh, c->en));
    lv_label_set_text(s_tpos, s_tres.reversed ? T(S_REVERSED) /* Reversed */ : T(S_UPRIGHT) /* Upright */);
    lv_obj_set_style_text_color(s_tpos, s_tres.reversed ? ui_palc(UI_PAL_tarot_rev, 0) : UI_GOLD, 0);
    lv_obj_align(s_tnum, s_tres.reversed ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID, 0, s_tres.reversed ? -Z(10) : Z(10));   /* reversed: upside down */
    lv_obj_align(s_tpos, s_tres.reversed ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, s_tres.reversed ? Z(10) : -Z(10));
    lv_label_set_text(s_tmean, s_tres.reversed ? T_RAW(c->rev_zh, c->rev_en) : T_RAW(c->up_zh, c->up_en));
    lv_label_set_text(s_thint, clock_time_valid() ? T(S_TODAY_S_CARD) /* Today's card */ : T(S_NOT_SYNCED_RANDOM) /* Time not set: random each time */);
    printf("APP fortune tarot card=%d reversed=%d\n", s_tres.card, s_tres.reversed);
}

static void tarot_step(void)
{
    if (!ui_screen_on()) return;
    int f = s_frame++;
    int w = f < 7 ? CARD_W - f * CARD_W / 7 : f < 14 ? (f - 7) * CARD_W / 7 : CARD_W;   /* turn the card over */
    lv_obj_set_width(s_tc, w < 2 ? 2 : w);
    if (f == 7) { tarot_reveal(); ui_haptic_tap(); }
    if (f >= 14) { anim_stop(); lv_obj_set_width(s_tc, CARD_W); }
}

static void tarot_draw(void)
{
    if (s_anim) return;
    uint64_t seed;
    pc_rng_t r;
    pc_rng_seed(&r, day_seed(PC_SALT_TAROT, &seed) ? seed : ((uint64_t)esp_random() << 32 | esp_random()));
    pc_tarot_draw(&r, &s_tres);
    lv_obj_remove_flag(s_tback, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_tface, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_tmean, "");
    anim_start(tarot_step);
}
static void tarot_tap(void) { tarot_draw(); }
static bool tarot_knob(int d) { (void)d; tarot_draw(); return true; }

static void tarot_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_timer_guard(p, &s_anim);
    s_tc = box(p, CARD_W, CARD_H, UI_PANEL2, 12);
    lv_obj_set_style_border_width(s_tc, 3, 0);
    lv_obj_set_style_border_color(s_tc, UI_GOLD, 0);
    lv_obj_align(s_tc, LV_ALIGN_CENTER, 0, ui_scale_pct(-42, 70));
    lv_obj_set_style_clip_corner(s_tc, true, 0);
    s_tback = lv_obj_create(s_tc);                       /* the back: rings and a star */
    lv_obj_remove_style_all(s_tback);
    lv_obj_set_size(s_tback, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(s_tback, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *ring = box(s_tback, ui_scale_pct(80 - i * 34, 118), ui_scale_pct(80 - i * 34, 118), UI_PANEL2, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_color(ring, UI_A1, 0);
        lv_obj_center(ring);
    }
    lv_obj_t *star = ui_label_d(s_tback, &lv_font_montserrat_20, 0, 40);
    lv_label_set_text(star, "*");
    lv_obj_set_style_text_color(star, UI_GOLD, 0);
    s_tface = lv_obj_create(s_tc);
    lv_obj_remove_style_all(s_tface);
    lv_obj_set_size(s_tface, CARD_W - 6, CARD_H - 6);
    lv_obj_center(s_tface);
    lv_obj_remove_flag(s_tface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_tface, LV_OBJ_FLAG_HIDDEN);
    s_tnum = lv_label_create(s_tface);
    lv_obj_set_style_text_font(s_tnum, font_large(), 0);
    s_tname = ui_label(s_tface, NULL, 0, CARD_W - 12);
    s_tpos = lv_label_create(s_tface);
    s_tmean = ui_label(p, NULL, ui_scale_pct(76, 118), Z(280));
    s_thint = ui_label_d(p, NULL, -152, 190);
    ui_text_airy(s_tmean);
    lv_obj_set_style_text_color(s_thint, UI_DIM, 0);
    lv_label_set_text(s_tmean, "");
    lv_label_set_text(s_thint, clock_time_valid() ? T(S_TAP_FOR_TODAY_S_CARD) /* Tap to flip today's card */ : T(S_TAP_TO_DRAW_NOT_SYNCED) /* Tap to draw (time not set: random) */);
    ui_back_button(p);
    ui_page_tap(p, tarot_tap);
    s_action = tarot_draw;
}
static void tarot_destroy(void) { anim_stop(); s_action = NULL; }
static const ui_screen_t scr_tarot = { .name = "fortune_tarot", .create = tarot_create, .destroy = tarot_destroy, .on_knob = tarot_knob };

/* ================= the fortune ring ================= */
static const ui_screen_t *const SUBS[5] = { &scr_kuji, &scr_jiao, &scr_wheel, &scr_gacha, &scr_tarot };
static void menu_pick(int i)
{
    if (i == 3 && !g_sd_ok) { ui_push_message(T(S_NO_TF_CARD) /* No TF card */); return; }
    if (i >= 0 && i < 5) ui_push(SUBS[i], NULL);
}
static void menu_back(void) { ui_pop(); }
static void fortune_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[5] = { T(S_OMIKUJI) /* Today's slip */, T(S_MOON_BLOCKS) /* Moon blocks */, T(S_WHEEL) /* Wheel */, T(S_GACHA) /* Gacha */, T(S_TAROT) /* Tarot */ };
    static const char *const icons[5] = { UI_ICON_OMIKUJI, UI_ICON_MOONBLOCKS, UI_ICON_WHEEL, UI_ICON_GACHA, UI_ICON_TAROT };
    ui_ring_set_icons(ui_ring_create(p, labels, 5, menu_pick, menu_back), icons, 5);
}
const ui_screen_t scr_fortune = { .name = "fortune", .create = fortune_create };

/* console (screenshots without touching the screen):
 * fortune [kuji|jiao|dice|wheel|gacha|tarot]  open the ring or a page   fortune draw  the page's main action
 * fortune ten | one  gacha pulls   fortune card N  card N of the last pull   fortune dice N  how many dice
 * fortune play  play the wheel's artist / the shown card */
void fortune_cmd(const char *a)
{
    static const char *const NAMES[5] = { "kuji", "jiao", "wheel", "gacha", "tarot" };
    int n;
    if (!a[0]) { ui_push(&scr_fortune, NULL); return; }
    if (!strcmp(a, "dice") || !strncmp(a, "dice ", 5)) { tools_cmd(a); return; }   /* the dice live in tools now */
    for (int i = 0; i < 5; i++) if (!strcmp(a, NAMES[i])) {   /* back to the fortune ring first: repeated commands must not stack pages */
        while (ui_top() && ui_top() != &scr_fortune && !strncmp(ui_top()->name, "fortune", 7)) ui_pop();
        if (ui_top() != &scr_fortune) ui_push(&scr_fortune, NULL);
        menu_pick(i);
        printf("APP fortune top=%s\n", ui_top() ? ui_top()->name : "-");
        return;
    }
    bool ours = ui_top() && !strncmp(ui_top()->name, "fortune", 7);
    if (!strcmp(a, "draw")) { if (s_action && ours) s_action(); else printf("APP fortune no action\n"); }
    else if (!strcmp(a, "ten") || !strcmp(a, "one")) {
        if (ui_top() != &scr_gacha) { while (ui_top() == &scr_card || ui_top() == &scr_ten) ui_pop(); }
        if (ui_top() != &scr_gacha) menu_pick(3);
        if (ui_top() == &scr_gacha) gacha_pull(a[0] == 't' ? 10 : 1);
    }
    else if (sscanf(a, "card %d", &n) == 1) { if (n >= 0 && n < s_gpulled && s_gtier) ui_push(&scr_card, (void *)(intptr_t)n); }
    else if (!strcmp(a, "play")) { if (ui_top() == &scr_wheel) wheel_play(); else if (ui_top() == &scr_card) card_play(); }
    else printf("APP fortune unknown %s\n", a);
    printf("APP fortune top=%s\n", ui_top() ? ui_top()->name : "-");
}
