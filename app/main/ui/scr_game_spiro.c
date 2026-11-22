// SPDX-License-Identifier: LGPL-2.1-or-later
/* Spirograph (ideas G5): a wheel rolls inside or outside a ring and its pen draws the curve in real time on the shared field's canvas
 * (ui_arena canvas mode: one transparent PSRAM picture, lines queued and drawn in one pass a step). The HEAD row (the speed; the rule, which opens the game's ring:
 * 規則 Classic / Layered / Music, 風格, gear ratio, pen offset, colour, 重來, 說明), the status line, and the shared exit (the popup (CF2): mode C, taps and the knob are both used).
 * Curve and pen: pc_game_spiro (host-tested: closes at integer ratios, stays in the circle). A tap on the field = a new random curve (the page's own seed stream);
 * the knob (a tap lends it to the field, the borrowed-knob way; the FakePod's keys) = the gear ratio, a new curve at once. Speed is the HEAD's left cell (the
 * knob the borrowed way), gear ratio / pen offset / colour axes of the game's ring (game_ring axes: a tap steps, then the knob). A new gear / pen / rule starts the curve over;
 * a new colour or style redraws what is drawn so far in the new colours. In-field feedback (ui_arena effects): a finished curve = a ring runs out + its lobe count
 * rising, a rare golden curve (1 in PC_SP_GOLD_N: the gold style, a symmetric star of 8+ lobes) = a burst and the coin chime. The music rule pens faster with the
 * music's energy (the arena's feed, only while it draws, only with the screen on). The screen off / a ring open / another page on top pauses the pen (the arena's
 * rule); a finished curve costs nothing (the timer rests). Only composes shared pieces (ui_arena, game_ring, game_style, ui_exit); state in PSRAM. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_spiro.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_theme.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"
#include "pc/pc_dial.h"

const ui_game_help_t game_help_spiro = {
    .zh = "一個小齒輪在大圓環裡面（或外面）滾動，齒輪上的筆畫出花紋；曲線繞整數圈就會剛好接起來，畫完就停。"
          "點一下場地：隨機換一個新花紋；點完之後轉動旋鈕（停下四秒回到音量）可以一格一格換齒輪比。"
          "上方右邊的格子打開選單：規則有三種——經典：畫一條曲線；層疊：同一組齒輪畫三層，筆距一層比一層小；音樂：筆跟著正在播的音樂的力道快慢，安靜時走得很慢。"
          "選單裡還有風格（換配色）、齒輪比（八組）、筆距（四種）、顏色（單色、雙色漸層、彩虹），這三個都可以點一下再轉旋鈕調整；換齒輪比、筆距或規則會從頭畫，換顏色或風格會用新顏色重畫已經畫好的部分。"
          "左邊的格子換畫的速度。螢幕關掉或打開選單時筆會暫停，點一下繼續。偶爾會出現金色的對稱星形",
    .en = "A small gear rolls inside (or outside) a big ring and its pen draws the pattern; with whole numbers of turns the curve closes exactly and the drawing stops. "
          "Tap the field for a new random pattern; after a tap, turning the knob (four seconds after you stop it is the volume again) steps the gear ratio. "
          "The right cell at the top opens the menu. Rules: Classic: one curve; Layers: three layers with the same gears, the pen closer to the centre each time; "
          "Music: the pen speeds up with the energy of the music playing and creeps when it is quiet. The menu also has Style (colours), Gear ratio (eight), "
          "Pen distance (four) and Colour (solid, two-tone, rainbow); each of those three takes the knob after a tap. A new gear, pen distance or rule starts over; "
          "a new colour or style redraws what is drawn so far in the new colours. The left cell changes the drawing speed. With the screen off or the menu open the "
          "pen pauses; tap to go on. Now and then a golden symmetric star appears",
};

static lv_obj_t *s_arena, *s_status;
static pc_sp_t s_g;
static game_sel_t s_sel;                               /* mode: the speed (0 normal, 1 fast, 2 slow); rule; style. Kept while the device is on */
static int s_colour;                                   /* the colour axis, kept too */
static game_style_t s_gold_style;
static game_field_palette_t s_pal;
static bool s_gold, s_paused, s_canvas, s_redraw;      /* s_redraw: a colour / style change asked for a redraw, done once by the next step */
static uint32_t s_games, s_rounds, s_lines;           /* curves started (the seed) / golden rolls / canvas lines drawn */
static char s_stat[72];
static const char *const RULE_ZH[PC_SP_RULES] = { "經典", "層疊", "音樂" }, *const RULE_EN[PC_SP_RULES] = { "Classic", "Layers", "Music" };
static const char *const COLOUR_ZH[PC_SP_COLOURS] = { "單色", "雙色", "彩虹" }, *const COLOUR_EN[PC_SP_COLOURS] = { "Solid", "Two-tone", "Rainbow" };
static const int SPEED_OF[PC_SP_SPEEDS] = { 1, 2, 0 };

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}
static int span(void) { return LV_MAX(8, ui_arena_radius(s_arena) - 3); }          /* the curve's radius in px: the line's width and the rim stay inside */
static int width(void) { return LV_MAX(2, ui_arena_radius(s_arena) / 44); }
static int px(float u) { return (int)lroundf(u * (float)span()); }

/* ---- drawing: a segment's colour by the colour axis (the golden game is always two-tone: gold into the style's other colour) ---- */
static int cmode(void) { return s_gold ? 1 : s_colour; }
static lv_color_t seg_colour(int layer, float t)
{
    const lv_color_t from[PC_SP_LAYERS_N] = { s_pal.piece, s_pal.tough, s_pal.fx }, to[PC_SP_LAYERS_N] = { s_pal.boom, s_pal.power, s_pal.boom };
    const int l = LV_CLAMP(0, layer, PC_SP_LAYERS_N - 1);
    switch (cmode()) {
    case 1: return lv_color_mix(to[l], from[l], (uint8_t)LV_CLAMP(0, (int)(t * 255.0f), 255));
    case 2: return ui_hue_on(game_style_hue((int)(t * 359.0f) + 120 * l), s_pal.fill);
    default: return from[l];
    }
}
static void line(float x0, float y0, float x1, float y1, int layer, float t)
{
    ui_arena_canvas_line(s_arena, px(x0), px(y0), px(x1), px(y1), width(), seg_colour(layer, t), LV_OPA_COVER);
    s_lines++;
}
static void pen(float x, float y)
{
    if (s_g.done) ui_arena_hide(s_arena, 0);
    else ui_arena_ball(s_arena, 0, px(x), px(y), LV_MAX(3, ui_arena_radius(s_arena) / 28), s_pal.ball, LV_OPA_COVER);
}
/* everything up to the pen's place again (a new colour or style, a screen that came back): the segments from the curve's own formula */
static void redraw(void)
{
    ui_arena_canvas_clear(s_arena);
    ui_arena_begin(s_arena);
    for (int l = 0; l <= s_g.layer && l < s_g.layers; l++) {
        const uint32_t upto = (l < s_g.layer || s_g.done) ? s_g.total : s_g.seg;
        float x0, y0, x1, y1;
        pc_sp_point(&s_g, l, 0, &x0, &y0);
        for (uint32_t k = 1; k <= upto; k++) {
            pc_sp_point(&s_g, l, (double)k, &x1, &y1);
            line(x0, y0, x1, y1, l, (float)k / (float)s_g.total);
            x0 = x1; y0 = y1;
        }
        if (l == s_g.layer && !s_g.done) pen(x0, y0);
    }
    if (s_g.done) pen(0, 0);
    ui_arena_end(s_arena);
}

static void want_redraw(void) { s_redraw = true; ui_arena_wake(s_arena); }
static int lobes_total(void) { return pc_sp_lobes(s_g.gear); }
static void show_status(void)
{
    char b[sizeof s_stat];
    int R, r;
    pc_sp_gear_of(s_g.gear, &R, &r);
    if (!s_canvas) snprintf(b, sizeof b, "%s", T(S_OUT_OF_MEMORY) /* Out of memory */);
    else if (s_paused) snprintf(b, sizeof b, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
    else if (s_g.done) snprintf(b, sizeof b, T(S_D_LOBES_TAP) /* %d petals · tap to change */, lobes_total());
    else snprintf(b, sizeof b, T(S_D_D_D_S_S), R, r, (int)lroundf(pc_sp_pen_of(s_g.pen) * 100.0f),
                  s_g.epi ? T(S_EPI) /* Epitrochoid */ : T(S_HYPO) /* Hypotrochoid */, s_gold ? T(S_GOLD_2) /*  Gold */ : "");
    ui_frame_hint(s_status, s_canvas && (s_paused || s_g.done)); /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}
/* the music rule's feed only while the pen draws (the arena keeps its timer for it) */
static void feed(void) { ui_arena_energy_on(s_arena, s_g.rule == PC_SP_MUSIC && !s_g.done); }

static void finished(void)
{
    char t[12];
    snprintf(t, sizeof t, T(S_D_LOBES) /* %d petals */, lobes_total());
    ui_arena_wave(s_arena, s_pal.fx);
    ui_arena_text(s_arena, 0, -span() / 3, t, s_gold ? s_pal.gold : s_pal.fx);
    sfx_play(s_gold ? "gold" : "sparkle");
    if (s_gold) { ui_arena_burst(s_arena, 0, 0, s_pal.gold, 8); ui_haptic_strong(); } else ui_haptic_tap();
}
/* the field's step: the pen's segments of this step (queued, drawn in one pass at the end of the batch) */
static void step(void *u)
{
    (void)u;
    if (s_redraw) { s_redraw = false; redraw(); }          /* however many colour steps came since the last tick: one pass of the drawing */
    if (s_g.done) return;
    const int n = pc_sp_step(&s_g, ui_arena_energy(s_arena));
    for (int i = 0; i < n; i++) line(s_g.out[i].x0, s_g.out[i].y0, s_g.out[i].x1, s_g.out[i].y1, s_g.out[i].layer, s_g.out[i].t);
    if (n) pen(s_g.out[n - 1].x1, s_g.out[n - 1].y1);
    if (s_g.ev_done) { finished(); feed(); }
    show_status();
}
static bool running(void *u) { (void)u; return pc_sp_running(&s_g) || s_redraw; }
static void paused(bool on, void *u) { (void)u; s_paused = on; show_status(); printf("APP game spiro paused=%d\n", on); }

/* a new curve from the same wheels (a rule, the speed's neighbours): nothing drawn */
static void restart(void)
{
    pc_sp_restart(&s_g);
    s_redraw = false;
    game_style_field(style(), &s_pal);                      /* a golden curve ended by this restart: the palette is the plain style's again */
    ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track);
    s_paused = false;
    ui_arena_unpause(s_arena);
    ui_arena_canvas_clear(s_arena);
    ui_arena_hide(s_arena, 0);
    ui_arena_wake(s_arena);
    feed();
    show_status();
}
static void apply_style(void)
{
    game_style_field(style(), &s_pal);
    ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track);
    want_redraw();
    feed();
}
/* a new random curve: roll gold (only when `roll`: 重來, a tap), the wheels from the page's own stream */
static void new_curve(bool roll)
{
    if (roll) s_gold = game_result_gold("spiro", s_rounds++);
    pc_sp_init(&s_g, s_sel.rule, SPEED_OF[s_sel.mode], s_colour, games_seed("spiro", s_games++), s_gold);
    s_paused = false;
    ui_arena_unpause(s_arena);
    game_style_field(style(), &s_pal);
    ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track);
    ui_arena_canvas_clear(s_arena);
    ui_arena_hide(s_arena, 0);
    ui_arena_wake(s_arena);
    feed();
    show_status();
    if (roll && s_gold) { sfx_play("gold"); ui_haptic_strong(); ui_arena_text(s_arena, 0, 0, T(S_GOLD_3) /* Golden! */, s_pal.gold); }
    printf("APP game spiro new gear=%d pen=%d epi=%d rule=%d style=%d mode=%d colour=%d gold=%d\n", s_g.gear, s_g.pen, s_g.epi, s_sel.rule, s_sel.style, s_sel.mode, s_colour, s_gold);
}
static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u; (void)x; (void)y;
    if (kind == UI_ARENA_STEP) {                       /* the borrowed knob (a tap lent it) / the FakePod's keys: the gear ratio */
        s_gold = false;
        pc_sp_set(&s_g, s_g.gear + v, -1, -1);
        restart();
        ui_haptic_tick();
        return;
    }
    ui_haptic_tap();
    sfx_play("move");
    new_curve(true);
}

static void dump(void *u)
{
    (void)u;
    printf("APP game spiro gear=%d pen=%d epi=%d rule=%d speed=%d colour=%d layer=%d seg=%u/%u done=%d gold=%d paused=%d lobes=%d lines=%u canvas=%d\n",
           s_g.gear, s_g.pen, s_g.epi, s_g.rule, s_g.speed, s_colour, s_g.layer, (unsigned)s_g.seg, (unsigned)s_g.total, s_g.done, s_gold, s_paused, lobes_total(),
           (unsigned)s_lines, s_canvas);
}
/* console `arena game ...` (tests and pictures): "new <gear> <pen> <epi 0|1>" the wheels at once (a drawing from the start); "gold 1|0" the golden look for the next
 * drawing (and this one redrawn); "finish" the pen draws to the end now */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int g, p, e, n;
    if (sscanf(arg, "new %d %d %d", &g, &p, &e) == 3) { s_gold = false; pc_sp_set(&s_g, g, p, e); restart(); }
    else if (sscanf(arg, "gold %d", &n) == 1) { s_gold = n != 0; apply_style(); show_status(); }
    else if (!strcmp(arg, "finish")) {
        ui_arena_begin(s_arena);
        while (pc_sp_running(&s_g)) step(NULL);
        ui_arena_end(s_arena);
    }
    else { printf("APP game spiro cmd unknown %s\n", arg); return; }
}
static const ui_arena_def_t ARENA = { .name = "spiro", .objs = 1, .step = step, .running = running, .paused = paused, .input = input, .dump = dump, .cmd = cmd, .toy = true };   /* a toy: the gear-ratio borrow is not kept alive by the steps (4 s, then the volume) */

/* ---- the ring ---- */
static const char *mode_text(int v) { return v == 0 ? T(S_NORMAL) /* Medium speed */ : v == 1 ? T(S_FAST) /* Fast */ : T(S_SLOW) /* Slow */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static const char *axis_name(int k) { return k == 0 ? T(S_GEAR) /* Gear ratio */ : k == 1 ? T(S_PEN) /* Pen offset */ : T(S_COLOUR) /* Colour */; }
static const char *axis_value(int k)
{
    static char b[16];
    int R, r;
    if (k == 0) { pc_sp_gear_of(s_g.gear, &R, &r); snprintf(b, sizeof b, "%d:%d", R, r); }
    else if (k == 1) snprintf(b, sizeof b, "%d%%", (int)lroundf(pc_sp_pen_of(s_g.pen) * 100.0f));
    else snprintf(b, sizeof b, "%s", T_RAW(COLOUR_ZH[s_colour], COLOUR_EN[s_colour]));
    return b;
}
static bool axis_step(int k, int d)
{
    if (k == 0) { s_gold = false; pc_sp_set(&s_g, s_g.gear + d, -1, -1); restart(); }
    else if (k == 1) { s_gold = false; pc_sp_set(&s_g, -1, s_g.pen + d, -1); restart(); }
    else if (k == 2) { s_colour = pc_wrapi(s_colour + d, PC_SP_COLOURS); s_g.colour = (uint8_t)s_colour; want_redraw(); }
    else return false;
    return true;
}
static void changed(int what, int v)
{
    if (what == GR_STYLE) apply_style();
    else if (what == GR_MODE) s_g.speed = (uint8_t)SPEED_OF[v];                              /* the speed only: the curve goes on */
    else if (what == GR_RULE) { s_g.rule = (uint8_t)v; restart(); }                          /* the same wheels, drawn again by the new rule */
    else new_curve(true);                                                                   /* Restart */
}
static bool ring_cmd(const char *arg)                  /* `games gear|pen|colour <n>`: the axes as the ring sets them (absolute); `games count <n>`: the seed and golden-roll counters */
{
    int n;
    if (sscanf(arg, "gear %d", &n) == 1) axis_step(0, n - s_g.gear);
    else if (sscanf(arg, "pen %d", &n) == 1) axis_step(1, n - s_g.pen);
    else if (sscanf(arg, "colour %d", &n) == 1) axis_step(2, n - s_colour);
    else if (sscanf(arg, "count %d", &n) == 1) { s_games = (uint32_t)n; s_rounds = (uint32_t)n; new_curve(true); }   /* the seed and golden-roll counters, then a new curve: the same curve whatever ran before (tests, the leak action) */
    else return false;
    dump(NULL);
    return true;
}
static const game_ring_t RING = { .id = "spiro", .modes = PC_SP_SPEEDS, .mode_text = mode_text, .rules = PC_SP_RULES, .rule_name = rule_name, .changed = changed, .cmd = ring_cmd,
                                  .axes = 3, .axis_name = axis_name, .axis_value = axis_value, .axis_step = axis_step };

static void exit_pause(void *u) { (void)u; ui_arena_pause(s_arena); }
static void exit_resume(void *u, uint32_t ms) { (void)u; (void)ms; ui_arena_resume(s_arena); ui_arena_kick(); }
static void resume(void) { ui_arena_kick(); }
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .pause = exit_pause, .resume = exit_resume };   /* taps start a new curve and the knob steps the gear  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_stat[0] = 0;
    s_lines = 0;
    s_sel.mode = LV_CLAMP(0, s_sel.mode, PC_SP_SPEEDS - 1);
    ui_obj_guard(ui_arena_create(p, &ARENA, NULL), &s_arena);
    s_canvas = s_arena && ui_arena_canvas(s_arena) != NULL;
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    s_gold = false;
    new_curve(false);
}

const ui_screen_t scr_game_spiro = { .name = "tools_game_spiro", .create = create, .on_resume = resume, .on_knob = ui_arena_knob, .content = true };
