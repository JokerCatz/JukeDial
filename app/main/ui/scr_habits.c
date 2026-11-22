// SPDX-License-Identifier: LGPL-2.1-or-later
/* Habits (ideas G7, clock family: the clock ring's Habits): 2, 4 or 8 habits as the sectors of ui_sectors, each named from a preset list (16 names; no typing on the
 * device). A tap on a sector = done today (one mark a local day; a second tap does nothing) and makes it the selected habit; a done sector is drawn lit. The title line says
 * "<name> · streak N" of the selected one; the left cell of the HEAD row is its 7-day strip (oldest first, today last: # done, . not), the right cell opens the page's ring
 * (ui_popring): Count (2 / 4 / 8, the knob), This cell (the selected sector's name: the knob steps through the presets that are not on another sector), Undo today (armed: today's
 * mark of the selected habit was a mistake), 說明 last. A missed day breaks the streak (pc_habit.h has the rules); midnight (local time of the clock's time zone) starts a new day:
 * the 1 s timer redraws the sectors and the strip.
 * Records are per PRESET, not per sector: changing a sector's name never loses or mixes a history. Keys (gamesave_svc, games.txt): hab.n, hab.p<slot> (the slot's preset),
 * hab.d<preset> (last day marked), hab.m<preset> (32-day mask), hab.s<preset> (streak). Without a synced clock the page says so and marks nothing.
 * TODO(web): the web page may later add names; habit_label() is the one place that turns a preset into text, a custom-name table would be consulted there (not built).
 * Statics are PSRAM .bss (built with -msmall-data-limit=0). Console `habit`: "" dumps "APP habit n=<n> sel=<i> today=<day> h=<slot>:<preset>:<streak>:<strip>,..."; `habit tap <i>`,
 * `habit sel <i>`, `habit ring`, `habit set <n>`, `habit undo`, `habit reset` (every record and the slots back to the defaults: tests, pictures), `habit seed <slot> <strip>`
 * (7 characters # or ., today last, marks those days: after reset), `habit name <step>` (the This-cell axis). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clock_svc.h"
#include "font.h"
#include "gamesave_svc.h"
#include "pc/pc_habit.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_help.h"
#include "ui_frame.h"
#include "ui_kit.h"
#include "ui_popring.h"
#include "ui_sectors.h"
#include "pc/pc_dial.h"

#define PRESETS 16
#define FLASH_MS 140
enum { H_COUNT, H_NAME, H_UNDO, H_N };
static const int COUNTS[3] = { 2, 4, 8 };
static const char *const ZH[PRESETS] = { "運動", "閱讀", "早睡", "早起", "冥想", "學習", "伸展", "日記", "喝水", "吃藥", "散步", "牙線", "整理", "感恩", "練琴", "戒糖" };
static const char *const EN[PRESETS] = { "Move", "Read", "Bed", "Rise", "Zen", "Study", "Flex", "Diary", "Water", "Meds", "Walk", "Floss", "Tidy", "Thanks", "Piano", "Diet" };
/* eight sectors are narrow (font_large text of two characters collides): one Chinese character or up to three letters on the sector, the title has the full name */
static const char *const ZH1[PRESETS] = { "運", "閱", "睡", "起", "冥", "學", "伸", "記", "水", "藥", "步", "線", "理", "謝", "琴", "糖" };
static const char *const EN3[PRESETS] = { "Mov", "Rea", "Bed", "Up", "Zen", "Stu", "Flx", "Log", "H2O", "Med", "Wlk", "Flo", "Tdy", "Thx", "Pno", "Dt" };

static lv_obj_t *s_page, *s_sec, *s_title, *s_cells[2];
static int s_n, s_sel;
static int s_slot[UI_SECTORS_MAX];                   /* slot -> preset */
static pc_hb_t s_rec[PRESETS];
static int32_t s_shown;                              /* the day the sectors were drawn for */
static ui_popring_t s_pop;

static int count_index(int n) { for (int i = 0; i < 3; i++) if (COUNTS[i] == n) return i; return 1; }
static const char *habit_label(int preset) { return T_RAW(ZH[preset], EN[preset]); }     /* the one place a preset becomes text (TODO(web) above) */
static const char *sector_label(int preset) { return s_n == 8 ? T_RAW(ZH1[preset], EN3[preset]) : habit_label(preset); }
static int32_t today(void) { return clock_time_valid() ? pc_hb_local_day(clock_now_utc(), clock_tz_min()) : 0; }

static void key(char *b, size_t n, char k, int p) { snprintf(b, n, "hab.%c%d", k, p); }
static void save_rec(int p)
{
    char k[12];
    key(k, sizeof k, 'd', p); gamesave_set(k, (uint32_t)s_rec[p].last);
    key(k, sizeof k, 'm', p); gamesave_set(k, s_rec[p].mask);
    key(k, sizeof k, 's', p); gamesave_set(k, s_rec[p].streak);
}
static void save_slots(void)
{
    gamesave_set("hab.n", (uint32_t)s_n);
    for (int i = 0; i < UI_SECTORS_MAX; i++) { char k[12]; key(k, sizeof k, 'p', i); gamesave_set(k, (uint32_t)s_slot[i]); }
}
static bool used_by_other(int slot, int preset)
{
    for (int i = 0; i < s_n; i++) if (i != slot && s_slot[i] == preset) return true;
    return false;
}
static void fix_slots(void)                          /* no preset on two visible slots (a hidden slot's stored name may collide after the count grew) */
{
    for (int i = 1; i < s_n; i++)
        for (int j = 0; j < i; ) { if (s_slot[j] == s_slot[i]) { s_slot[i] = (s_slot[i] + 1) % PRESETS; j = 0; } else j++; }
}
static void load(void)
{
    s_n = COUNTS[count_index((int)gamesave_get_or("hab.n", 4))];
    for (int i = 0; i < UI_SECTORS_MAX; i++) { char k[12]; key(k, sizeof k, 'p', i); s_slot[i] = pc_wrapi((int)gamesave_get_or(k, (uint32_t)i), PRESETS); }
    fix_slots();
    for (int p = 0; p < PRESETS; p++) {
        char k[12];
        key(k, sizeof k, 'd', p); s_rec[p].last = (int32_t)gamesave_get_or(k, 0);
        key(k, sizeof k, 'm', p); s_rec[p].mask = gamesave_get_or(k, 0);
        key(k, sizeof k, 's', p); s_rec[p].streak = gamesave_get_or(k, 0);
    }
}

static void set_if(lv_obj_t *l, const char *t) { if (l && strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t); }
static void refresh(void)
{
    if (!s_page) return;
    int32_t td = today();
    s_shown = td;
    static char b[64];
    if (s_sec) {
        for (int i = 0; i < s_n; i++) {
            ui_sectors_set_text(s_sec, i, sector_label(s_slot[i]));
            ui_sectors_set_look(s_sec, i, pc_hb_done(&s_rec[s_slot[i]], td) ? UI_SEC_LIT : UI_SEC_NORMAL);
        }
    }
    int p = s_slot[s_sel];
    if (!td) snprintf(b, sizeof b, "%s", T(S_NOT_SYNCED) /* Time not set yet */);
    else snprintf(b, sizeof b, "%s · %s %u", habit_label(p), T(S_STREAK) /* Streak */, (unsigned)pc_hb_streak(&s_rec[p], td));
    set_if(s_title, b);
    if (s_sec && !s_title) ui_sectors_set_status(s_sec, b);          /* a page with a frame: the status stays in the content */
    char sb[PC_HB_STRIP + 1];
    pc_hb_strip(&s_rec[p], td, sb, sizeof sb);           /* # done, . not: plain ASCII, narrow enough for the cell and no glyph to miss */
    if (s_cells[0]) ui_choice_set(s_cells[0], sb, UI_LOOK_DIM);
}

static void mark(int slot)
{
    s_sel = slot;
    int32_t td = today();
    if (!td) { ui_push_message(T(S_THE_TIME_IS_NOT_SET_YET) /* Time not set yet */); refresh(); return; }
    int p = s_slot[slot];
    if (pc_hb_mark(&s_rec[p], td)) {
        save_rec(p);
        sfx_play(s_rec[p].streak % 7 == 0 ? "win" : "ok");
        printf("APP habit mark slot=%d preset=%d streak=%u\n", slot, p, (unsigned)s_rec[p].streak);
    }
    refresh();
}
static void tapped(int i, void *user) { (void)user; mark(i); }

/* ---- the ring ---- */
static const char *cell_label(int i)
{
    static char b[H_N][40];
    if (i == H_COUNT) snprintf(b[i], sizeof b[i], "%s\n%d", T(S_HABITS_2) /* Count */, s_n);
    else if (i == H_NAME) snprintf(b[i], sizeof b[i], "%s\n%s", T(S_THIS_ONE) /* This cell */, habit_label(s_slot[s_sel]));
    else snprintf(b[i], sizeof b[i], "%s", T(S_UNDO_TODAY) /* Undo today */);
    return b[i];
}
static const char *armed_text(int i) { (void)i; return T(S_TAP_AGAIN_UNDO_TODAY_S_TICK) /* Tap again: undo today's check-in */; }
static void build(void);
static bool name_step(int d)
{
    int p = s_slot[s_sel], q = p;
    for (int k = 0; k < PRESETS; k++) {
        q = pc_wrapi(q + (d < 0 ? -1 : 1), PRESETS);
        if (!used_by_other(s_sel, q)) break;
    }
    if (q == p) return false;
    s_slot[s_sel] = q;
    save_slots();
    return true;
}
static bool ring_step(int axis, int d)
{
    if (axis == H_COUNT) { s_n = COUNTS[pc_wrapi(count_index(s_n) + d, 3)]; fix_slots(); if (s_sel >= s_n) s_sel = 0; save_slots(); build(); }
    else if (axis == H_NAME) { if (!name_step(d)) return false; refresh(); }
    else return false;
    printf("APP habit setup n=%d sel=%d name=%d\n", s_n, s_sel, s_slot[s_sel]);
    return true;
}
static void undo_today(void)
{
    int p = s_slot[s_sel];
    if (pc_hb_unmark(&s_rec[p], today())) { save_rec(p); ui_haptic_strong(); printf("APP habit undo slot=%d preset=%d\n", s_sel, p); }
    refresh();
}
static void ring_pick(int i)
{
    if (i == H_UNDO) { ui_popring_close(); undo_today(); }
    else if (i < 0) ui_popring_close();
}
static void ring_open(void)
{
    s_pop = (ui_popring_t){ .n = H_N, .label = cell_label, .pick = ring_pick, .step = ring_step, .axis_mask = 1u << H_COUNT | 1u << H_NAME,
                            .arm_mask = 1u << H_UNDO, .armed = armed_text, .help = &HELP_HABITS };
    ui_popring_open(s_page, &s_pop, 0);
}
static void head_tap(lv_event_t *e) { if ((int)(intptr_t)lv_event_get_user_data(e) == 1) ring_open(); }

/* ---- the page ---- */
static void build(void)                              /* (re)make the sectors for s_n */
{
    if (s_sec) lv_obj_delete(s_sec);                 /* the guard clears s_sec */
    const ui_sectors_def_t def = { .n = s_n, .rot = s_n == 2 ? 270 : s_n == 4 ? 45 : 23, .hub_pct = 0, .tap_flash_ms = FLASH_MS, .tones = NULL, .progress = false, .frame = true };
    ui_obj_guard(ui_sectors_create(s_page, &def, tapped, NULL, NULL), &s_sec);
    if (!s_sec) return;
    lv_obj_move_to_index(s_sec, 0);                  /* behind the HEAD row, the title and Back */
    refresh();
}
static void timer_cb(lv_timer_t *t) { (void)t; if (today() != s_shown) refresh(); }
static bool on_knob(int d) { return ui_axis_knob(d) || (s_sec && ui_sectors_knob(s_sec, d)); }

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_page = ui_obj_guard(p, &s_page);
    s_sec = NULL;
    s_sel = 0;
    s_shown = -1;
    load();
    ui_frame_enable(p);                                  /* the round glass: a pilot page of the content-first frame (ui_frame.h) */
    s_title = NULL;
    if (!ui_frame_active(p)) s_title = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_title);
    ui_frame_option(ui_choice_row(p, UI_SLOT_HEAD, 2, head_tap, s_cells));
    ui_obj_guard(s_cells[0], &s_cells[0]);
    ui_obj_guard(s_cells[1], &s_cells[1]);
    if (s_cells[1]) ui_choice_set(s_cells[1], T(S_MENU) /* Menu */, UI_LOOK_NORMAL);
    build();
    ui_back_button(p);
    ui_timer_bind(p, timer_cb, 1000, NULL);          /* midnight */
}
static void destroy(void) { gamesave_flush(); s_sec = NULL; s_cells[0] = s_cells[1] = NULL; s_title = NULL; s_page = NULL; }

const ui_screen_t scr_habits = { .name = "clock_habits", .create = create, .destroy = destroy, .on_knob = on_knob, .content = true };

void habit_cmd(const char *a)
{
    int v;
    if (ui_top() != &scr_habits) { printf("APP habit none\n"); return; }
    if (sscanf(a, "tap %d", &v) == 1) { if (v >= 0 && v < s_n) ui_sectors_tap(s_sec, v); }
    else if (sscanf(a, "sel %d", &v) == 1) { if (v >= 0 && v < s_n) { s_sel = v; refresh(); } }
    else if (!strcmp(a, "ring")) ring_open();
    else if (!strcmp(a, "undo")) undo_today();
    else if (sscanf(a, "name %d", &v) == 1) { if (name_step(v)) refresh(); }
    else if (sscanf(a, "set %d", &v) == 1) { s_n = COUNTS[count_index(v)]; fix_slots(); if (s_sel >= s_n) s_sel = 0; save_slots(); build(); }
    else if (!strcmp(a, "reset")) {
        for (int i = 0; i < UI_SECTORS_MAX; i++) s_slot[i] = i;
        for (int p = 0; p < PRESETS; p++) { s_rec[p] = (pc_hb_t){ 0 }; save_rec(p); }
        s_sel = 0;
        save_slots();
        refresh();
    }
    else if (!strncmp(a, "seed ", 5) && today() && sscanf(a + 5, "%d", &v) == 1 && v >= 0 && v < s_n && strlen(a + 5) >= 3) {
        const char *st = strrchr(a, ' ') + 1;
        int p = s_slot[v], len = (int)strlen(st);
        for (int i = 0; i < len && i < PC_HB_STRIP; i++) if (st[i] == '#') pc_hb_mark(&s_rec[p], today() - (PC_HB_STRIP - 1) + i);
        save_rec(p);
        refresh();
    }
    else if (a[0]) { printf("APP habit unknown %s\n", a); return; }
    printf("APP habit n=%d sel=%d today=%d h=", s_n, s_sel, (int)today());
    for (int i = 0; i < s_n; i++) {
        char st[PC_HB_STRIP + 1];
        pc_hb_strip(&s_rec[s_slot[i]], today(), st, sizeof st);
        printf("%s%d:%d:%u:%s", i ? "," : "", i, s_slot[i], (unsigned)pc_hb_streak(&s_rec[s_slot[i]], today()), st);
    }
    printf("\n");
}
