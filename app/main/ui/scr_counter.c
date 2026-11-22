// SPDX-License-Identifier: LGPL-2.1-or-later
/* Counters (ideas wave 0b G2, a tools item, not a game): 2, 4 or 8 counters as the sectors of ui_sectors (one big number each). A tap on a sector
 * counts it +1 (a short light and a haptic tap, from ui_sectors) and makes it the selected one; the borrowed knob (ui_axis_borrow, the usual 4 s) then adds or
 * takes away one on the selected counter. Every 10th count plays the bank's "tick". The HEAD row's one cell opens the page's ring (ui_popring): counter count (2 / 4 / 8,
 * three values: the knob), name (score / laps / times / A-H: a preset, no typing), 風格 (game_style), reset (the armed tap: every counter to 0), 說明 last.
 * The selected sector is the one drawn at full strength, the others dim; the title line says "<name>: <value>" of the selected one.
 * Values, count, name and style persist through gamesave_svc (keys ctr.n, ctr.nm, ctr.st, ctr.v0..ctr.v7; games.txt). Only shared pieces are composed here;
 * the statics are PSRAM .bss (built with -msmall-data-limit=0). Console `counter`: "" dumps "APP counter n=<n> sel=<i> name=<k> style=<k> v=<values>",
 * `counter tap <i>` a touch on counter i (ui_sectors_tap), `counter sel <i>` selects without counting, `counter ring` opens the ring (popring), `counter reset` zeroes at once, `counter set <n> <name> <style>` sets the three as the ring would. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_style.h"
#include "gamesave_svc.h"
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

#define CTR_MAX   9999                        /* a sector shows four digits */
#define CTR_TICK  10                          /* every 10th count: a milestone tick */
#define FLASH_MS  140
enum { C_COUNT, C_NAME, C_STYLE, C_RESET, C_N };
enum { NAME_SCORE = 0, NAME_LAPS, NAME_TIMES, NAME_LETTERS, NAMES };
static const int COUNTS[3] = { 2, 4, 8 };

static lv_obj_t *s_page, *s_sec, *s_title, *s_cell;
static int s_n, s_sel, s_name, s_style;
static int32_t s_v[UI_SECTORS_MAX];
static ui_popring_t s_pop;

static int count_index(int n) { for (int i = 0; i < 3; i++) if (COUNTS[i] == n) return i; return 1; }

static const char *name_of(int i)
{
    static char b[24];
    static const char *const ZH[3] = { "分數", "圈數", "次數" }, *const EN[3] = { "Score", "Laps", "Times" };
    if (s_name == NAME_LETTERS) snprintf(b, sizeof b, "%c", 'A' + i);
    else snprintf(b, sizeof b, "%s %d", T_RAW(ZH[s_name], EN[s_name]), i + 1);
    return b;
}
static const char *preset_name(int k)
{
    static const char *const ZH[NAMES] = { "分數", "圈數", "次數", "A-H" }, *const EN[NAMES] = { "Score", "Laps", "Times", "A-H" };
    return T_RAW(ZH[k], EN[k]);
}

static void show_value(int i)
{
    char b[12];
    snprintf(b, sizeof b, "%d", (int)s_v[i]);
    ui_sectors_set_text(s_sec, i, b);
}
static void show_title(void)
{
    static char b[48];
    snprintf(b, sizeof b, "%s: %d", name_of(s_sel), (int)s_v[s_sel]);
    if (s_title) lv_label_set_text(s_title, b);
    else if (s_sec) ui_sectors_set_status(s_sec, b);     /* a page with a frame: the status stays in the content */
}
static void show_looks(void)
{
    for (int i = 0; i < s_n; i++) ui_sectors_set_look(s_sec, i, i == s_sel ? UI_SEC_NORMAL : UI_SEC_DIM);
}
static void save_value(int i) { char k[12]; snprintf(k, sizeof k, "ctr.v%d", i); gamesave_set(k, (uint32_t)s_v[i]); }
static void save_setup(void) { gamesave_set("ctr.n", (uint32_t)s_n); gamesave_set("ctr.nm", (uint32_t)s_name); gamesave_set("ctr.st", (uint32_t)s_style); }

static void apply_style(void) { ui_sectors_set_style(s_sec, game_style(s_style)); }

static void tapped(int i, void *user);
static void build(void)                                  /* (re)make the sectors for s_n (2, 4 or 8) */
{
    if (s_sec) lv_obj_delete(s_sec);                     /* the guard clears s_sec */
    const ui_sectors_def_t def = { .n = s_n, .rot = s_n == 2 ? 270 : s_n == 4 ? 45 : 23, .hub_pct = 0, .tap_flash_ms = FLASH_MS, .tones = NULL, .progress = false, .frame = true };
    ui_obj_guard(ui_sectors_create(s_page, &def, tapped, NULL, NULL), &s_sec);
    if (!s_sec) return;
    lv_obj_move_to_index(s_sec, 0);                      /* behind the HEAD row, the title and Back */
    if (s_sel >= s_n) s_sel = 0;
    apply_style();
    for (int i = 0; i < s_n; i++) show_value(i);
    show_looks();
    show_title();
}

static void count(int i, int d, bool tap)                /* the selected counter moves by d (a tap: +1; the knob: either way) */
{
    int32_t v = s_v[i] + d;
    v = v < 0 ? 0 : v > CTR_MAX ? CTR_MAX : v;
    bool changed = v != s_v[i];
    s_sel = i;
    s_v[i] = v;
    show_value(i);
    show_looks();
    show_title();
    if (changed) save_value(i);
    if (changed && !tap) { ui_sectors_flash(s_sec, i, FLASH_MS, false); ui_haptic_tick(); }    /* a tap's light and haptic come from ui_sectors */
    bool tick = changed && d > 0 && v % CTR_TICK == 0;
    if (tick) sfx_play("tick");
    printf("APP counter %d=%d n=%d%s\n", i, (int)v, s_n, tick ? " sound=tick" : "");
}

static bool knob_step(int axis, int d) { (void)axis; count(s_sel, d, false); return true; }

static void tapped(int i, void *user)
{
    (void)user;
    count(i, 1, true);
    ui_axis_borrow(s_page, 0, knob_step, NULL, false);   /* the knob adjusts it now (a tap already counted: no second step) */
}

/* ---- the ring ---- */
static const char *cell_label(int i)
{
    static char b[C_N][40];                                /* the ring asks for every cell before it copies any */
    if (i == C_COUNT) snprintf(b[i], sizeof b[i], "%s\n%d", T(S_COUNTERS) /* Count */, s_n);
    else if (i == C_NAME) snprintf(b[i], sizeof b[i], "%s\n%s", T(S_NAMES) /* Name */, preset_name(s_name));
    else if (i == C_STYLE) snprintf(b[i], sizeof b[i], "%s\n%s", T(S_STYLE) /* Style */, game_style_name(s_style));
    else snprintf(b[i], sizeof b[i], "%s", T(S_ZERO) /* Reset */);
    return b[i];
}
static const char *armed_text(int i) { (void)i; return T(S_TAP_AGAIN_ZERO_ALL) /* Tap again: reset all */; }

static void reset_all(void)
{
    for (int i = 0; i < UI_SECTORS_MAX; i++) if (s_v[i]) { s_v[i] = 0; save_value(i); }
    for (int i = 0; i < s_n; i++) show_value(i);
    show_title();
    ui_haptic_strong();
    printf("APP counter reset n=%d\n", s_n);
}

static bool ring_step(int axis, int d)
{
    if (axis == C_COUNT) { s_n = COUNTS[pc_wrapi(count_index(s_n) + d, 3)]; save_setup(); build(); }
    else if (axis == C_NAME) { s_name = pc_wrapi(s_name + d, NAMES); save_setup(); show_title(); }
    else if (axis == C_STYLE) { s_style = pc_wrapi(s_style + d, game_style_count()); save_setup(); apply_style(); }
    else return false;
    printf("APP counter setup n=%d name=%d style=%d\n", s_n, s_name, s_style);
    return true;
}
static void ring_pick(int i)
{
    if (i == C_RESET) { ui_popring_close(); reset_all(); }
    else if (i < 0) ui_popring_close();
}
static void ring_open(void)
{
    s_pop = (ui_popring_t){ .n = C_N, .label = cell_label, .pick = ring_pick, .step = ring_step, .axis_mask = 1u << C_COUNT | 1u << C_NAME | 1u << C_STYLE,
                            .arm_mask = 1u << C_RESET, .armed = armed_text, .help = &HELP_COUNTER };
    ui_popring_open(s_page, &s_pop, 0);
}
static void head_tap(lv_event_t *e) { (void)e; ring_open(); }

/* ---- the page ---- */
static bool on_knob(int d) { return ui_axis_knob(d) || (s_sec && ui_sectors_knob(s_sec, d)); }   /* the borrowed knob first; the keys' cursor on a device without a knob */

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_page = ui_obj_guard(p, &s_page);
    s_sec = NULL;
    s_n = COUNTS[count_index((int)gamesave_get_or("ctr.n", 4))];
    s_name = pc_wrapi((int)gamesave_get_or("ctr.nm", NAME_SCORE), NAMES);
    s_style = pc_wrapi((int)gamesave_get_or("ctr.st", 0), game_style_count());
    s_sel = 0;
    for (int i = 0; i < UI_SECTORS_MAX; i++) { char k[12]; snprintf(k, sizeof k, "ctr.v%d", i); uint32_t v = gamesave_get_or(k, 0); s_v[i] = v > CTR_MAX ? CTR_MAX : (int32_t)v; }
    s_title = NULL;                                      /* the round glass keeps its title line; the square panel's frame (ui_frame.h) has the status in the content */
    if (!ui_frame_active(p)) s_title = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_title);
    ui_frame_option(ui_choice_row(p, UI_SLOT_HEAD, 1, head_tap, &s_cell));
    ui_obj_guard(s_cell, &s_cell);
    if (s_cell) ui_choice_set(s_cell, T(S_MENU) /* Menu */, UI_LOOK_NORMAL);
    build();
    ui_back_button(p);
}

static void destroy(void) { gamesave_flush(); s_sec = NULL; s_cell = NULL; s_title = NULL; s_page = NULL; }

const ui_screen_t scr_counter = { .name = "tools_counter", .create = create, .destroy = destroy, .on_knob = on_knob, .content = true };

void counter_cmd(const char *a)
{
    int v, nm, st;                                       /* parsed into locals: a partial sscanf match must not touch the page's state */
    if (ui_top() != &scr_counter) { printf("APP counter none\n"); return; }
    if (sscanf(a, "tap %d", &v) == 1) { if (v >= 0 && v < s_n) ui_sectors_tap(s_sec, v); }
    else if (sscanf(a, "sel %d", &v) == 1) { if (v >= 0 && v < s_n) { s_sel = v; show_looks(); show_title(); } }
    else if (!strcmp(a, "ring")) ring_open();
    else if (!strcmp(a, "reset")) reset_all();
    else if (sscanf(a, "set %d %d %d", &v, &nm, &st) == 3) {     /* count, name preset, style: as the ring would set them (pictures, tests: no dependence on what was saved) */
        s_n = COUNTS[count_index(v)];
        s_name = pc_wrapi(nm, NAMES);
        s_style = pc_wrapi(st, game_style_count());
        save_setup();
        build();
    }
    else if (a[0]) { printf("APP counter unknown %s\n", a); return; }
    char vals[UI_SECTORS_MAX * 6 + 1];
    int n = 0;
    for (int i = 0; i < s_n; i++) n += snprintf(vals + n, sizeof vals - (size_t)n, i ? ",%d" : "%d", (int)s_v[i]);
    printf("APP counter n=%d sel=%d name=%d style=%d v=%s\n", s_n, s_sel, s_name, s_style, vals);
}
