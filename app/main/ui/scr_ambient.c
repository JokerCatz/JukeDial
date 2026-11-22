// SPDX-License-Identifier: LGPL-2.1-or-later
/* Ambient scenes (ideas G5): lava lamp / fish tank / campfire / night light on the shared field (ui_arena) with the game ring (game_ring): the HEAD row's left cell is the speed, the
 * right cell the scene (the ring's 場景 cell), the ring has 場景, 色調 (the three tones of ambient_tone), brightness (the extra axis: five night-friendly steps), 重來 and 說明 last.
 * A tap pokes the scene (a blob splits, food drops or a fish darts, the fire flares, the light pulses); the tap borrows the knob for 4 s and a turn then steps the
 * brightness, both ways (the arena's `toy` flag: the borrow is never kept alive, the knob is the volume otherwise). The music's level (ui_arena_energy, read-only, only
 * while the screen is on: the arena stops its timer when the screen is off or the page is covered) makes the scenes livelier. The motion is pc_ambient (host-tested), this
 * file only maps its primitives onto the arena's slots and plays the events (a burst, a rare sound, haptics). The exit is the shared one (mode A: a knob turn that falls to
 * the volume shows 返回). Only composes shared pieces (ui_arena, game_ring, ui_exit, ambient_tone); state in PSRAM (.bss: this file is built with -msmall-data-limit=0). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ambient_tone.h"
#include "font.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_ambient.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_exit.h"
#include "ui_frame.h"
#include "ui_kit.h"

static lv_obj_t *s_arena;
static pc_am_t s_g;
static game_sel_t s_sel;                               /* mode: the speed (0 normal, 1 fast, 2 slow); rule: the scene; style: the tone. Kept while the device is on */
static int s_bd;                                       /* the brightness step as an offset from the default (so the zero-initialised static is the default) */
static ambient_pal_t s_pal;
static int s_prev_n;                                   /* primitives set by the last sync: the slots above it are hidden */
static uint32_t s_opens;                               /* scenes started (the seed's count) */
static char s_blabel[24];
static const char *const SCENE_ZH[PC_AM_SCENES] = { "熔岩燈", "魚缸", "篝火", "夜燈" }, *const SCENE_EN[PC_AM_SCENES] = { "Lava lamp", "Aquarium", "Campfire", "Night light" };
static const float SPEED_OF[3] = { 1.0f, 1.6f, 0.6f };

static int level(void) { return LV_CLAMP(0, ambient_bright_default() + s_bd, AMBIENT_BRIGHT_N - 1); }
static float scale(void) { return (float)ui_arena_radius(s_arena) / PC_AM_FIELD; }
static int px(float u) { return (int)lroundf(u * scale()); }
static lv_color_t gold(void) { return s_pal.tone[s_sel.rule == PC_AM_FISH ? 4 : 3]; }

/* ---- the primitives onto the arena's slots: only what changed is redrawn (ui_arena compares) ---- */
static void sync(void)
{
    pc_am_prim_t p[PC_AM_MAXPRIM];
    const int n = pc_am_draw(&s_g, p, PC_AM_MAXPRIM);
    ui_arena_begin(s_arena);
    for (int i = 0; i < n; i++) {
        lv_color_t c = s_pal.tone[p[i].tone % PC_AM_TONES];
        if (p[i].mix && p[i].tone + 1 < PC_AM_TONES) c = lv_color_mix(s_pal.tone[p[i].tone + 1], c, p[i].mix);
        if (p[i].shape == PC_AM_RECT) ui_arena_rect(s_arena, i, px(p[i].x), px(p[i].y), LV_MAX(2, px(p[i].a)), LV_MAX(2, px(p[i].b)), px(p[i].b) / 2, c, p[i].opa);
        else ui_arena_ball(s_arena, i, px(p[i].x), px(p[i].y), LV_MAX(1, px(p[i].a)), c, p[i].opa);
    }
    for (int i = n; i < s_prev_n; i++) ui_arena_hide(s_arena, i);
    s_prev_n = n;
    ui_arena_end(s_arena);
}

/* the scene's events: an effect in the field, graded haptics; the one sound is the rare catch */
static void events(void)
{
    for (int i = 0; i < s_g.n_ev; i++) {
        const pc_am_ev_t *v = &s_g.ev[i];
        const int x = px(v->x), y = px(v->y);
        switch (v->kind) {
        case PC_AM_EV_SPLIT: ui_arena_burst(s_arena, x, y, s_pal.tone[3], 4); ui_haptic_tick(); break;
        case PC_AM_EV_FEED: ui_haptic_tick(); break;
        case PC_AM_EV_EAT: ui_arena_burst(s_arena, x, y, s_pal.tone[3], 3); break;
        case PC_AM_EV_DART: ui_haptic_tick(); break;
        case PC_AM_EV_GOLD_SEEN: ui_arena_burst(s_arena, x, y, gold(), 3); break;
        case PC_AM_EV_GOLD_CAUGHT:
            ui_arena_burst(s_arena, x, y, gold(), 8);
            ui_arena_text(s_arena, x, y - px(10), T(S_GOLD) /* Gold */, gold());
            sfx_play("gold");
            ui_haptic_strong();
            break;
        case PC_AM_EV_FLARE: ui_haptic_tap(); break;
        case PC_AM_EV_SPARKS: ui_arena_burst(s_arena, x, y, s_pal.tone[4], 8); ui_haptic_tick(); break;
        case PC_AM_EV_PULSE: ui_arena_wave(s_arena, s_pal.tone[2]); ui_haptic_tap(); break;
        case PC_AM_EV_BLINK: ui_haptic_tick(); break;
        default: break;
        }
    }
    s_g.n_ev = 0;
}

/* ---- the field's callbacks ---- */
static void step(void *u)
{
    (void)u;
    pc_am_step(&s_g, (float)ui_arena_energy(s_arena) / 255.0f);
    events();
    sync();
}
static bool running(void *u) { (void)u; return true; }
static void paused(bool on, void *u) { (void)u; printf("APP ambient paused=%d\n", on); }
static void apply_palette(void)
{
    ambient_palette(s_sel.rule, s_sel.style, level(), &s_pal);
    ui_arena_field(s_arena, s_pal.bg, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.rim);
    sync();
}
static void set_bright(int d)
{
    const int v = LV_CLAMP(0, level() + d, AMBIENT_BRIGHT_N - 1);
    s_bd = v - ambient_bright_default();
    apply_palette();
    char t[12];
    snprintf(t, sizeof t, "%s %d%%", T(S_BRIGHT) /* Brightness */, ambient_bright_pct(v));
    ui_arena_text(s_arena, 0, 0, t, s_pal.tone[3 % PC_AM_TONES]);
}
static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u;
    if (kind == UI_ARENA_STEP) { if (v) set_bright(v > 0 ? 1 : -1); return; }      /* the borrowed knob (a tap before), or the FakePod's keys: the brightness */
    pc_am_poke(&s_g, (float)x / scale(), (float)y / scale());
    events();
    sync();
}
static void dump(void *u)
{
    (void)u;
    int golds = pc_am_count(&s_g, PC_AM_K_GOLDFISH);
    for (int i = 0; i < PC_AM_ITEMS; i++) golds += s_g.it[i].kind == PC_AM_K_BLOB && s_g.it[i].tone == 3;
    printf("APP ambient scene=%d speed=%d style=%d bright=%d prims=%d blobs=%d fish=%d food=%d bubbles=%d gold=%d flames=%d embers=%d firefly=%d phase=%d\n",
           s_g.scene, (int)lroundf(s_g.speed * 100), s_sel.style, ambient_bright_pct(level()), s_prev_n, pc_am_count(&s_g, PC_AM_K_BLOB), pc_am_count(&s_g, PC_AM_K_FISH),
           pc_am_count(&s_g, PC_AM_K_FOOD), pc_am_count(&s_g, PC_AM_K_BUBBLE), golds,
           pc_am_count(&s_g, PC_AM_K_FLAME), pc_am_count(&s_g, PC_AM_K_EMBER), pc_am_count(&s_g, PC_AM_K_FIREFLY), (int)lroundf(s_g.phase * 100));
}
static void new_scene(uint32_t seed);
/* console `arena game ...` (tests and pictures): "seed <n>" starts the scene again with that seed, "rare" its rare event now, "poke <x> <y>" a tap at a field point in units
 * (100 = the rim), "bright <level 0..4>", "energy <0..255>" is the arena's own `arena` dump; the speed and the scene go through `games mode|rule <n>` */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int a, b;
    if (sscanf(arg, "seed %d", &a) == 1) new_scene((uint32_t)a);
    else if (!strcmp(arg, "rare")) { pc_am_force_rare(&s_g); events(); sync(); }
    else if (sscanf(arg, "poke %d %d", &a, &b) == 2) { pc_am_poke(&s_g, (float)a, (float)b); events(); sync(); }
    else if (sscanf(arg, "bright %d", &a) == 1) { s_bd = LV_CLAMP(0, a, AMBIENT_BRIGHT_N - 1) - ambient_bright_default(); apply_palette(); }
    else printf("APP ambient cmd unknown %s\n", arg);
}
static const ui_arena_def_t ARENA = { .name = "ambient", .objs = PC_AM_MAXPRIM, .step = step, .running = running, .input = input, .paused = paused, .dump = dump, .cmd = cmd, .toy = true, .full_sq = true };

static void new_scene(uint32_t seed)
{
    pc_am_init(&s_g, s_sel.rule, seed, SPEED_OF[s_sel.mode % 3]);
    ui_arena_unpause(s_arena);
    ui_arena_clear(s_arena);
    s_prev_n = 0;
    apply_palette();
    ui_arena_wake(s_arena);
    printf("APP ambient new scene=%d style=%d mode=%d\n", s_sel.rule, s_sel.style, s_sel.mode);
}

static const char *mode_text(int v) { return v == 0 ? T(S_NORMAL) /* Medium speed */ : v == 1 ? T(S_FAST) /* Fast */ : T(S_SLOW) /* Slow */; }
static const char *scene_name(int v) { return T_RAW(SCENE_ZH[v], SCENE_EN[v]); }
static const char *bright_name(int k) { (void)k; return T(S_BRIGHT) /* Brightness */; }
static const char *bright_value(int k)
{
    (void)k;
    snprintf(s_blabel, sizeof s_blabel, "%d%%", ambient_bright_pct(level()));
    return s_blabel;
}
static bool bright_step(int k, int d) { (void)k; if (d) set_bright(d > 0 ? 1 : -1); return true; }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_MODE) s_g.speed = SPEED_OF[s_sel.mode % 3];   /* the speed changes at once, the scene goes on */
    else if (what == GR_STYLE) apply_palette();
    else new_scene(games_seed("ambient", s_opens++));            /* a scene, 重來: a new scene */
}
static const ui_exit_def_t EXIT_DEF = { .mode = UI_EXIT_C };   /* CF2: taps poke / drop / restart: the popup waits for a free knob (a turn) */
static const game_ring_t RING = { .id = "ambient", .modes = 3, .mode_text = mode_text, .rules = PC_AM_SCENES, .rule_name = scene_name, .changed = changed,
                                  .rule_zh = "場景", .rule_en = "Scene", .style_zh = "色調", .style_en = "Tone", .help = &HELP_AMBIENT,
                                  .axes = 1, .axis_name = bright_name, .axis_value = bright_value, .axis_step = bright_step };

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_frame_enable(p);                                  /* the round glass: a pilot page of the content-first frame (ui_frame.h; the square panel has it already) */
    ui_obj_guard(ui_arena_create(p, &ARENA, NULL), &s_arena);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT_DEF, NULL);
    ui_arena_energy_on(s_arena, true);
    new_scene(games_seed("ambient", s_opens++));
}

static void resume(void) { ui_arena_kick(); }   /* the page over it was popped (help) */
const ui_screen_t scr_ambient = { .name = "tools_ambient", .create = create, .on_resume = resume, .on_knob = ui_arena_knob, .content = true };
