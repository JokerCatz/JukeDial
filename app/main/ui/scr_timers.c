// SPDX-License-Identifier: LGPL-2.1-or-later
/* Stopwatch and countdown pages (HUD task). The state and the running live in timers_svc.c (they go on when you leave the page); a page is a face
 * (tface.c: info / rings / the designed HUD face, its own choice per page in cfg swface / cdface) and a tap-revealed ring: the centre starts / pauses / resumes, the other cells are
 * Lap or Min / Sec, Reset (armed: two taps) and the Face axis (borrowed knob), 說明 last. Only composes shared pieces. */
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "pc/pc_clockface.h"
#include "pc/pc_timers.h"
#include "screens.h"
#include "tface.h"
#include "timers_svc.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_popring.h"

static lv_obj_t *s_root, *s_cbox, *s_face;
static lv_timer_t *s_t;
static int s_kind;
static bool s_cd;                                      /* the page shown is the countdown */
static const char *key(void) { return s_cd ? "cdface" : "swface"; }

/* ---- the data ---- */
#define AV(d, f, ...) snprintf((d)->art.v[f], sizeof (d)->art.v[0], __VA_ARGS__)
static void sw_art(tface_data_t *d, const pc_sw_t *s, int64_t el)   /* the designed face's slots: fixed formats */
{
    int64_t cs = el / 10;
    AV(d, FAV_MAIN, "%02d:%02d", (int)(cs / 6000 % 100), (int)(cs / 100 % 60));
    AV(d, FAV_CC, "%02d", (int)(cs % 100));
    AV(d, FAV_HOURS, "%02d", (int)(cs / 360000 % 100));
    AV(d, FAV_STATE, "%d", s->running ? 1 : el ? 2 : 0);
    AV(d, FAV_LAPS, "%02d", s->nlap % 100);
    int best = pc_sw_lap_best(s);
    AV(d, FAV_BEST, "%d", best >= 0 && best < 3 ? best + 1 : 0);
    for (int k = 0; k < 3; k++) {
        int no; int64_t sp, tot;
        int fn = FAV_LAP1_NO + 2 * k, fv = FAV_LAP1 + 2 * k;
        if (pc_sw_lap_get(s, k, &no, &sp, &tot)) {
            int64_t c = sp / 10;
            AV(d, fn, "%02d", no % 100);
            AV(d, fv, "%02d:%02d.%02d", (int)(c / 6000 % 100), (int)(c / 100 % 60), (int)(c % 100));
        } else { AV(d, fn, "--"); AV(d, fv, "--:--.--"); }
    }
    d->art.warn = tface_common(&d->art) & 2;              /* FakePod battery low: the only real warning of a stopwatch */
}
static void sw_data(tface_data_t *d, int64_t now)
{
    pc_sw_t *s = timers_sw();
    int64_t el = pc_sw_elapsed(s, now);
    pc_sw_fmt(el, d->big, sizeof d->big);
    d->prog_pm = (int)(el % 60000 / 60);               /* the ring sweeps once a minute */
    if (tface_kind(s_face) == PC_TF_HUD) { sw_art(d, s, el); return; }
    int nl = tface_kind(s_face) == PC_TF_INFO ? 5 : 3, n = 0, best = pc_sw_lap_best(s);
    if (s->nlap == 0) {
        snprintf(d->line[0], sizeof d->line[0], "%s", s->running ? T(S_SW_RUNNING) /* Running */ : el ? T(S_SW_PAUSED_LINE) /* Paused, tap to go on */ : T(S_SW_TAP_START) /* Tap to start */);
        n = 1;
    }
    for (int k = 0; k < nl && k < s->nlap && k < PC_SW_LAPS; k++, n++) {
        int no; int64_t sp, tot;
        char a[16], b[16];
        pc_sw_lap_get(s, k, &no, &sp, &tot);
        pc_sw_fmt(sp, a, sizeof a);
        pc_sw_fmt(tot, b, sizeof b);
        if (tface_kind(s_face) == PC_TF_INFO) snprintf(d->line[n], sizeof d->line[0], T(S_SW_LAP_LINE) /* Lap %d  %s  %s */, no, a, b);
        else snprintf(d->line[n], sizeof d->line[0], "%s %d  %s", T(S_SW_LAP) /* Lap */, no, a);
        if (k == best) strncat(d->line[n], " *", sizeof d->line[0] - strlen(d->line[n]) - 1);
    }
    d->nline = n;
}
static void cd_data(tface_data_t *d, int64_t now)
{
    pc_cd_t *c = timers_cd();
    int64_t left = pc_cd_left_ms(c, now);
    pc_cd_fmt(left, d->big, sizeof d->big);
    bool warn = pc_cd_warn(c, now);
    d->red = warn || c->state == PC_CD_DONE;
    d->prog_pm = c->state == PC_CD_IDLE ? 0 : c->state == PC_CD_DONE ? 1000 : 1000 - (int)(left * 1000 / ((int64_t)c->set_s * 1000));
    if (tface_kind(s_face) == PC_TF_HUD) {
        int64_t ls = (left + 999) / 1000;                  /* rounded up, as pc_cd_fmt */
        AV(d, FAV_MAIN, "%02d:%02d", (int)(ls / 60 % 100), (int)(ls % 60));
        AV(d, FAV_SET, "%02d:%02d", c->set_s / 60 % 100, c->set_s % 60);
        AV(d, FAV_STATE, "%d", c->state == PC_CD_RUN ? 1 : c->state == PC_CD_PAUSE ? 2 : c->state == PC_CD_DONE ? 3 : 0);
        int lit = c->state == PC_CD_IDLE ? 20 : c->state == PC_CD_DONE ? 0 : (int)((left * 20 + (int64_t)c->set_s * 1000 - 1) / ((int64_t)c->set_s * 1000));
        AV(d, FAV_PROG, "%d", lit);                        /* 20 segments: what is left */
        int w = tface_common(&d->art);
        d->art.warn = d->red || (w & 2);                   /* the last 10 s, time up, FakePod battery low */
        return;
    }
    snprintf(d->line[0], sizeof d->line[0], "%s", c->state == PC_CD_RUN ? T(S_SW_RUNNING) : c->state == PC_CD_PAUSE ? T(S_CD_PAUSED) /* Paused */ : c->state == PC_CD_DONE ? T(S_TIME_IS_UP) /* Time is up */ : T(S_CD_SET_TIME) /* Tap to set the time */);
    char b[16];
    pc_cd_fmt((int64_t)c->set_s * 1000, b, sizeof b);
    snprintf(d->line[1], sizeof d->line[0], "%s %s", T(S_START) /* Start */, b);
    d->nline = 2;
}
static void refresh(void)
{
    if (!s_root || !s_face) return;
    if (!ui_screen_on()) return;                       /* dark: nothing to draw */
    tface_data_t d;
    memset(&d, 0, sizeof d);
    int64_t now = timers_now_ms();
    if (s_cd) cd_data(&d, now); else sw_data(&d, now);
    tface_set(s_face, &d);
}
static void tick(lv_timer_t *t) { (void)t; refresh(); }

/* ---- the face ---- */
static void face_fill(void)
{
    lv_obj_clean(s_cbox);
    s_face = ui_obj_guard(tface_create(s_cbox, s_kind, s_cd ? TF_CD : TF_SW), &s_face);
    refresh();
}
static const char *face_label_cell(void)
{
    static char b[40];
    snprintf(b, sizeof b, "%s\n%s", T(S_FACE) /* Face */, tface_label(s_kind));
    return b;
}
static bool face_step(int d)
{
    s_kind = tface_step(s_kind, d, s_cd ? TF_CD : TF_SW);
    tface_pref_set(key(), s_kind);
    face_fill();
    ui_popring_raise();
    return true;
}

static const char *col_label_cell(void)
{
    static char b[40];
    snprintf(b, sizeof b, "%s\n%s", T(S_COLOUR) /* Colour */, tface_hudcol_label(tface_hudcol()));
    return b;
}
static bool col_step(int d)                            /* the HUD's colour scheme: one choice for every HUD face */
{
    int n = pc_hc_count();
    tface_hudcol_set(((tface_hudcol() + d) % n + n) % n);
    refresh();
    return true;
}

/* ---- the stopwatch ring: Lap, Reset (armed), Face, Colour ---- */
enum { SW_LAP, SW_RESET, SW_FACE, SW_COL, SW_N };
static const char *sw_label(int i)
{
    static char b[SW_N][40];
    if (i == SW_FACE) { snprintf(b[i], sizeof b[i], "%s", face_label_cell()); return b[i]; }
    if (i == SW_COL) { snprintf(b[i], sizeof b[i], "%s", col_label_cell()); return b[i]; }
    snprintf(b[i], sizeof b[i], "%s", i == SW_LAP ? T(S_SW_LAP) /* Lap */ : T(S_RESET) /* Reset */);
    return b[i];
}
static const char *sw_centre(void) { return timers_sw()->running ? T(S_PAUSE) /* Pause */ : pc_sw_elapsed(timers_sw(), timers_now_ms()) ? T(S_RESUME) /* Resume */ : T(S_START) /* Start */; }
static const char *sw_armed(int i) { (void)i; return T(S_TAP_AGAIN_ZERO_ALL) /* Tap again: reset all */; }
static void back(void) { ui_popring_close(); ui_pop(); }
static void sw_pick(int i)
{
    if (i == -1) timers_sw_toggle();
    else if (i == SW_LAP) timers_sw_lap();
    else if (i == SW_RESET) timers_sw_reset();
    refresh();
    ui_popring_relabel();
}
static bool sw_step(int axis, int d) { return axis == SW_FACE ? face_step(d) : axis == SW_COL && col_step(d); }
static const ui_popring_t s_sw_def = { .n = SW_N, .label = sw_label, .pick = sw_pick, .centre = sw_centre, .back = back, .axis_mask = 1u << SW_FACE | 1u << SW_COL, .step = sw_step,
                                       .arm_mask = 1u << SW_RESET, .armed = sw_armed, .help = &HELP_STOPWATCH };

/* ---- the countdown ring: Min, Sec (axes), Reset (armed), Face ---- */
enum { CD_MIN, CD_SEC, CD_RESET, CD_FACE, CD_COL, CD_N };
static const char *cd_label(int i)
{
    static char b[CD_N][40];
    pc_cd_t *c = timers_cd();
    if (i == CD_MIN) snprintf(b[i], sizeof b[i], "%s\n%02d", T(S_CD_MIN) /* Min */, c->set_s / 60);
    else if (i == CD_SEC) snprintf(b[i], sizeof b[i], "%s\n%02d", T(S_CD_SEC) /* Sec */, c->set_s % 60);
    else if (i == CD_FACE) snprintf(b[i], sizeof b[i], "%s", face_label_cell());
    else if (i == CD_COL) snprintf(b[i], sizeof b[i], "%s", col_label_cell());
    else snprintf(b[i], sizeof b[i], "%s", T(S_RESET) /* Reset */);
    return b[i];
}
static const char *cd_centre(void) { pc_cd_t *c = timers_cd(); return c->state == PC_CD_RUN ? T(S_PAUSE) : c->state == PC_CD_PAUSE ? T(S_RESUME) : T(S_START); }
static void cd_pick(int i)
{
    if (i == -1) timers_cd_toggle();
    else if (i == CD_RESET) timers_cd_reset();
    refresh();
    ui_popring_relabel();
}
static bool cd_step(int axis, int d)
{
    pc_cd_t *c = timers_cd();
    if (axis == CD_FACE) return face_step(d);
    if (axis == CD_COL) return col_step(d);
    if (c->state == PC_CD_RUN || c->state == PC_CD_PAUSE) return false;   /* a running countdown is not re-set: reset it first */
    int m = c->set_s / 60, s = c->set_s % 60;
    if (axis == CD_MIN) m = ui_axis_step(m, 0, 99, 1, d, ui_axis_from_tap());
    else s = ui_axis_step(s, 0, 59, 1, d, ui_axis_from_tap());
    int t = m * 60 + s;
    timers_cd_set(t < 1 ? 1 : t > PC_CD_MAX_S ? PC_CD_MAX_S : t);
    refresh();
    return true;
}
static const ui_popring_t s_cd_def = { .n = CD_N, .label = cd_label, .pick = cd_pick, .centre = cd_centre, .back = back, .axis_mask = 1u << CD_MIN | 1u << CD_SEC | 1u << CD_FACE | 1u << CD_COL,
                                       .step = cd_step, .arm_mask = 1u << CD_RESET, .armed = sw_armed, .help = &HELP_COUNTDOWN };
static const ui_popring_t *def(void) { return s_cd ? &s_cd_def : &s_sw_def; }
static void ring_open(void) { ui_popring_open(s_root, def(), 0); }

/* ---- the pages ---- */
static void create(lv_obj_t *p, bool cd)
{
    s_root = p;
    s_cd = cd;
    s_kind = tface_pref_feat(key(), PC_TF_INFO, s_cd ? TF_CD : TF_SW);
    ui_page_tap(p, ring_open);
    s_cbox = ui_content(p);
    s_face = ui_obj_guard(tface_create(s_cbox, s_kind, s_cd ? TF_CD : TF_SW), &s_face);
    s_t = ui_timer_bind(p, tick, 100, NULL);
    if (cd) timers_cd_page(true);
    refresh();
}
static void sw_create(lv_obj_t *p, void *arg) { (void)arg; create(p, false); }
static void cd_create(lv_obj_t *p, void *arg) { (void)arg; create(p, true); }
static void destroy(void)
{
    if (ui_popring_up(&s_sw_def) || ui_popring_up(&s_cd_def)) ui_popring_close();
    timers_cd_page(false);
    s_t = NULL; s_root = s_cbox = s_face = NULL;
}
const ui_screen_t scr_stopwatch = { .name = "clock_stopwatch", .create = sw_create, .destroy = destroy, .on_knob = ui_axis_knob, .content = true };
const ui_screen_t scr_countdown = { .name = "clock_countdown", .create = cd_create, .destroy = destroy, .on_knob = ui_axis_knob, .content = true };

/* console `timers <cmd>`: sw start|lap|reset|laps, cd set <s>|start|reset|jump <s>, ring, face <name|next>, dump, (nothing) the state line */
void timers_cmd(const char *a)
{
    int v;
    if (!*a) { timers_print(); return; }
    if (!strcmp(a, "sw start") || !strcmp(a, "sw toggle")) timers_sw_toggle();
    else if (!strcmp(a, "sw lap")) timers_sw_lap();
    else if (!strcmp(a, "sw reset")) timers_sw_reset();
    else if (!strcmp(a, "sw laps")) {
        for (int k = 0; k < PC_SW_LAPS; k++) { int no; int64_t sp, tot; if (!pc_sw_lap_get(timers_sw(), k, &no, &sp, &tot)) break; printf("APP timers lap k=%d no=%d split=%lld total=%lld best=%d\n", k, no, (long long)sp, (long long)tot, pc_sw_lap_best(timers_sw()) == k); }
    }
    else if (sscanf(a, "cd set %d", &v) == 1) timers_cd_set(v);
    else if (!strcmp(a, "cd start") || !strcmp(a, "cd toggle")) timers_cd_toggle();
    else if (!strcmp(a, "cd reset")) timers_cd_reset();
    else if (sscanf(a, "cd jump %d", &v) == 1) timers_cd_jump(v);
    else if (!strcmp(a, "ring")) { if (s_root) ring_open(); }
    else if (!strncmp(a, "face ", 5)) {
        if (!s_root) { printf("APP timers face none\n"); return; }
        s_kind = !strcmp(a + 5, "next") ? tface_step(s_kind, 1, s_cd ? TF_CD : TF_SW) : pc_tf_parse(a + 5);
        tface_pref_set(key(), s_kind);
        face_fill();
        printf("APP timers face=%s\n", pc_tf_name(s_kind));
    }
    else if (!strcmp(a, "dump")) { refresh(); tface_dump(s_face); }
    else if (!strcmp(a, "stats")) { tface_stats(); return; }
    else { printf("APP timers unknown %s\n", a); return; }
    refresh();
    timers_print();
}
