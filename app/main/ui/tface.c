// SPDX-License-Identifier: LGPL-2.1-or-later
/* The timing faces (tface.h): info, rings and the dispatch to the designed HUD faces (face_art.c), plus the real data every HUD face can show. Shared piece. */
#include "tface.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "time/clock.h"
#include "cfg.h"
#include "clock_svc.h"
#include "font.h"
#include "pc/pc_clockface.h"
#include "pc/pc_power.h"
#include "power_svc.h"
#include "ui_geom.h"
#include "ui_kit.h"
#include "ui_theme.h"

typedef struct {
    int kind, feat;
    lv_obj_t *arc, *big, *tag, *ln[5];
    face_art_t *art;
    tface_data_t d;
    bool red;
} tf_t;

static lv_obj_t *tlabel(lv_obj_t *p, const lv_font_t *f, lv_color_t c, int y, int w)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_width(l, ui_scale(w));
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_label_set_text(l, "");
    lv_obj_align(l, LV_ALIGN_CENTER, 0, ui_scale(y));
    return l;
}
static void tf_free(lv_event_t *e) { heap_caps_free(lv_event_get_user_data(e)); }
static tf_t *of(lv_obj_t *box) { return box ? lv_obj_get_user_data(box) : NULL; }

static const char *const s_feat[TF__FEATS] = { "clock", "sw", "cd", "pomo" };
static EXT_RAM_BSS_ATTR uint32_t s_mem;                 /* PSRAM of the HUD faces open (console tface stats) */
static void art_del(lv_event_t *e) { s_mem -= (uint32_t)(uintptr_t)lv_event_get_user_data(e); }
int tface_fallback(int feat) { return feat == TF_POMO ? PC_TF_RINGS : PC_TF_INFO; }   /* what a HUD choice shows without the zip: the pomodoro's built-in face is the rings, the others' the info face */
int tface_step(int kind, int d, int feat)
{
    int n = pc_tf_count(), k = ((kind + d) % n + n) % n;
    if (k == PC_TF_HUD && !tface_hud_ok()) k = ((k + d) % n + n) % n;   /* not offered without the zip */
    return k;
}
int tface_pref_feat(const char *key, int dflt, int feat)
{
    int k = tface_pref(key, dflt);
    return k == PC_TF_HUD && !tface_hud_ok() ? tface_fallback(feat) : k;   /* the stored choice stays in cfg (the zip may come later); the page shows the plain face */
}
bool tface_hud_ok(void) { return face_art_pack_state() == FACE_PACK_OK; }
lv_obj_t *tface_create(lv_obj_t *parent, int kind, int feat)
{
    tf_t *t = heap_caps_calloc(1, sizeof *t, MALLOC_CAP_SPIRAM);
    lv_obj_t *b = t ? ui_content(parent) : NULL;
    if (!b) { heap_caps_free(t); return NULL; }
    t->kind = kind;
    t->feat = feat < 0 || feat >= TF__FEATS ? TF_CLOCK : feat;
    t->d.prog_pm = -2;                                     /* nothing set yet: the first set draws everything */
    lv_obj_set_user_data(b, t);
    lv_obj_add_event_cb(b, tf_free, LV_EVENT_DELETE, t);
    if (kind == PC_TF_HUD && face_art_pack_state() != FACE_PACK_OK) kind = tface_fallback(feat);   /* no faces zip: the feature's plain face, never a blank page */
    t->kind = kind;
    if (kind == PC_TF_HUD) {
        char name[16];
        snprintf(name, sizeof name, "%s_%s", UI_ROUND ? "rd" : "sq", s_feat[t->feat]);
        t->art = face_art_open(b, name);
        if (t->art) {
            uint32_t m = face_art_mem(t->art);
            s_mem += m;
            lv_obj_add_event_cb(b, art_del, LV_EVENT_DELETE, (void *)(uintptr_t)m);
            return b;
        }
        t->kind = kind = tface_fallback(feat);             /* no art (a face missing from the zip, no memory): the plain face */
    }
    if (kind == PC_TF_RINGS) {
        lv_obj_t *a = lv_arc_create(b);
        lv_obj_set_size(a, ui_edge_d(330), ui_edge_d(330));
        lv_obj_center(a);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_arc_set_range(a, 0, 1000);
        lv_arc_set_bg_angles(a, 0, 360);
        lv_arc_set_rotation(a, 270);
        lv_obj_set_style_arc_color(a, UI_PANEL2, LV_PART_MAIN);
        lv_obj_set_style_arc_color(a, UI_A1, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
        t->arc = a;
    }
    bool ring = kind == PC_TF_RINGS;
    t->tag = tlabel(b, font_main(), UI_DIM, ring ? -92 : -100, 300);
    t->big = tlabel(b, font_clock(), UI_FG, ring ? -30 : -44, 320);
    int n = ring ? 3 : 5;
    for (int i = 0; i < n; i++) t->ln[i] = tlabel(b, font_main(), i == 0 ? UI_A1 : UI_DIM, (ring ? 32 : 18) + i * 28, 300);
    return b;
}

void tface_set(lv_obj_t *box, const tface_data_t *d)
{
    tf_t *t = of(box);
    if (!t) return;
    if (t->kind == PC_TF_HUD) { face_art_set(t->art, &d->art, tface_hudcol()); t->d = *d; return; }
    if (strcmp(t->d.big, d->big)) lv_label_set_text(t->big, d->big);
    if (strcmp(t->d.tag, d->tag)) lv_label_set_text(t->tag, d->tag);
    int n = t->kind == PC_TF_RINGS ? 3 : 5;
    for (int i = 0; i < n; i++) {
        const char *s = i < d->nline ? d->line[i] : "";
        if (strcmp(t->d.line[i], s)) lv_label_set_text(t->ln[i], s);
    }
    if (d->red != t->red) {
        t->red = d->red;
        lv_obj_set_style_text_color(t->big, d->red ? UI_WARN : UI_FG, 0);
        if (t->arc) lv_obj_set_style_arc_color(t->arc, d->red ? UI_WARN : UI_A1, LV_PART_INDICATOR);
    }
    if (t->arc && d->prog_pm != t->d.prog_pm) lv_arc_set_value(t->arc, d->prog_pm < 0 ? 0 : d->prog_pm);
    t->d = *d;
}
int tface_kind(lv_obj_t *box) { tf_t *t = of(box); return t ? t->kind : -1; }
void tface_dump(lv_obj_t *box)
{
    tf_t *t = of(box);
    if (!t) { printf("APP tface none\n"); return; }
    printf("APP tface kind=%s feat=%s big=%s prog=%d red=%d alert=%d", pc_tf_name(t->kind), s_feat[t->feat], t->d.big, t->d.prog_pm, t->d.red, t->d.art.warn);
    for (int i = 0; i < FAV__N; i++) if (t->d.art.v[i][0]) printf(" %s=%s", face_art_field_name(i), t->d.art.v[i]);
    printf(" l0=%s\n", t->d.line[0]);
}
void tface_stats(void) { printf("APP tface stats redraw_px=%lu mem=%lu\n", (unsigned long)face_art_redraw_px(), (unsigned long)s_mem); }

int tface_pref(const char *key, int dflt) { return cfg_has(key) ? pc_tf_parse(cfg_str(key, "")) : dflt; }
void tface_pref_set(const char *key, int kind) { cfg_set_str(key, pc_tf_name(kind)); }
const char *tface_label(int kind) { return kind == PC_TF_HUD ? T(S_TF_HUD) /* HUD */ : kind == PC_TF_RINGS ? T(S_TF_RINGS) /* Rings */ : T(S_TF_INFO) /* Info */; }
int tface_hudcol(void) { return cfg_has("hudcol") ? pc_hc_parse(cfg_str("hudcol", "")) : PC_HC_TRI; }
void tface_hudcol_set(int c) { cfg_set_str("hudcol", pc_hc_name(c)); }
const char *tface_hudcol_label(int c)
{
    return c == PC_HC_RED ? T(S_HUD_COL_RED) /* Red */ : c == PC_HC_CYAN ? T(S_HUD_COL_CYAN) /* Cyan */ : c == PC_HC_TRI ? T(S_HUD_COL_TRI) /* Tri-colour */ : T(S_HUD_COL_GREEN) /* Green */;
}
/* ---- the real data every HUD face can show ---- */
static EXT_RAM_BSS_ATTR face_vals_t s_cv;
static EXT_RAM_BSS_ATTR uint32_t s_cv_at;
static EXT_RAM_BSS_ATTR int s_cv_warn;
static EXT_RAM_BSS_ATTR bool s_cv_ok;
int tface_common(face_vals_t *v)      /* asked at 10 Hz by the stopwatch: rebuilt once a second */
{
    uint32_t now = lv_tick_get();
    if (!s_cv_ok || now - s_cv_at >= 1000) {
        face_vals_t *c = &s_cv;
        memset(c, 0, sizeof *c);
        bool sync = clock_time_valid();
        int w = sync ? 0 : 1;
        snprintf(c->v[FAV_SYNC], sizeof c->v[0], "%d", sync);
        if (sync) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            clock_civil_t cv;
            clock_civil_from_utc((int64_t)tv.tv_sec, clock_tz_min(), &cv);
            snprintf(c->v[FAV_CLOCK_HM], sizeof c->v[0], "%02d:%02d", cv.hour % 100, cv.minute % 100);
        } else snprintf(c->v[FAV_CLOCK_HM], sizeof c->v[0], "--:--");
        int pct = power_svc_batt_pct();
        if (pct >= 0) {
            bool chg = power_svc_charging();
            if (pct > 100) pct = 100;
            snprintf(c->v[FAV_BATT], sizeof c->v[0], "%3d", pct);
            snprintf(c->v[FAV_BATTBAR], sizeof c->v[0], "%d", (pct + 19) / 20);
            snprintf(c->v[FAV_POWER], sizeof c->v[0], "%d", chg ? 1 : 2);
            if (pct <= 15 && !chg) w |= 2;
        } else {                                          /* the round board has the rail ADC only: its source, no percentage and no low-battery warning */
            pc_power_t p = power_svc_state();
            snprintf(c->v[FAV_POWER], sizeof c->v[0], "%d", p == PC_PWR_USB ? 1 : p == PC_PWR_BATT ? 2 : 0);
            snprintf(c->v[FAV_BATT], sizeof c->v[0], "---");
            snprintf(c->v[FAV_BATTBAR], sizeof c->v[0], "0");
        }
        s_cv_warn = w;
        s_cv_at = now;
        s_cv_ok = true;
    }
    static const int keep[] = { FAV_SYNC, FAV_CLOCK_HM, FAV_BATT, FAV_BATTBAR, FAV_POWER };
    for (size_t i = 0; i < sizeof keep / sizeof keep[0]; i++) memcpy(v->v[keep[i]], s_cv.v[keep[i]], sizeof v->v[0]);
    return s_cv_warn;
}
