// SPDX-License-Identifier: LGPL-2.1-or-later
/* Breathing coach (ideas G7, in the white-noise app: a row of its scene list): a circle in the shared field (ui_arena) that grows on the inhale, stays on the
 * hold and shrinks on the exhale, a word and a countdown above it, a faint ring on the rim that fills with the session. The timing is pc_breath (host-tested);
 * this file maps it onto the field's slots, plays the soft haptic tick of each phase and, when asked, lets the playing noise scene follow the breath through
 * noise_engine_set_breath (a gain of 0.5..1: never above the user's level; the engine lets go by itself 1 s after the last call). The HEAD row: left = the
 * session length (3 / 5 / 10 minutes), right = the pattern (4-7-8, box, coherent 5-5); the ring: 節奏, 外觀 (game_style), noise follow (off / light / deep, the borrowed-knob axis),
 * 重來, 說明 last. The arena's `toy` flag: the field never pins the knob (a tap borrows it 4 s: a turn then steps the noise follow), a cover (screen off, a ring) only
 * stops the timer (the session waits, the noise follow lets go) and ui_arena_kick restarts it. A tap on a finished session starts another. The exit is the shared one
 * (mode A: a knob turn that falls to the volume shows 返回). Only composes shared pieces; state in PSRAM (.bss: this file is built with -msmall-data-limit=0). */
#include <stdio.h>
#include <string.h>
#include "font.h"
#include "game_ring.h"
#include "game_style.h"
#include "noise_engine.h"
#include "pc/pc_breath.h"
#include "screens.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_axis.h"
#include "ui_exit.h"
#include "ui_kit.h"

static lv_obj_t *s_arena, *s_title;
static pc_br_t s_g;
static game_sel_t s_sel;                               /* mode: the session length (0..2); rule: the pattern; style: the look. Kept while the device is on */
static int s_follow;                                   /* 0 off, 1 soft, 2 deep */
static game_field_palette_t s_pal;
static char s_text[48];
static const int DEPTH[3] = { 0, 250, PC_BR_DEPTH_MAX };

static int depth(void) { return DEPTH[s_follow]; }
static const char *phase_word(int ph)
{
    return ph == PC_BR_IN ? T(S_INHALE) /* Inhale */ : ph == PC_BR_HOLD ? T(S_HOLD) /* Hold */ : ph == PC_BR_OUT ? T(S_EXHALE) /* Exhale */ : T(S_REST) /* Pause */;
}
static lv_color_t phase_color(int ph) { return ph == PC_BR_IN ? s_pal.fx : ph == PC_BR_HOLD ? s_pal.power : s_pal.piece; }

static void title_set(void)
{
    char t[48];
    const int left = pc_br_left_s(&s_g);
    if (s_g.done) snprintf(t, sizeof t, "%s", T(S_DONE_TAP_TO_REPEAT) /* Done  tap to go again */);
    else snprintf(t, sizeof t, "%s %d  %d:%02d", phase_word(s_g.phase), pc_br_phase_left_s(&s_g), left / 60, left % 60);
    if (!strcmp(t, s_text)) return;
    snprintf(s_text, sizeof s_text, "%s", t);
    lv_label_set_text(s_title, t);
}
static void sync(void)
{
    const int R = ui_arena_radius(s_arena), rmin = R * 30 / 100, rmax = R * 85 / 100;
    const int r = rmin + (rmax - rmin) * pc_br_level(&s_g) / 1000;
    ui_arena_begin(s_arena);
    ui_arena_ball(s_arena, 0, 0, 0, r, phase_color(s_g.phase), 210);
    const int deg = (int)(359L * s_g.elapsed / (s_g.total ? s_g.total : 1));   /* the ring fills from 12 o'clock, almost round at the end (the arena takes at most a full turn) */
    if (deg >= 2) ui_arena_arc(s_arena, 1, R - 7, R - 3, 270, 270 + deg, true, s_pal.piece, 200);
    else ui_arena_hide(s_arena, 1);
    ui_arena_end(s_arena);
    title_set();
}
static void follow_noise(void)
{
    if (s_follow && !s_g.done && noise_current() >= 0) noise_engine_set_breath(pc_br_noise_pm(pc_br_level(&s_g), depth()));
}

static void step(void *u)
{
    (void)u;
    const int ev = pc_br_step(&s_g);
    if (ev & PC_BR_EV_PHASE) ui_haptic_tick();
    if (ev & PC_BR_EV_DONE) {
        ui_haptic_strong();
        ui_arena_wave(s_arena, s_pal.fx);
        printf("APP breath done cycles=%d\n", s_g.cycles);
    }
    follow_noise();
    sync();
}
static bool running(void *u) { (void)u; return !s_g.done; }
static void apply_style(void)
{
    game_style_field(game_style(s_sel.style), &s_pal);
    ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track);
    sync();
}
static void new_session(void)
{
    pc_br_init(&s_g, s_sel.rule, s_sel.mode);
    s_text[0] = 0;
    ui_arena_unpause(s_arena);
    ui_arena_clear(s_arena);
    apply_style();
    ui_arena_wake(s_arena);
    printf("APP breath new pattern=%s total=%d follow=%d\n", pc_br_pattern_id(s_g.pattern), s_g.total / PC_BR_FPS, s_follow);
}
static void set_follow(int v)
{
    s_follow = LV_CLAMP(0, v, 2);
    if (!s_follow) noise_engine_set_breath(1000);
    char t[12];
    snprintf(t, sizeof t, "%s %s", T(S_NOISE) /* Noise */, s_follow == 0 ? T(S_OFF) /* Off */ : s_follow == 1 ? T(S_SOFT) /* Light */ : T(S_DEEP) /* Deep */);
    ui_arena_text(s_arena, 0, 0, t, s_pal.fx);
}
static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u; (void)x; (void)y;
    if (kind == UI_ARENA_STEP) { if (v) set_follow(s_follow + (v > 0 ? 1 : -1)); return; }   /* the borrowed knob (a tap before), or the FakePod's keys */
    if (s_g.done) new_session();
}
static void dump(void *u)
{
    (void)u;
    printf("APP breath pattern=%s phase=%d phase_left=%d left=%d level=%d cycles=%d done=%d follow=%d gain=%d noise=%d\n", pc_br_pattern_id(s_g.pattern), s_g.phase,
           pc_br_phase_left_s(&s_g), pc_br_left_s(&s_g), pc_br_level(&s_g), s_g.cycles, s_g.done, s_follow, noise_engine_breath(), noise_current());
}
/* console `arena game <arg>`: "follow <0..2>" sets the noise follow, "end" jumps to the last second of the session */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int a;
    if (sscanf(arg, "follow %d", &a) == 1) { s_follow = LV_CLAMP(0, a, 2); if (!s_follow) noise_engine_set_breath(1000); }
    else if (!strcmp(arg, "end")) { s_g.elapsed = s_g.total - PC_BR_FPS; sync(); }
    else printf("APP breath cmd unknown %s\n", arg);
}
static const ui_arena_def_t ARENA = { .name = "breath", .objs = 2, .step = step, .running = running, .input = input, .dump = dump, .cmd = cmd, .toy = true, .full = true };

static const char *mode_text(int v) { return v == 0 ? T(S_3_MIN) /* 3 min */ : v == 1 ? T(S_5_MIN) /* 5 min */ : T(S_10_MIN) /* 10 min */; }
static const char *pattern_name(int v) { return v == PC_BR_478 ? "4-7-8" : v == PC_BR_BOX ? T(S_BOX) /* Box */ : "5-5"; }
static const char *follow_name(int k) { (void)k; return T(S_NOISE) /* Noise */; }
static const char *follow_value(int k) { (void)k; return s_follow == 0 ? T(S_OFF_2) /* Off */ : s_follow == 1 ? T(S_SOFT_2) /* Light */ : T(S_DEEP_2) /* Deep */; }
static bool follow_step(int k, int d)
{
    (void)k;
    s_follow = ui_axis_step(s_follow, 0, 2, 1, d, ui_axis_from_tap());
    if (!s_follow) noise_engine_set_breath(1000);
    return true;
}
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else new_session();                                  /* a length, a pattern, 重來: the session starts over */
}
static const ui_exit_def_t EXIT_DEF = { .mode = UI_EXIT_C };   /* CF2: taps are used: the popup waits for a free knob */
static const game_ring_t RING = { .id = "breath", .modes = 3, .mode_text = mode_text, .rules = PC_BR_PATTERNS, .rule_name = pattern_name, .changed = changed,
                                  .rule_zh = "節奏", .rule_en = "Pattern", .style_zh = "外觀", .style_en = "Look", .help = &HELP_BREATH,
                                  .axes = 1, .axis_name = follow_name, .axis_value = follow_value, .axis_step = follow_step };

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_arena_create(p, &ARENA, NULL), &s_arena);
    s_title = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_title);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT_DEF, NULL);
    new_session();
}
static void destroy(void) { noise_engine_set_breath(1000); }
static void resume(void) { ui_arena_kick(); }   /* the page over it was popped (help) */
const ui_screen_t scr_breath = { .name = "noise_breath", .create = create, .destroy = destroy, .on_resume = resume, .on_knob = ui_arena_knob, .content = true };
