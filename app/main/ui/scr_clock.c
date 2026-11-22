// SPDX-License-Identifier: LGPL-2.1-or-later
/* Clock, alarm, pomodoro screens (spec 2026-11-10-clock-design.md). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "time/clock.h"
#include "time/moon.h"
#include "time/sun.h"
#include "time/terms.h"
#include "ui_theme.h"
#include "cfg.h"
#include "clock_svc.h"
#include "font.h"
#include "icons_gen.h"
#include "library_svc.h"
#include "mp3_ctl.h"
#include "screens.h"
#include "ui_axis.h"
#include "ui_ring.h"
#include "clock_face.h"
#include "pc/pc_clockface.h"
#include "ui_kit.h"
#include "ui_help.h"
#include "ui_popring.h"
#include "tface.h"
#include "timers_svc.h"
#include "pc/pc_clockface.h"

static const char *const WEEK[7] = { "日", "一", "二", "三", "四", "五", "六" };

/* every screen refreshes on its own timer (pushed screens hide the ones below, they are not recreated) */
static lv_timer_t *every(void (*fn)(void), int ms) { return lv_timer_create((lv_timer_cb_t)(void *)fn, ms, NULL); }
static void stop_timer(lv_timer_t **t) { if (*t) { lv_timer_delete(*t); *t = NULL; } }

/* ---- the clock page ---- */
static lv_obj_t *s_time, *s_date, *s_term, *s_sun, *s_moonname, *s_moon;
static lv_timer_t *s_ct, *s_pt, *s_rt;
static int s_moon_day = -1;
static const ui_screen_t scr_clock_menu;
static lv_obj_t *line(lv_obj_t *p, const lv_font_t *f, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_width(l, ui_scale(300));
    ui_text_airy(l);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, ui_scale(y));
    return l;
}
/* a label redraws even when given the same text: once a second that was the whole middle of the screen */
static void set_if(lv_obj_t *l, const char *t) { if (strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }
static void clock_refresh(void)
{
    if (!s_time) return;
    int32_t tz = clock_tz_min();
    char zone[24];
    clock_zone_label(zone, sizeof zone);
    if (!clock_time_valid()) {
        set_if(s_time, "--:--");
        set_if(s_date, T(S_NOT_SYNCED) /* Time not set yet */);
        set_if(s_term, T(S_NEEDS_WI_FI_TAP_SYNC) /* Needs Wi-Fi: tap → set the time now */);
        set_if(s_sun, "");
        set_if(s_moonname, zone);
        return;
    }
    char b[96];
    int64_t now = clock_now_utc();
    clock_civil_t v;
    clock_civil_from_utc(now, tz, &v);
    snprintf(b, sizeof b, "%02d:%02d", v.hour, v.minute);
    set_if(s_time, b);
    if (i18n_zh()) snprintf(b, sizeof b, "%d月%d日 星期%s", v.month, v.day, WEEK[v.weekday]);
    else snprintf(b, sizeof b, "%04d-%02d-%02d %s", v.year, v.month, v.day, clock_weekday_name(v.weekday));
    set_if(s_date, b);
    int nx;
    int ti = terms_index(now);
    int64_t next = terms_next_utc(now, &nx);
    int days = terms_days_until(now, next, tz);   /* local calendar days; 0 = the term begins today */
    char dd[24];
    if (days > 0) snprintf(dd, sizeof dd, "%d %s", days, T(S_DAYS) /* days later */);
    else snprintf(dd, sizeof dd, "%s", T(S_TODAY) /* Today */);
    snprintf(b, sizeof b, "%s · %s %s", T_RAW(terms_name_zh(ti), terms_name(ti)), T_RAW(terms_name_zh(nx), terms_name(nx)), dd);
    set_if(s_term, b);
    int rise, set;
    if (clock_sun_has() && !sun_rise_set(v.year, v.month, v.day, clock_tz_day_min(v.year, v.month, v.day), clock_sun_lat_e4(), clock_sun_lon_e4(), &rise, &set))   /* no location set from the web page: nothing here */
        snprintf(b, sizeof b, "%s %02d:%02d · %s %02d:%02d", T(S_RISE) /* Sunrise */, rise / 60, rise % 60, T(S_SET) /* Sunset */, set / 60, set % 60);
    else b[0] = 0;
    set_if(s_sun, b);
    snprintf(b, sizeof b, "%s · %s", T_RAW(moon_phase_name_zh(moon_phase8(now)), moon_phase_name(moon_phase8(now))), zone);
    set_if(s_moonname, b);
    if (v.day != s_moon_day) { clock_moon_draw(s_moon, now); s_moon_day = v.day; }
}
static void clock_tap(void) { ui_push(&scr_clock_menu, NULL); }
static lv_obj_t *s_croot, *s_cbox;                         /* s_cbox: the face's objects; a new face cleans and refills it (the face ring and its lines are on the root, above it) */
static int s_cface;                                        /* the face shown */
static bool s_moon_only;                                   /* opened from tools (`tools moon`): the moon face, whatever face is stored, until a face is picked here */
static int s_wface;                                        /* the face axis' working face while its ring is open */
static const ui_axis_ring_t FACE;
static void face_fill(void)                                /* the face again into the same box: what is shown now (a rebuild keeps the page's own face) */
{
    stop_timer(&s_ct);
    clock_face_destroy();
    s_time = NULL;
    s_moon = NULL;
    lv_obj_clean(s_cbox);
    lv_obj_t *p = s_cbox;
    s_cface = ui_axis_ring_up(&FACE) ? s_wface : s_moon_only ? pc_cf_parse("moon") : clock_face_current();
    if (s_cface == pc_cf_parse("hud") && !tface_hud_ok()) s_cface = 0;   /* the HUD art is a plug-in (_player/faces.zip): without it the stored choice stays, the info face shows */
    if (s_cface != 0) { clock_face_create(p, s_cface); return; }
    s_moon = clock_moon_create(p, ui_by_panel(ui_scale_big(64), 150));
    if (s_moon) lv_obj_align(s_moon, LV_ALIGN_CENTER, 0, ui_scale(ui_by_panel(-112, -81)));
    s_moon_day = -1;
    s_date = line(p, font_main(), ui_by_panel(-62, -2));
    s_time = line(p, font_clock(), ui_by_panel(-5, 44));
    s_term = line(p, font_main(), ui_by_panel(52, 90));
    s_sun = line(p, font_main(), ui_by_panel(80, 111));
    s_moonname = line(p, font_main(), ui_by_panel(108, 133));
    s_ct = every(clock_refresh, 1000);
    ui_timer_guard(s_time, &s_ct);                     /* not the root: a new face cleans and refills the box */
    clock_refresh();
}
/* arg non-NULL (console `tools moon`): the moon face whatever face is chosen, cfg untouched (also last_screen) */
static void clock_create(lv_obj_t *p, void *arg)
{
    if (!arg) cfg_set_str("last_screen", "clock");
    s_croot = p;
    s_moon_only = arg != NULL;
    ui_page_tap(p, clock_tap);
    s_cbox = ui_content(p);                                      /* not clickable: taps reach the root */
    face_fill();
}
static void clock_rebuild(void) { if (s_croot) face_fill(); }   /* the face changed: same screen, new contents */

/* ---- the face axis (Task 7, rule R21): the Face cell in the clock ring opens a one-cell ring of axes over the face itself; the knob turns the face both ways,
 * shown at once; 4 s after the last action it closes and the face is stored. Its Back cell returns to the clock ring. ---- */
static const char *face_name(int a) { return a == 1 ? T(S_COLOUR) /* Colour */ : T(S_FACE) /* Face */; }
static const char *face_value(int a) { return a == 1 ? tface_hudcol_label(tface_hudcol()) : clock_face_label(s_wface); }
static const char *face_hint(int a)
{
    static char b[40];
    if (a == 0 && !tface_hud_ok()) return T(S_TF_HUD_NOPACK) /* the reason HUD is not among the faces */;
    snprintf(b, sizeof b, "%s%s", ui_word_knob(), a == 1 ? T(S_HUD_KNOB_COLOUR) /* = change the HUD colour */ : T(S_FACE_2) /* = change face */);
    return b;
}
static bool face_step(int a, int d)
{
    if (a == 1) { int k = pc_hc_count(); tface_hudcol_set(((tface_hudcol() + d) % k + k) % k); if (s_croot) face_fill(); return true; }   /* the HUD colour scheme (a HUD face redraws with it) */
    int n = pc_cf_count(), f = ((s_wface + d) % n + n) % n;
    if (f == pc_cf_parse("hud") && !tface_hud_ok()) f = ((f + d) % n + n) % n;   /* not offered without the zip */
    if (f == s_wface) return false;
    s_wface = f;
    if (s_croot) face_fill();
    return true;
}
static void face_store(void)
{
    s_moon_only = false;                               /* a face was picked on this page: it shows that one from now on */
    if (s_wface != clock_face_current()) clock_face_set(s_wface);
}
static void face_back(void) { ui_axis_ring_close(false); ui_push(&scr_clock_menu, NULL); }
static const ui_axis_ring_t FACE = { .n = 2, .name = face_name, .value = face_value, .hint = face_hint, .step = face_step, .store = face_store, .back = face_back };
static void face_open(void)                            /* the tap on Face: the next face at once, the knob is the face's for 4 s */
{
    if (!s_croot) return;
    s_wface = s_cface;
    ui_axis_ring_open(s_croot, &FACE, 0, true);
}

void clock_face_cmd(const char *name)   /* console: clockface <name|next|moon?|?|ring> */
{
    if (!strcmp(name, "moon?")) { clock_moon_report(clock_now_utc()); return; }
    if (!strcmp(name, "dump")) { clock_face_dump(); return; }   /* the HUD face's data as last set (tests) */
    if (!strcmp(name, "ring")) { if (ui_top() == &scr_clock) face_open(); return; }   /* the Face tap: the face ring with the next face */
    if (!strcmp(name, "?")) { printf("APP clockface shown=%s stored=%s ring=%d\n", s_croot ? pc_cf_name(s_cface) : "-", pc_cf_name(clock_face_current()), ui_axis_ring_up(&FACE)); return; }
    int i = !strcmp(name, "next") ? pc_cf_next(clock_face_current()) : pc_cf_parse(name);
    clock_face_set(i);
    printf("APP clockface set=%s\n", pc_cf_name(i));
    if (ui_top() == &scr_clock) clock_rebuild();
}
bool clock_screen_open(void) { return s_croot != NULL; }   /* one clock screen at a time: its state is static */
static void clock_gone(void) { stop_timer(&s_ct); clock_face_destroy(); s_time = NULL; s_moon = NULL; s_croot = s_cbox = NULL; }   /* an open face ring stores its face when the root deletes it */
const ui_screen_t scr_clock = { .name = "clock", .create = clock_create, .destroy = clock_gone, .on_knob = ui_axis_knob, .on_suspend = ui_axis_stop };

/* ---- the clock menu ---- */
static const ui_screen_t scr_alarm, scr_pomo;
bool clock_show(const char *what)   /* console tests: open a clock sub-screen; false = no such page */
{
    const ui_screen_t *s = !strcmp(what, "pomo") ? &scr_pomo : !strcmp(what, "clockmenu") ? &scr_clock_menu : !strcmp(what, "alarm") ? &scr_alarm
                         : !strcmp(what, "stopwatch") ? &scr_stopwatch : !strcmp(what, "countdown") ? &scr_countdown
                         : !strcmp(what, "drink") ? &scr_drink : !strcmp(what, "habits") ? &scr_habits : NULL;
    if (s) ui_push(s, NULL);
    return s != NULL;
}
enum { M_ALARM, M_POMO, M_STOPWATCH, M_COUNTDOWN, M_DRINK, M_HABITS, M_FACE, M_SETTIME, M_N };
static void menu_pick(int a)
{
    if (a == M_ALARM) ui_replace(&scr_alarm, NULL);
    else if (a == M_POMO) ui_replace(&scr_pomo, NULL);
    else if (a == M_STOPWATCH) ui_replace(&scr_stopwatch, NULL);   /* HUD task: stopwatch and countdown live here beside the alarm and the pomodoro (one ring of time tools, the clock's faces) */
    else if (a == M_COUNTDOWN) ui_replace(&scr_countdown, NULL);
    else if (a == M_DRINK) ui_replace(&scr_drink, NULL);     /* Drink water and Habits (ideas G7) */
    else if (a == M_HABITS) ui_replace(&scr_habits, NULL);
    else if (a == M_FACE) { ui_pop(); face_open(); }   /* the ring goes, the face axis opens over the face: the next face at once */
    else if (a == M_SETTIME) ui_replace(&scr_timeset, NULL);   /* the page for setting the clock by hand (and the Wi-Fi sync, which used to be a cell of its own) */
    else ui_pop();
}
static void menu_close(void) { ui_pop(); }            /* the centre with nothing picked: the ring only, the face is on top */
static void menu_back(void)                            /* the Back cell leaves the clock, as on the spectrum page */
{
    ui_pop();
    if (ui_top() == &scr_clock) ui_pop();
}
static void menu_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    static char face[48];
    snprintf(face, sizeof face, "%s\n%s", T(S_FACE) /* Face */, clock_face_label(clock_face_current()));   /* two lines fit a segment */
    const char *labels[M_N] = { T(S_ALARM) /* Alarm */, T(S_POMODORO) /* Pomodoro */, T(S_STOPWATCH) /* Stopwatch */, T(S_COUNTDOWN) /* Countdown */, T(S_DRINK) /* Drink water */, T(S_HABITS) /* Habits */, face, T(S_SET_TIME) /* Set time */ };
    static const char *const icons[M_N] = { [M_ALARM] = UI_ICON_ALARM, [M_POMO] = UI_ICON_POMODORO, [M_STOPWATCH] = UI_ICON_STOPWATCH, [M_COUNTDOWN] = UI_ICON_COUNTDOWN, [M_DRINK] = UI_ICON_DRINK, [M_HABITS] = UI_ICON_HABITS, [M_FACE] = UI_ICON_FACE, [M_SETTIME] = UI_ICON_CLOCK };
    lv_obj_t *r = ui_ring_create(p, labels, M_N, menu_pick, menu_back);
    ui_ring_set_icons(r, icons, M_N);
    ui_ring_set_system(r, M_SETTIME);
    ui_ring_set_item_now(r, M_FACE);                   /* Face is an axis: its first tap steps it */
    ui_ring_set_close(r, menu_close);
}
static const ui_screen_t scr_clock_menu = { .name = "clock_menu", .create = menu_create };

/* ---- alarm settings ---- */
enum { A_ON, A_HOUR, A_MIN, A_DAYS, A_SOUND, A_USE_NOW, A_TEST, A_N };
static lv_obj_t *s_arow[A_N], *s_alist;
static const char *days_label(uint8_t d)
{
    return d == PC_DAYS_EVERY ? T(S_EVERY_DAY) /* Daily */ : d == PC_DAYS_WEEKDAYS ? T(S_WEEKDAYS) /* Weekdays */ : d == PC_DAYS_WEEKEND ? T(S_WEEKEND) /* Weekend */ : T(S_ONCE) /* Once */;
}
static void alarm_refresh(void)
{
    pc_alarm_t *a = clock_alarm();
    char b[200];
    for (int i = 0; i < A_N; i++) {
        switch (i) {
        case A_ON: snprintf(b, sizeof b, "%s: %s%s", T(S_ALARM) /* Alarm */, a->on ? T(S_ON) /* On */ : T(S_OFF) /* Off */, clock_time_valid() ? "" : T(S_NOT_SYNCED_2) /* (time not set yet) */); break;
        case A_HOUR: snprintf(b, sizeof b, "%s: %02d", T(S_HOUR) /* Hour */, a->min / 60); break;
        case A_MIN: snprintf(b, sizeof b, "%s: %02d", T(S_MINUTE) /* Min */, a->min % 60); break;
        case A_DAYS: snprintf(b, sizeof b, "%s: %s", T(S_REPEAT) /* Repeat */, days_label(a->days)); break;   /* an axis (four choices): tap, then the knob */
        case A_SOUND: {
            const char *s = clock_alarm_sound(), *base = strrchr(s, '/');
            snprintf(b, sizeof b, "%s: %s", T(S_SOUND) /* Ringtone */, s[0] ? (base ? base + 1 : s) : T(S_BUILT_IN_CHIME) /* Built-in chime */);
            break;
        }
        case A_USE_NOW: snprintf(b, sizeof b, "%s", T(S_USE_THE_PLAYING_SONG) /* Use the song now playing as the ringtone */); break;
        default: snprintf(b, sizeof b, "%s", T(S_TEST_THE_ALARM_STOPS_PLAYBACK) /* Preview ringtone (stops the current playback) */); break;
        }
        lv_label_set_text(lv_obj_get_child(s_arow[i], 0), b);
        lv_obj_set_style_bg_color(s_arow[i], i == ui_axis_current_of(s_alist) ? UI_SEL : (i == A_USE_NOW || i == A_TEST) ? UI_SYS : UI_PANEL, 0);
    }
}
/* Hour (24, wraps) and Min (12 steps of 5, wraps inside the hour: no carry into the hour) are axes: the tap steps them, then the knob for 4 s (UI unify T5).
 * A minute that is not a multiple of 5 first goes to the next one in the direction of the step (31 +1 -> 35, -1 -> 30). */
static bool time_step(int axis, int d)
{
    pc_alarm_t *a = clock_alarm();
    int h = a->min / 60, m = a->min % 60;
    if (axis == A_HOUR) h = ((h + d) % 24 + 24) % 24;
    else {
        if (m % 5) { if (d > 0) { m += 5 - m % 5; d--; } else if (d < 0) { m -= m % 5; d++; } }
        m = ((m / 5 + d) % 12 + 12) % 12 * 5;
    }
    a->min = h * 60 + m;
    clock_alarm_saved();
    alarm_refresh();
    return true;
}
/* Repeat (four choices) is an axis (Task 7, rule R21): the tap steps it, then the knob steps it for 4 s instead of scrolling */
static bool days_step(int axis, int d)
{
    (void)axis;
    static const uint8_t D[4] = { PC_DAYS_WEEKDAYS, PC_DAYS_EVERY, PC_DAYS_WEEKEND, PC_DAYS_ONCE };
    pc_alarm_t *a = clock_alarm();
    int i = 0;
    while (i < 4 && D[i] != a->days) i++;
    a->days = D[((i + d) % 4 + 4) % 4];
    clock_alarm_saved();
    alarm_refresh();
    return true;
}
static void alarm_row(lv_event_t *e)
{
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    pc_alarm_t *a = clock_alarm();
    if (r == A_SOUND) {                                                    /* two taps: the first asks, the second goes back to the chime */
        if (!clock_alarm_sound()[0]) return;                               /* already the built-in chime: nothing to reset */
        if (!ui_arm_tap(lv_event_get_target(e), T(S_TAP_AGAIN_BACK_TO_THE_BUILT_IN_CHIME) /* Tap again: back to the built-in chime */)) return;
    } else ui_arm_cancel();
    if (r == A_DAYS) { ui_axis_borrow(s_alist, A_DAYS, days_step, alarm_refresh, true); return; }
    if (r == A_HOUR || r == A_MIN) { ui_axis_borrow(s_alist, r, time_step, alarm_refresh, true); return; }
    if (ui_axis_current_of(s_alist) >= 0) ui_axis_end();                  /* another row: the knob scrolls again */
    if (r == A_ON) { a->on = !a->on; clock_alarm_saved(); if (a->on && !clock_time_valid()) clock_sync_now(); }
    else if (r == A_SOUND) clock_alarm_set_sound("");                  /* back to the built-in chime */
    else if (r == A_USE_NOW) {
        int t = mp3_ctl_current();
        if (t < 0) { ui_push_message(T(S_PLAY_AN_MP3_FIRST) /* Play an MP3 first */); return; }
        clock_alarm_set_sound(pc_str(&g_lib, g_lib.tracks[t].path));
    } else if (r == A_TEST) clock_alarm_test(true);
    alarm_refresh();
}
static bool alarm_knob(int d)
{
    /* the scroll path always takes the knob: the list always has more rows than fit (same as the settings page) */
    if (ui_axis_knob(d)) { ui_arm_cancel(); return true; }           /* Hour, Min and Repeat have the knob for 4 s */
    return ui_list_knob(s_alist, d);                                  /* no row holds the knob: it scrolls the column */
}
void clock_alarm_tap(int row) { if (ui_top() == &scr_alarm && s_alist && row >= 0 && row < A_N) lv_obj_send_event(s_arow[row], LV_EVENT_CLICKED, NULL); }   /* console "alarm tap N" (baseline pictures) */
static void alarm_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list(p), &s_alist);
    for (int i = 0; i < A_N; i++) {
        lv_obj_t *b = ui_list_row(s_alist, "", UI_PANEL, alarm_row, (void *)(intptr_t)i);
        lv_obj_t *l = lv_obj_get_child(b, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);             /* long texts take a second line instead of being cut */
        lv_obj_set_height(l, LV_SIZE_CONTENT);
        s_arow[i] = b;
    }
    ui_list_help_row(s_alist, &HELP_ALARM);                              /* the last row: Repeat, needs time sync, ringtone */
    ui_back_button(p);
    alarm_refresh();
}
static void alarm_destroy(void) { clock_alarm_test(false); }
static const ui_screen_t scr_alarm = { .name = "alarm", .create = alarm_create, .destroy = alarm_destroy, .on_knob = alarm_knob };

/* ---- pomodoro ---- */
/* The page is a face (tface.c: info / rings / hud, cfg pomoface, default rings) with the time and the phase text. A tap on it opens the ring (UI unify T7, spec 4.3): the centre starts / pauses / resumes,
 * the Stop cell is an armed cell (first tap arms for 4 s) and exists only while running or paused, the Face cell (borrowed knob) changes the look, the Back cell pops the page. A centre action leaves the ring open (new centre text, 4 s again). */
static lv_obj_t *s_pbox, *s_pface, *s_proot;
static int s_pkind;
static int pomo_face_cell(void) { return clock_pomo()->phase == PC_POMO_IDLE ? 0 : 1; }   /* the Face cell: the only one while idle, after Stop while running */
static void pomo_refresh(void)
{
    if (!s_pface || !ui_screen_on()) return;
    pc_pomo_t *p = clock_pomo();
    int64_t now = esp_timer_get_time() / 1000000;
    int left = pc_pomo_left(p, now), total = p->phase == PC_POMO_FOCUS ? p->focus_s : p->phase == PC_POMO_LONG ? p->long_s : p->break_s;
    tface_data_t d;
    memset(&d, 0, sizeof d);
    if (p->phase == PC_POMO_IDLE) {
        snprintf(d.big, sizeof d.big, "25:00");
        snprintf(d.line[0], sizeof d.line[0], "%s", T(S_TAP_TO_START) /* Tap to start focusing */);
        d.prog_pm = 0;
    } else {
        snprintf(d.big, sizeof d.big, "%02d:%02d", left / 60, left % 60);
        if (p->paused) snprintf(d.line[0], sizeof d.line[0], "%s", T(S_PAUSED_TAP_TO_RESUME) /* Paused・tap to resume */);
        else snprintf(d.line[0], sizeof d.line[0], "%s · %s %d", p->phase == PC_POMO_FOCUS ? T(S_FOCUS) /* Focus */ : p->phase == PC_POMO_LONG ? T(S_LONG_BREAK) /* Long break */ : T(S_BREAK) /* Break */,
                 T(S_DONE_2) /* Done */, p->done);
        d.prog_pm = total ? 1000 - left * 1000 / total : 0;
    }
    d.nline = 1;
    if (tface_kind(s_pface) == PC_TF_HUD) {                              /* the designed face: fixed formats; phase 0 idle 1 focus 2 break 3 long break 4 paused */
        snprintf(d.art.v[FAV_MAIN], sizeof d.art.v[0], "%s", d.big);
        snprintf(d.art.v[FAV_PHASE], sizeof d.art.v[0], "%d", p->phase == PC_POMO_IDLE ? 0 : p->paused ? 4 : p->phase == PC_POMO_FOCUS ? 1 : p->phase == PC_POMO_LONG ? 3 : 2);
        snprintf(d.art.v[FAV_ROUND], sizeof d.art.v[0], "%02d", p->done % 100);
        snprintf(d.art.v[FAV_PROG], sizeof d.art.v[0], "%d", p->phase == PC_POMO_IDLE ? 0 : total ? (total - left) * 20 / total : 0);   /* 20 segments: done so far */
        d.art.warn = tface_common(&d.art) & 2;              /* FakePod battery low */
    }
    tface_set(s_pface, &d);
}
static void pomo_face_fill(void)
{
    lv_obj_clean(s_pbox);
    s_pface = ui_obj_guard(tface_create(s_pbox, s_pkind, TF_POMO), &s_pface);
    pomo_refresh();
}
static const char *pomo_cell(int i)
{
    static char b[40];
    if (i == pomo_face_cell()) { snprintf(b, sizeof b, "%s\n%s", T(S_FACE) /* Face */, tface_label(s_pkind)); return b; }
    if (i == pomo_face_cell() + 1) { snprintf(b, sizeof b, "%s\n%s", T(S_COLOUR) /* Colour */, tface_hudcol_label(tface_hudcol())); return b; }
    return T(S_STOP) /* Stop */;
}
static const char *pomo_centre(void)
{
    pc_pomo_t *p = clock_pomo();
    return p->phase == PC_POMO_IDLE ? T(S_START) /* Start */ : p->paused ? T(S_RESUME) /* Resume */ : T(S_PAUSE) /* Pause */;
}
static const char *pomo_armed(int i) { (void)i; return T(S_TAP_AGAIN_TO_STOP) /* Tap again to stop */; }
static void pomo_pick(int i);
static void pomo_back(void);
static bool pomo_step(int axis, int d)
{
    if (axis == pomo_face_cell() + 1) { int n = pc_hc_count(); tface_hudcol_set(((tface_hudcol() + d) % n + n) % n); pomo_refresh(); return true; }   /* the HUD colour scheme */
    if (axis != pomo_face_cell()) return false;
    s_pkind = tface_step(s_pkind, d, TF_POMO);
    tface_pref_set("pomoface", s_pkind);
    pomo_face_fill();
    ui_popring_raise();
    return true;
}
/* two const defs (flash, 0 B RAM): idle has the Face and Colour cells (0, 1), running or paused has Stop (cell 0, armed), Face (1) and Colour (2); a state change reopens the ring with the right one (silently, 4 s again) */
static const ui_popring_t s_pdef_idle = { .n = 2, .label = pomo_cell, .pick = pomo_pick, .centre = pomo_centre, .back = pomo_back, .axis_mask = 3u, .step = pomo_step, .help = &HELP_POMO };
static const ui_popring_t s_pdef_run = { .n = 3, .label = pomo_cell, .pick = pomo_pick, .centre = pomo_centre, .back = pomo_back, .axis_mask = 6u, .step = pomo_step, .arm_mask = 1u, .armed = pomo_armed, .help = &HELP_POMO };
static const ui_popring_t *pomo_def(void) { return clock_pomo()->phase == PC_POMO_IDLE ? &s_pdef_idle : &s_pdef_run; }
static void pomo_after(void) { pomo_refresh(); ui_popring_open(s_proot, pomo_def(), 0); }
static void pomo_pick(int i)
{
    pc_pomo_t *p = clock_pomo();
    if (i == -1) { if (p->phase == PC_POMO_IDLE) clock_pomo_start(); else if (p->paused) clock_pomo_resume(); else clock_pomo_pause(); }
    else clock_pomo_stop();                              /* the armed cell, second tap */
    pomo_after();
}
static void pomo_back(void) { ui_popring_close(); ui_pop(); }
static void pomo_open(void) { ui_popring_open(s_proot, pomo_def(), 0); }
static void pomo_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_proot = p;
    s_pkind = tface_pref_feat("pomoface", PC_TF_RINGS, TF_POMO);
    ui_page_tap(p, pomo_open);
    s_pbox = ui_content(p);
    s_pface = ui_obj_guard(tface_create(s_pbox, s_pkind, TF_POMO), &s_pface);
    s_pt = every(pomo_refresh, 250);
    ui_timer_guard(p, &s_pt);
    pomo_refresh();
}
static void pomo_destroy(void) { if (ui_popring_up(&s_pdef_idle) || ui_popring_up(&s_pdef_run)) ui_popring_close(); stop_timer(&s_pt); s_pface = s_pbox = s_proot = NULL; }
void clock_pomo_stat(void)                               /* console "pomo stat" */
{
    pc_pomo_t *p = clock_pomo();
    int left = pc_pomo_left(p, esp_timer_get_time() / 1000000);
    printf("APP pomo state=%s left=%d\n", p->phase == PC_POMO_IDLE ? "idle" : p->paused ? "pause" : "run", p->phase == PC_POMO_IDLE ? p->focus_s : left);
}
static const ui_screen_t scr_pomo = { .name = "pomo", .create = pomo_create, .destroy = pomo_destroy };

/* ---- ringing ---- */
static lv_obj_t *s_rtime;
static void ring_refresh(void)
{
    if (!s_rtime) return;
    if (!clock_ringing()) { ui_pop(); return; }          /* stopped elsewhere (timeout); destroy clears the timer */
    clock_civil_t v;
    clock_civil_from_utc(clock_now_utc(), clock_tz_min(), &v);
    lv_label_set_text_fmt(s_rtime, "%02d:%02d", v.hour, v.minute);
}
static void ring_tap(void) { clock_ring_stop(); ui_pop(); }
static bool ring_knob(int d) { (void)d; clock_ring_snooze(); ui_pop(); ui_push_message(T(S_AGAIN_IN_9_MIN) /* Snooze 9 minutes */); return true; }
static void ring_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    line(p, font_large(), -80);
    lv_label_set_text(lv_obj_get_child(p, -1), T(S_ALARM) /* Alarm */);
    s_rtime = line(p, font_clock(), -10);
    lv_obj_t *h = line(p, font_main(), 70);
    lv_label_set_text_fmt(h, "%s\n%s%s%s", T(S_TAP_TO_STOP) /* Tap to stop */, T(S_TURN_THE) /* turn */, ui_word_knob(), T(S_AGAIN_IN_9_MIN_2) /* : snooze 9 minutes */);
    ui_page_tap(p, ring_tap);
    s_rt = every(ring_refresh, 1000);
    ui_timer_guard(p, &s_rt);
    ring_refresh();
}
static void ring_destroy(void) { stop_timer(&s_rt); s_rtime = NULL; }
const ui_screen_t scr_ring = { .name = "ring", .create = ring_create, .destroy = ring_destroy, .on_knob = ring_knob };
