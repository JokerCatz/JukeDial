// SPDX-License-Identifier: LGPL-2.1-or-later
/* Game of Life (ideas G6, game id `life`): Conway on a fine grid over the whole box (round build: its inscribed circle, square build: the full square), on the shared field (ui_arena, `full`: the whole
 * design circle under the HEAD row and the exit). The grid, the rule step (bit-parallel over 4 bitplanes: alive / age level / afterglow trail) and the pattern library
 * are pc_game_life (host-tested). This page only maps it onto the arena's canvas: a changed cell (round build: the circle of the box's width, a little past the design circle up to the panel edge; square build: the whole square; nothing is drawn outside it) is written straight into the opaque RGB565 picture (no grid look: dead cells
 * are not drawn, live neighbours touch edge to edge and merge into blobs; young cells in the style's piece colour, a cell that lived long (level 7) in gold, a dying
 * cell's afterglow in the same colour at 1/2, 1/4, 1/8 ... of its strength), and the changed boxes (6 bands) are handed to the field, so a generation costs the area that moved.
 * HEAD left = the speed (medium / fast / slow: the generations a second wanted); the real rate is capped by the cell size (pc_life_cap_gens) and by how long a generation
 * took (honest degrade: the status line shows the real generations a second and says "cap" when it is held back). HEAD right = the ring: 規則 Classic / Beat / High life / Day and night,
 * 風格, cell size 1 / 2 / 3 / 4 / 6 px, afterglow off / 1 / 3 / 5, pattern (random 15 / 25 / 35 % and eight patterns), edge wall / wrap, reseed, clear (armed), 說明.
 * A tap = mischief (not a precision device): a random blob of live cells round the tap, or the chosen pattern dropped there; rarely (the page's golden roll) a glider gun.
 * The knob (a tap lends it, 4 s) = the pattern axis. The Beat rule drops a small random cluster on every onset of the playing music (ui_beat, stopped when covered or the
 * screen is off) so the field never dies. When the field dies out (not in Beat) the result card says the generations and the peak population; a field that has not changed
 * for 40 generations rests (the timer stops; a tap wakes it). Exit: the shared one, mode A (a toy: the knob is the volume). Only composes shared pieces; state in PSRAM. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_life.h"
#include "psram_alloc.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_beat.h"
#include "ui_exit.h"
#include "ui_frame.h"
#include "ui_kit.h"
#include "pc/pc_dial.h"

const ui_game_help_t game_help_life = {
    .zh = "康威生命遊戲：每個活細胞周圍有 2 或 3 個活鄰居就繼續活，死細胞周圍剛好 3 個活鄰居就誕生，其他都死。"
          "點一下場地是「搗亂」：在手指附近撒一把隨機的活細胞（選了圖案就把那個圖案丟在那裡），偶爾會冒出一座滑翔機槍；點完之後轉動旋鈕（停下四秒回到音量）可以換圖案。"
          "活得久的細胞會變成金色，剛死的細胞會留下漸暗的殘影。"
          "上方右邊的格子打開選單：規則——經典；節拍：跟著正在播的音樂，每個鼓點掉下一小撮細胞，場地永遠不會死光；高生命、日與夜是兩種變體。"
          "風格換配色；格子是每個細胞的像素（1 到 6，越小越細也越吃力）；殘影是死掉後留幾代（關、1、3、5）；圖案是隨機的密度或八種經典圖案；邊緣是牆（場地的邊界）或環繞（左右上下接起來）。這幾個都可以點一下再轉旋鈕調整。"
          "重新播種照圖案重新開始，清空要連點兩下。左邊的格子換速度；格子太小時速度會被限制，狀態列顯示實際的每秒代數。",
    .en = "Conway's Game of Life: a live cell with 2 or 3 live neighbours lives on, a dead cell with exactly 3 is born, everything else dies. "
          "Tap the field to stir it: a random handful of live cells lands round your finger (or the chosen pattern, if one is selected), and now and then a glider gun appears; "
          "after a tap, turn the knob (four seconds after you stop it is the volume again) to change the pattern. Cells that live long turn gold and a cell that just died leaves a fading afterglow. "
          "The right cell at the top opens the menu. Rules: Classic; Beat: follows the music playing, every drum hit drops a small cluster so the field never dies out; HighLife and Day and Night are variants. "
          "Style changes the colours; Cell is the pixels of a cell (1 to 6: smaller is finer and heavier); Glow is how many generations a dead cell fades (off, 1, 3, 5); "
          "Pattern is a random density or one of eight classic patterns; Edge is wall (the field's border) or wrap (the sides join). Each of these takes the knob after a tap. "
          "Reseed starts again by the pattern; Clear takes two taps. The left cell changes the speed; with small cells the speed is capped and the status line shows the real generations a second.",
};

#define CELLS       5
#define GLOWS       4
#define PATS        (3 + PC_LIFE_PATTERNS)         /* random 15 / 25 / 35 % then the library */
#define QUIET_GENS  40                             /* generations without a change before the field rests */
#define BANDS       6
static const int CELL_PX[CELLS] = { 1, 2, 3, 4, 6 }, GLOW_OF[GLOWS] = { 0, 1, 3, 5 }, DENS[3] = { 15, 25, 35 };
static const int WANT_GPS[3] = { 10, 30, 3 };      /* mode 0 medium, 1 fast, 2 slow: generations a second wanted */
static const char *const RULE_ZH[PC_LIFE_RULES] = { "經典", "節拍", "高生命", "日與夜" }, *const RULE_EN[PC_LIFE_RULES] = { "Classic", "Beat", "HighLife", "Day&Night" };
static const char *const PAT_ZH[PC_LIFE_PATTERNS] = { "滑翔機", "輕型太空船", "滑翔機槍", "脈衝星", "R-五格", "橡實", "長壽者", "兔子" };
static const char *const PAT_EN[PC_LIFE_PATTERNS] = { "Glider", "Spaceship", "Glider gun", "Pulsar", "R-pentomino", "Acorn", "Diehard", "Rabbits" };

static lv_obj_t *s_arena, *s_status;
static pc_life_t s_g;
static void *s_mem;
static uint16_t *s_px;                             /* the canvas's pixels (the arena's, RGB565, opaque) */
static int s_stride, s_box, s_off, s_cell;         /* canvas stride (px), the box's side, the grid's offset in it, the cell's px */
static game_sel_t s_sel;                           /* mode = the speed, rule, style: kept while the device is on */
static int s_size, s_glow, s_pat, s_edge;          /* the axes (kept too): cell size index, afterglow index, pattern, 0 wall / 1 wrap (zero-initialised: an initialised static would be .data, in internal RAM; the defaults are set once in create) */
static bool s_set;
static game_field_palette_t s_pal;
static uint16_t s_col[PC_LIFE_C_TRAIL0 + PC_LIFE_TRAIL_MAX];   /* RGB565 of a visual class, opaque: the trails and the empty cell are pre-blended into the field's fill */
static bool s_canvas, s_over, s_rest;
static int s_phase, s_every, s_extra;              /* steps since the last generation, steps a generation waits (the wanted rate), steps the measured cost adds */
static int s_quiet, s_gps;                         /* generations without a change; the MEASURED generations a second (0: not measured yet) */
static uint32_t s_win_t0, s_win_gens, s_last_step, s_ivl_sum, s_ivl_n;   /* the 1 s window of the measure; the time between two field steps, summed over 30 steps */
static bool s_fail_next;                           /* console `games oom 1`: the next create finds no memory (tests of the out-of-memory page) */
static uint32_t s_rounds, s_t0, s_onsets, s_ticks, s_seen_ticks, s_taps, s_last_paint_cells;
static char s_stat[72];

static const game_style_t *style(void) { return game_style(s_sel.style); }
static int cell(void) { return CELL_PX[s_size]; }
static int gps_cap(void) { return pc_life_cap_gens(cell()); }
static int gps_nominal(void) { return 30 / (s_every + s_extra); }
static int gps_now(void) { return s_gps > 0 ? s_gps : gps_nominal(); }          /* what the status line says: counted over the last second, not the plan */
static void win_reset(void) { s_win_t0 = lv_tick_get(); s_win_gens = 0; s_gps = 0; s_last_step = 0; s_ivl_sum = s_ivl_n = 0; }
static bool capped(void) { return WANT_GPS[s_sel.mode] > gps_nominal() || (s_gps > 0 && s_gps + 1 < gps_nominal()); }
static void set_rate(void)
{
    const int g = LV_MIN(WANT_GPS[s_sel.mode], gps_cap());
    s_every = LV_MAX(1, (30 + g - 1) / g);
    win_reset();
}
/* the afterglow over the fill the way the RGB565 layer blends an ARGB8888 pixel (LVGL's lv_color_24_16_mix), so the opaque value is the one the blend drew */
static uint16_t over(lv_color_t c, uint16_t bg, uint8_t mix)
{
    const uint32_t inv = 255u - mix;
    return (uint16_t)(((((c.red >> 3) * mix + ((bg >> 11) & 0x1Fu) * inv) << 3) & 0xF800u) + ((((c.green >> 2) * mix + ((bg >> 5) & 0x3Fu) * inv) >> 3) & 0x07E0u) + (((c.blue >> 3) * mix + (bg & 0x1Fu) * inv) >> 8));
}
static void colours(void)
{
    game_style_field(style(), &s_pal);
    const uint16_t fill = lv_color_to_u16(s_pal.fill), piece = lv_color_to_u16(s_pal.piece);
    s_col[PC_LIFE_C_EMPTY] = fill;
    s_col[PC_LIFE_C_YOUNG] = piece;
    s_col[PC_LIFE_C_GOLD] = lv_color_to_u16(s_pal.gold);
    for (int k = 0; k < PC_LIFE_TRAIL_MAX; k++) s_col[PC_LIFE_C_TRAIL0 + k] = over(s_pal.piece, fill, (uint8_t)(255 >> (k + 1)));   /* 1/2, 1/4, 1/8 ... of the strength over the fill: what the blend drew */
}

/* ---- the canvas: the changed cells written straight into the picture, their boxes (6 bands of rows) told to the field ---- */
static void put_cell(int x, int y, uint16_t c)
{
    uint16_t *d = s_px + (size_t)(s_off + y * s_cell) * (size_t)s_stride + (size_t)(s_off + x * s_cell);
    for (int r = 0; r < s_cell; r++, d += s_stride)
        for (int q = 0; q < s_cell; q++) d[q] = c;
}
static void paint_changed(void)
{
    if (!s_px || !s_mem) return;
    int x0[BANDS], x1[BANDS], y0[BANDS], y1[BANDS];
    for (int b = 0; b < BANDS; b++) { x0[b] = s_g.w; x1[b] = -1; y0[b] = s_g.h; y1[b] = -1; }
    uint64_t *chg = pc_life_chg(&s_g);
    uint32_t n = 0;
    for (int y = 0; y < s_g.h; y++) {
        const int b = y * BANDS / s_g.h;
        for (int i = 0; i < s_g.nw; i++) {
            uint64_t m = chg[(size_t)y * (size_t)s_g.nw + (size_t)i];
            while (m) {
                const int x = i * 64 + __builtin_ctzll(m);
                m &= m - 1;
                if (!pc_life_in_circle(&s_g, x, y)) continue;                       /* a wrapped cell outside the field (the round panel's corners) is not drawn */
                put_cell(x, y, s_col[pc_life_class(pc_life_value(&s_g, x, y))]);
                n++;
                if (x < x0[b]) x0[b] = x;
                if (x > x1[b]) x1[b] = x;
                if (y < y0[b]) y0[b] = y;
                if (y > y1[b]) y1[b] = y;
            }
        }
    }
    pc_life_chg_clear(&s_g);
    s_last_paint_cells = n;
    for (int b = 0; b < BANDS; b++)
        if (x1[b] >= 0) ui_arena_canvas_dirty(s_arena, s_off + x0[b] * s_cell, s_off + y0[b] * s_cell, s_off + (x1[b] + 1) * s_cell - 1, s_off + (y1[b] + 1) * s_cell - 1);
}
/* the whole picture again from the grid (a new colour or style, a new grid) */
static void repaint_all(void)
{
    if (!s_px || !s_mem) return;
    ui_arena_canvas_clear(s_arena);
    for (int y = 0; y < s_g.h; y++)
        for (int x = 0; x < s_g.w; x++) {
            const int v = pc_life_value(&s_g, x, y);
            if (v && pc_life_in_circle(&s_g, x, y)) put_cell(x, y, s_col[pc_life_class(v)]);
        }
    pc_life_chg_clear(&s_g);
    ui_arena_canvas_dirty(s_arena, 0, 0, s_box - 1, s_box - 1);
}

static void show_status(void)
{
    char b[sizeof s_stat];
    if (!s_canvas || !s_mem) snprintf(b, sizeof b, "%s", T(S_OUT_OF_MEMORY) /* Out of memory */);
    else if (s_over) snprintf(b, sizeof b, T(S_U_GEN_PEAK_U) /* %u gen·peak %u */, (unsigned)s_g.gen, (unsigned)s_g.peak);
    else if (s_rest) snprintf(b, sizeof b, T(S_U_GEN_STILL_TAP) /* %u gen·still: tap */, (unsigned)s_g.gen);
    else if (s_g.rule == PC_LIFE_BEAT && s_g.pop == 0) snprintf(b, sizeof b, "%s", T(S_WAIT_FOR_BEAT_TAP) /* Waiting for the beat·tap to sprinkle */);
    else snprintf(b, sizeof b, T(S_U_ALIVE_D_S_S) /* %u alive·%d gen/s%s */, (unsigned)s_g.pop, gps_now(), capped() ? T(S_STR_5) /* cap */ : "");
    if (!s_status || !strcmp(b, s_stat)) return;
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}
static bool listening(void) { return s_g.rule == PC_LIFE_BEAT; }
static void feed(bool run) { ui_beat_hold(!(run && listening() && ui_screen_on())); }

static bool trails_left(void)
{
    if (!s_mem || !s_g.trail) return false;
    for (int i = 0; i < s_g.nw * s_g.h; i++) if (s_g.pl[3][i]) return true;
    return false;
}
static bool extinct(void) { return s_g.pop == 0 && s_g.gen > 0; }
static void again(void);
static void finish(void)
{
    char t[40];
    snprintf(t, sizeof t, T(S_EXTINCT_PEAK_U) /* Extinct · peak %u */, (unsigned)s_g.peak);
    const game_end_t e = { .id = "life", .variant = s_g.rule, .outcome = PC_GAME_DRAW, .vs_device = false, .moves = (int)s_g.gen, .ms = lv_tick_elaps(s_t0),
                           .title = t, .gold = false, .mode = GAME_SCORE_HIGH, .unit = T(S_GENS) /* gen */, .style = style() };   /* a score game: the generations, the most of the session is the best */
    s_over = true;
    ui_haptic_tap();
    game_result_end(s_arena, &e, again);
}
static void wake(void)
{
    s_rest = false; s_quiet = 0;
    win_reset();
    ui_arena_wake(s_arena);
    feed(true);
}

/* ---- seeding ---- */
static int blob_r(void) { return LV_MAX(3, 26 / cell()); }
static void drop_cluster(int cx, int cy, int r, int pct) { pc_life_splash(&s_g, cx, cy, r, pct); }
static void seed(void)
{
    pc_life_clear(&s_g);
    if (s_pat < 3) pc_life_fill_random(&s_g, DENS[s_pat]);
    else pc_life_place(&s_g, s_pat - 3, s_g.w / 2, s_g.h / 2, 0);
}
static void new_game(void)
{
    game_result_hide();
    s_over = false; s_rest = false; s_quiet = 0; s_phase = 0;
    s_t0 = lv_tick_get();
    s_onsets = ui_beat_onsets();
    seed();
    paint_changed();
    ui_arena_unpause(s_arena);
    wake();
    show_status();
}
static void again(void) { new_game(); }

/* (re)build the grid for the cell size: the one block is sized for the finest */
static void grid(void)
{
    s_cell = cell();
    const int w = s_box / s_cell;
    s_off = (s_box - w * s_cell) / 2;
    if (s_mem && pc_life_init(&s_g, s_mem, pc_life_mem(s_box, s_box), w, w, games_seed("life", s_rounds++))) { pc_life_shape(&s_g, ui_arena_round()); pc_life_config(&s_g, s_sel.rule, s_edge, GLOW_OF[s_glow]); }
    set_rate();
    s_extra = 0;
}
static void field(void) { ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track); }

/* the field's step: one generation every s_every steps (the rate), the beat's cluster, the painting */
static void step(void *u)
{
    (void)u;
    s_ticks++;
    if (!s_mem || s_over) return;
    {                                                        /* honest degrade: when the field's steps come further apart than 1/30 s the screen cannot keep up: wait one step more, give it back when they are on time */
        const uint32_t now = lv_tick_get();
        if (s_last_step) { s_ivl_sum += now - s_last_step; s_ivl_n++; }
        s_last_step = now;
        if (s_ivl_n >= 30) {
            const uint32_t avg = s_ivl_sum / s_ivl_n;
            if (avg > 50 && s_every + s_extra < 30) s_extra++;
            else if (avg < 40 && s_extra > 0) s_extra--;
            s_ivl_sum = s_ivl_n = 0;
        }
    }
    if (listening()) {
        const uint32_t on = ui_beat_onsets();
        if (on != s_onsets) {
            s_onsets = on;
            const int r = LV_MAX(2, 14 / cell());
            const int span = s_g.w / 3;
            drop_cluster(s_g.w / 2 + (int)(pc_life_rand(&s_g) % (uint32_t)(2 * span + 1)) - span, s_g.h / 2 + (int)(pc_life_rand(&s_g) % (uint32_t)(2 * span + 1)) - span, r, 55);
            paint_changed();
            s_quiet = 0; s_rest = false;
        }
    }
    if (++s_phase < s_every + s_extra) return;
    s_phase = 0;
    pc_life_step(&s_g);
    uint64_t any = 0;
    const uint64_t *c = pc_life_chg(&s_g);
    for (int i = 0; i < s_g.nw * s_g.h && !any; i++) any |= c[i];
    s_quiet = any ? 0 : s_quiet + 1;
    paint_changed();
    s_win_gens++;
    if (lv_tick_elaps(s_win_t0) >= 1000) { s_gps = (int)((s_win_gens * 1000 + lv_tick_elaps(s_win_t0) / 2) / lv_tick_elaps(s_win_t0)); s_win_t0 = lv_tick_get(); s_win_gens = 0; }
    if (extinct() && !listening() && !trails_left()) finish();                 /* the afterglow fades out first, then the card */
    else if (s_quiet >= QUIET_GENS && !listening()) { s_rest = true; feed(false); }
    show_status();
}
static bool running(void *u) { (void)u; return s_mem && !s_over && !s_rest && (s_g.pop > 0 || listening() || trails_left()); }

static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u;
    if (!s_mem) return;
    if (kind == UI_ARENA_STEP) {                              /* the borrowed knob (a tap lent it) / the FakePod's keys: the pattern the taps drop */
        s_pat = pc_wrapi(s_pat + v, PATS);
        ui_haptic_tick();
        game_ring_refresh();
        return;
    }
    if (s_over) { ui_haptic_tap(); again(); return; }
    ui_haptic_tap();
    sfx_play("move");
    const int cx = (x + s_box / 2 - s_off) / s_cell, cy = (y + s_box / 2 - s_off) / s_cell;
    s_taps++;
    if (game_result_gold("life", s_taps)) {                    /* the surprise: a glider gun */
        pc_life_place(&s_g, 2, cx, cy, (int)(pc_life_rand(&s_g) & 3));
        ui_arena_text(s_arena, 0, -s_box / 4, T(S_GLIDER_GUN) /* Glider gun! */, s_pal.gold);
        sfx_play("sparkle");
    }
    else if (s_pat >= 3) pc_life_place(&s_g, s_pat - 3, cx, cy, (int)(pc_life_rand(&s_g) & 7));
    else drop_cluster(cx, cy, blob_r(), 55);
    ui_arena_begin(s_arena);
    paint_changed();
    ui_arena_end(s_arena);
    wake();
    show_status();
}

static void state_line(const char *head)
{
    printf("%s cell=%d w=%d gen=%u pop=%u peak=%u rule=%d edge=%d trail=%d pat=%d every=%d gps=%d cap=%d speed=%d quiet=%d rest=%d over=%d painted=%u canvas=%d mem=%u trailcells=%d\n",
           head, cell(), s_g.w, (unsigned)s_g.gen, (unsigned)s_g.pop, (unsigned)s_g.peak, s_g.rule, s_g.torus, s_g.trail, s_pat, s_every + s_extra, gps_now(), capped(), s_sel.mode, s_quiet,
           s_rest, s_over, (unsigned)s_last_paint_cells, s_canvas, (unsigned)pc_life_mem(s_box, s_box), trails_left());
}
static void dump(void *u) { (void)u; state_line("APP game life"); }
/* console `arena game ...`: "gen <n>" n generations now (the picture follows); "seed <pct> [rng]" a clear grid, that % random (from that generator state); "place <i> <x> <y> <orient>" a library pattern centred at a cell;
 * "block <x> <y>" a 2x2 still life; "state" one line "APP life state ..." (the same fields as the dump, which every arena command prints); "clear"; "splash <x> <y> <r> <pct>"; "fresh" a new game by the axes as reseed does; "beat" one onset's cluster now; "step1" one generation by the rate's phase (as the field's tick) */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int a, b, c, d;
    if (!s_mem) { printf("APP game life cmd no memory\n"); return; }
    if (sscanf(arg, "gen %d", &a) == 1) { for (int i = 0; i < LV_CLAMP(0, a, 5000); i++) { pc_life_step(&s_g); } paint_changed(); show_status(); }
    else if (sscanf(arg, "seed %d %d", &a, &b) >= 1) { game_result_hide(); s_over = false; if (sscanf(arg, "seed %d %d", &a, &b) == 2) s_g.rng = (uint32_t)b ? (uint32_t)b : 1u; pc_life_clear(&s_g); pc_life_fill_random(&s_g, a); paint_changed(); wake(); show_status(); }
    else if (sscanf(arg, "place %d %d %d %d", &a, &b, &c, &d) == 4) { pc_life_place(&s_g, a, b, c, d); paint_changed(); wake(); show_status(); }
    else if (sscanf(arg, "splash %d %d %d %d", &a, &b, &c, &d) == 4) { drop_cluster(a, b, c, d); paint_changed(); wake(); show_status(); }
    else if (sscanf(arg, "block %d %d", &a, &b) == 2) { for (int i = 0; i < 4; i++) pc_life_set(&s_g, a + i % 2, b + i / 2, true); paint_changed(); wake(); show_status(); }
    else if (!strcmp(arg, "state")) state_line("APP life state");
    else if (!strcmp(arg, "clear")) { pc_life_clear(&s_g); paint_changed(); show_status(); }
    else if (!strcmp(arg, "fresh")) new_game();
    else if (!strcmp(arg, "beat")) { drop_cluster(s_g.w / 2, s_g.h / 2, LV_MAX(2, 14 / cell()), 55); paint_changed(); wake(); show_status(); }
    else { printf("APP game life cmd unknown %s\n", arg); return; }
}
static const ui_arena_def_t ARENA = { .name = "life", .objs = 1, .step = step, .running = running, .input = input, .dump = dump, .cmd = cmd, .full = true, .toy = true };

/* ---- the ring ---- */
static const char *mode_text(int v) { return v == 0 ? T(S_NORMAL) /* Medium speed */ : v == 1 ? T(S_FAST) /* Fast */ : T(S_SLOW) /* Slow */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static const char *axis_name(int k) { return k == 0 ? T(S_CELL) /* Cell size */ : k == 1 ? T(S_GLOW) /* Afterglow */ : k == 2 ? T(S_PATTERN) /* Pattern */ : T(S_EDGE) /* Edge */; }
static const char *axis_value(int k)
{
    static char b[24];
    if (k == 0) snprintf(b, sizeof b, "%d px", cell());
    else if (k == 1) { if (GLOW_OF[s_glow]) snprintf(b, sizeof b, "%d", GLOW_OF[s_glow]); else snprintf(b, sizeof b, "%s", T(S_OFF_2) /* Off */); }
    else if (k == 2) { if (s_pat < 3) snprintf(b, sizeof b, T(S_RANDOM_D) /* Random %d%% */, DENS[s_pat]); else snprintf(b, sizeof b, "%s", T_RAW(PAT_ZH[s_pat - 3], PAT_EN[s_pat - 3])); }
    else snprintf(b, sizeof b, "%s", s_edge ? T(S_WRAP) /* Wrap */ : T(S_WALL) /* Wall */);
    return b;
}
static bool axis_step(int k, int d)
{
    if (!s_mem) return false;
    if (k == 0) { s_size = pc_wrapi(s_size + d, CELLS); grid(); ui_arena_canvas_clear(s_arena); new_game(); }
    else if (k == 1) { s_glow = pc_wrapi(s_glow + d, GLOWS); pc_life_config(&s_g, s_g.rule, s_g.torus, GLOW_OF[s_glow]); paint_changed(); }
    else if (k == 2) s_pat = pc_wrapi(s_pat + d, PATS);
    else if (k == 3) { s_edge = pc_wrapi(s_edge + d, 2); pc_life_config(&s_g, s_g.rule, s_edge, GLOW_OF[s_glow]); paint_changed(); wake(); }
    else return false;
    show_status();
    return true;
}
static void changed(int what, int v)
{
    if (!s_mem) { if (what == GR_STYLE) { colours(); field(); } return; }
    if (what == GR_STYLE) { colours(); field(); repaint_all(); }
    else if (what == GR_MODE) { set_rate(); s_extra = 0; }
    else if (what == GR_RULE) { pc_life_config(&s_g, v, s_edge, GLOW_OF[s_glow]); s_onsets = ui_beat_onsets(); wake(); }
    else new_game();                                                                         /* Reseed */
    show_status();
}
static bool ring_cmd(const char *arg)                  /* `games cell|glow|pat|edge <n>`: the axes as the ring sets them (absolute); `games oom 1`: the next open finds no memory */
{
    int n;
    if (sscanf(arg, "oom %d", &n) == 1) s_fail_next = n != 0;
    else if (sscanf(arg, "cell %d", &n) == 1) axis_step(0, n - s_size);
    else if (sscanf(arg, "glow %d", &n) == 1) axis_step(1, n - s_glow);
    else if (sscanf(arg, "pat %d", &n) == 1) axis_step(2, n - s_pat);
    else if (sscanf(arg, "edge %d", &n) == 1) axis_step(3, n - s_edge);
    else return false;
    dump(NULL);
    return true;
}
static const char *again_text(void) { return T(S_RESEED) /* Reseed */; }
static const char *action_text(void) { return T(S_CLEAR) /* Clear */; }
static const char *action_armed(void) { return T(S_TAP_AGAIN_TO_CLEAR) /* Tap again to clear */; }
static void action_run(void)
{
    if (!s_mem) return;
    game_result_hide();
    s_over = false;
    pc_life_clear(&s_g);
    paint_changed();
    s_rest = false;
    show_status();
    ui_arena_kick();
    wake();
}
static const game_ring_t RING = { .id = "life", .modes = 3, .mode_text = mode_text, .rules = PC_LIFE_RULES, .rule_name = rule_name, .changed = changed, .cmd = ring_cmd,
                                  .axes = 4, .axis_name = axis_name, .axis_value = axis_value, .axis_step = axis_step,
                                  .again_text = again_text, .action_text = action_text, .action_armed = action_armed, .action_run = action_run };

static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C };   /* CF2: taps poke / drop / restart: the popup waits for a free knob (a turn)  (CF2: popup after a free knob turn) */
static void feed_cb(lv_timer_t *t)                          /* the beat's FFT only while the field steps (a cover or a rest stops the steps) and listens */
{
    (void)t;
    const bool ran = s_ticks != s_seen_ticks;
    s_seen_ticks = s_ticks;
    feed(ran && !s_over);
}
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    if (!s_set) { s_set = true; s_size = 2; s_pat = 1; s_glow = 2; }   /* 3 px, random 25 %, an afterglow of 3 generations */
    memset(&s_g, 0, sizeof s_g);
    s_stat[0] = 0;
    s_over = false; s_rest = false; s_extra = 0; s_ticks = s_seen_ticks = 0; s_px = NULL; s_mem = NULL; win_reset();
    s_sel.mode = LV_CLAMP(0, s_sel.mode, 2);
    ui_obj_guard(ui_arena_create(p, &ARENA, NULL), &s_arena);
    if (s_arena) { colours(); field(); }                                    /* the opaque canvas starts as the field's fill */
    s_canvas = s_arena && ui_arena_canvas565(s_arena) != NULL;
    s_px = s_canvas ? ui_arena_canvas_pixels16(s_arena, &s_stride) : NULL;
    s_box = s_canvas ? lv_obj_get_width(s_arena) : 0;
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    ui_frame_chrome(s_status);                                              /* CF2 (owner 77-78): the generation text is popup chrome, the field is the page */
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    colours();
    if (s_arena) { field(); ui_beat_on(s_arena); ui_timer_bind(s_arena, feed_cb, 500, NULL); }
    s_mem = s_px && !s_fail_next ? psram_calloc(1, pc_life_mem(s_box, s_box)) : NULL;
    s_fail_next = false;
    if (s_mem) { grid(); new_game(); }
    else show_status();
}
static void destroy(void) { psram_free(s_mem); s_mem = NULL; s_px = NULL; memset(&s_g, 0, sizeof s_g); }
static void resume(void) { ui_arena_kick(); }

const ui_screen_t scr_game_life = { .name = "tools_game_life", .create = create, .destroy = destroy, .on_resume = resume, .on_knob = ui_arena_knob, .content = true };
