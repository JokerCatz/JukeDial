// SPDX-License-Identifier: LGPL-2.1-or-later
/* Tools (spec an internal note): toys that run while the music plays,
 * no reboot, ~0 internal RAM. Ring: Dice (dice), Slot (3x3 slot),
 * Fortune (the fortune ring). Room left on the ring's first page for three list tools (Lunch, Pick a person, Idea generator).
 * Internal RAM: this file is built with -msmall-data-limit=0, so its statics are .bss in PSRAM (linker.lf); the
 * slot's reel strips, the rim LED masks and every LVGL object are PSRAM too. Symbols: Noto Color Emoji rendered
 * at build time (tools/gen/make_slot_symbols.py) into a flash asset. Logic: pc_reel (host-tested). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "pc/pc_fortune.h"
#include "pc/pc_lowmem.h"
#include "pc/pc_reel.h"
#include "cfg.h"
#include "cpu_load.h"
#include "font.h"
#include "icons_gen.h"
#include "ledrim.h"
#include "lowmem.h"
#include "reel.h"
#include "sfx.h"
#include "screens.h"
#include "games.h"
#include "ui_help.h"
#include "ui_ring.h"
#include "ui_theme.h"
#include "ui_frame.h"
#include "ui_kit.h"

#define FRAME_MS 33
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

/* animation load: core loads between the start and the end of the last roll / spin (console "tools stat") */
static cpu_snap_t s_cpu0;
static float s_load[2];
static void load_begin(void) { cpu_snap(&s_cpu0); }
static void load_end(void) { cpu_snap_t b; cpu_snap(&b); cpu_load(&s_cpu0, &b, s_load); }

/* ================= dice ================= */
/* 1-3 dice (cfg dice_n). Tap or turn the knob: the dice tumble (a new face every 3 frames, a haptic tick each
 * time), bounce and turn, land one after the other with a strong click and a rim flash; doubles / triples get a
 * gold chase. lowmem >= 2: the result at once. */
#define DIE_ROUND 72                 /* the die's edge on the round glass; the square panel makes it large (SQ2): the most 3 dice fit, 2 or 1 take more */
static lv_obj_t *s_die[3], *s_pip[3][9], *s_dsum, *s_dcount;
static int s_dn, s_dface[3], s_dframe;
static lv_timer_t *s_danim;
static uint32_t s_dt0;
static int s_drolls, s_dms, s_dframes;
static bool s_drot;                    /* the dice turn while tumbling: ~30 % of core 0 for 3 dice (transformed layers) */

static void die_face(int d, int v)
{
    static const uint16_t MASK[7] = { 0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D };   /* 3x3 pips, bit = row*3+col */
    for (int k = 0; k < 9; k++) {
        if (MASK[v] >> k & 1) lv_obj_remove_flag(s_pip[d][k], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pip[d][k], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_pip[d][k], ui_c(v == 1 ? ui_pal(UI_PAL_dice)[2] : ui_pal(UI_PAL_dice)[3]), 0);
    }
}

static int die_d(void) { return s_dn >= 3 || ui_by_panel(0, 1) ? ui_scale_big(DIE_ROUND) : ui_scale_fill(DIE_ROUND); }   /* SQ3: on the square panel every count has the size that three dice fit with */
static float die_x(int d) { return (d - (s_dn - 1) / 2.0f) * (float)(die_d() + die_d() * 22 / DIE_ROUND); }
static int die_y(void) { return -(die_d() - ui_scale(DIE_ROUND)) / 4; }   /* a large die rises a little: the hint line stays clear under it */
static void die_fit(int d)             /* the die's edge, corner, pivot and pips for the current count */
{
    const int D = die_d(), pip = D * 13 / DIE_ROUND, st = D * 20 / DIE_ROUND;
    lv_obj_set_size(s_die[d], D, D);
    lv_obj_set_style_radius(s_die[d], D * 14 / DIE_ROUND, 0);
    lv_obj_set_style_transform_pivot_x(s_die[d], D / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_die[d], D / 2, 0);
    for (int k = 0; k < 9; k++) {
        lv_obj_set_size(s_pip[d][k], pip, pip);
        lv_obj_align(s_pip[d][k], LV_ALIGN_CENTER, (k % 3 - 1) * st, (k / 3 - 1) * st);
    }
}

static void dice_layout(void)
{
    for (int d = 0; d < 3; d++) {
        die_fit(d);
        if (d < s_dn) lv_obj_remove_flag(s_die[d], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_die[d], LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_die[d], LV_ALIGN_CENTER, (int32_t)die_x(d), die_y());
        lv_obj_set_style_transform_rotation(s_die[d], 0, 0);
    }
}

static int dice_land(int d) { return 15 + 6 * d; }

static void dice_result(void)
{
    int sum = 0, same = 1;
    for (int d = 0; d < s_dn; d++) sum += s_dface[d];
    for (int d = 1; d < s_dn; d++) if (s_dface[d] != s_dface[0]) same = 0;
    if (s_dn > 1) lv_label_set_text_fmt(s_dsum, same ? T(S_S_TOTAL_D) /* %s! total %d */ : T(S_STOTAL_D) /* %s total %d */,
                                        same ? (s_dn == 3 ? T(S_TRIPLE) /* Triple */ : T(S_DOUBLE) /* Pair */) : "", sum);
    else lv_label_set_text_fmt(s_dsum, "%d", sum);
    lv_obj_set_style_text_color(s_dsum, UI_FG, 0);
    lv_obj_set_style_text_font(s_dsum, font_large(), 0);
    if (s_dn > 1 && same) { ledrim_chase(ui_tok(UI_PAL_gold), 2); ui_haptic_strong(); sfx_play("win"); }
    s_drolls++;
    printf("APP tools dice n=%d faces=%d,%d,%d sum=%d same=%d\n", s_dn, s_dface[0], s_dface[1], s_dface[2], sum, s_dn > 1 && same);
}

static void dice_step(lv_timer_t *t)
{
    (void)t;
    int f = s_dframe++;
    s_dframes++;
    bool tumbled = false;
    for (int d = 0; d < s_dn; d++) {
        int land = dice_land(d);
        if (f < land) {
            if (f % 3 == 0) { die_face(d, 1 + (int)pc_rng_below(ui_rng(), 6)); tumbled = true; }
            float k = 1.0f - (float)f / (float)land;                     /* the bounces die down */
            int y = die_y() - (int)(30.0f * k * fabsf(sinf((float)f * 0.55f + (float)d)));
            int x = (int)die_x(d) + (int)(8.0f * k * sinf((float)(f + 2 * d) * 1.7f));
            lv_obj_align(s_die[d], LV_ALIGN_CENTER, x, y);
            if (s_drot) lv_obj_set_style_transform_rotation(s_die[d], (int32_t)((f * 370 + d * 900) % 3600) * (k > 0.3f ? 1 : 0), 0);
        } else if (f == land) {
            die_face(d, s_dface[d]);
            lv_obj_align(s_die[d], LV_ALIGN_CENTER, (int32_t)die_x(d), die_y());
            lv_obj_set_style_transform_rotation(s_die[d], 0, 0);
            ui_haptic_strong();                                          /* each die lands with a strong click */
            sfx_play("dice");
            ledrim_flash(ui_theme()->a1, 350);
        }
    }
    if (tumbled && ui_screen_on()) ui_haptic_tick();                     /* one tick per tumble */
    if (tumbled) sfx_play("tick");
    if (f >= dice_land(s_dn - 1)) {
        lv_timer_delete(s_danim);
        s_danim = NULL;
        s_dms = (int)(lv_tick_get() - s_dt0);
        load_end();
        dice_result();
    }
}

static void dice_roll(void)
{
    if (s_danim) return;
    for (int d = 0; d < 3; d++) s_dface[d] = d < s_dn ? pc_dice_roll(ui_rng()) : 0;
    lv_label_set_text(s_dsum, "");
    ui_frame_hint(s_dsum, false);                          /* the total is content again */
    if (lowmem_level() >= PC_LOWMEM_STILL || !ui_screen_on()) {          /* no animation: the result at once */
        for (int d = 0; d < s_dn; d++) die_face(d, s_dface[d]);
        dice_layout();
        ui_haptic_strong();
        sfx_play("dice");
        ledrim_flash(ui_theme()->a1, 350);
        s_dframes = 0; s_dms = 0;
        printf("APP tools dice still lowmem=%d\n", lowmem_level());
        dice_result();
        return;
    }
    int rot = cfg_int("dice_rot", 1);       /* 0 never, 1 not in Bluetooth modes (the BT stack shares core 0), 2 always */
    s_drot = rot == 2 || (rot == 1 && cfg_int("bt_mode", 0) == 0);
    s_dframe = 0;
    s_dframes = 0;
    s_dt0 = lv_tick_get();
    load_begin();
    s_danim = lv_timer_create(dice_step, FRAME_MS, NULL);
}
/* the count: an axis, 1..3 wrapping (a tap steps +1, the knob both ways while borrowed); inert while the dice tumble */
static const char *dice_count_text(int axis) { static char b[24]; (void)axis; snprintf(b, sizeof b, T(S_DICE_D) /* Dice: %d */, s_dn); return b; }
static bool dice_count_step(int axis, int d)
{
    (void)axis;
    if (s_danim) return false;
    s_dn = ((s_dn - 1 + d % 3) % 3 + 3) % 3 + 1;
    cfg_set_int("dice_n", s_dn);
    dice_layout();
    return true;
}
static bool dice_knob(int d) { if (!ui_axis_knob(d)) dice_roll(); return true; }
static const ui_screen_t scr_dice;
static void dice_set(int n)                  /* console: tools dice N */
{
    if (n < 1 || n > 3 || ui_top() != &scr_dice) return;
    dice_count_step(0, n - s_dn);
    if (s_dcount) lv_label_set_text(lv_obj_get_child(s_dcount, 0), dice_count_text(0));
}

static void dice_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_timer_guard(p, &s_danim);
    ledrim_create(p);
    s_dn = cfg_int("dice_n", 2);
    if (s_dn < 1 || s_dn > 3) s_dn = 2;
    for (int d = 0; d < 3; d++) {
        s_die[d] = box(p, DIE_ROUND, DIE_ROUND, ui_palc(UI_PAL_dice, 0), 14);      /* sized by die_fit (dice_layout, below) */
        ui_face_outline(s_die[d], 2);                      /* light themes: the white die on the near-white page gets an edge */
        for (int k = 0; k < 9; k++) s_pip[d][k] = box(s_die[d], 13, 13, ui_palc(UI_PAL_dice, 1), LV_RADIUS_CIRCLE);
        s_dface[d] = 1 + d * 2;
        die_face(d, s_dface[d]);
    }
    s_dcount = ui_obj_guard(ui_axis_button(p, UI_SLOT_HEAD, 0, dice_count_step, dice_count_text), &s_dcount);
    ui_frame_option(s_dcount);                             /* the page frame's overlay (ui_frame.h) has the dice count */
    s_dsum = ui_text(p, UI_SLOT_LOWER, NULL);              /* the total (large, set with the result); before the first roll it is the way to roll */
    lv_label_set_text_fmt(s_dsum, T(S_TAP_OR_TURN_TO_ROLL) /* Tap or turn %s to roll */, ui_word_knob());
    lv_obj_set_style_text_color(s_dsum, UI_DIM, 0);
    ui_frame_hint(s_dsum, true);                           /* the way to roll is popup text (it would be cut by the card's edge in the content) */
    dice_layout();
    ui_back_button(p);
    ui_page_tap(p, dice_roll);
}
static void dice_destroy(void) { if (s_danim) { lv_timer_delete(s_danim); s_danim = NULL; } ledrim_destroy(); }
static const ui_screen_t scr_dice = { .name = "tools_dice", .create = dice_create, .destroy = dice_destroy, .on_knob = dice_knob, .content = true };

/* ================= slot machine ================= */
/* 3 reels x 3 rows, 5 lines (rows + diagonals), pc_reel odds and pays. Points only, no money: score = points won,
 * with spins, wins and the best win, kept in cfg "slot" ("score spins wins best", one key, saved with the usual
 * 3 s debounce after a spin). Tap or knob: spin; again while spinning: stop soon (same result). */
#define CELL_W ui_by_panel(ui_scale_big(64), 98)   /* the square panel's reels are larger (SQ2); the symbols are enlarged with them (reel_cell_image_zoom) */
#define CELL_H ui_by_panel(ui_scale_big(48), 82)   /* SQ3: the square panel's cells are nearly square (taller, not the 3 : 4 of the round ones) */                  /* the symbols are 52 px: 2 px of their margin is cut top and bottom; the rows end where the old ones did */
#define GAP ui_scale_big(6)
#define SYM_PX 52
#define BLOCK_W (3 * CELL_W + 2 * GAP)
#define BLOCK_H (3 * CELL_H)
#define BLOCK_Y ui_by_panel(-8, -6)                /* the frame starts 4 px under the HEAD pill */
extern const uint8_t slot_syms_start[] asm("_binary_slot_syms_bin_start");
extern const uint8_t slot_syms_end[] asm("_binary_slot_syms_bin_end");
static lv_image_dsc_t s_sym[PC_SYM_N];
EXT_RAM_BSS_ATTR static reel_zoom_t s_symz;   /* M4: PSRAM */
static uint8_t s_strip[PC_SLOT_REELS][PC_SLOT_LEN];
static const uint8_t *s_strips[PC_SLOT_REELS];
static reel_view_t *s_reels;
static int s_stops[PC_SLOT_REELS], s_lpay[PC_SLOT_LINES];
static lv_obj_t *s_line[PC_SLOT_LINES], *s_score, *s_res;
static int s_score_n, s_spins, s_wins, s_best, s_last_win;
static bool s_spinning;
static int s_bx, s_by, s_abs[PC_SLOT_LINES][4];   /* the reel block's top left; each stroke's end points */
static bool s_force;                              /* console `tools grid`: the next slot page puts s_fgrid on the first three stops of every strip */
static uint8_t s_fgrid[PC_SLOT_ROWS][PC_SLOT_REELS];
#define LINE_COL ui_pal(UI_PAL_slot_lines)   /* PC_SLOT_LINES entries */

static bool syms_load(void)
{
    const uint8_t *b = slot_syms_start;
    if (((uintptr_t)slot_syms_end - (uintptr_t)b) < 8 || memcmp(b, "SYM1", 4) || b[4] != PC_SYM_N) return false;
    int px = b[5];
    size_t one = (size_t)px * px * 3;
    if (((uintptr_t)slot_syms_end - (uintptr_t)b) < 8 + one * PC_SYM_N) return false;
    for (int k = 0; k < PC_SYM_N; k++) {
        lv_image_dsc_t *im = &s_sym[k];
        memset(im, 0, sizeof *im);
        im->header.magic = LV_IMAGE_HEADER_MAGIC;
        im->header.cf = LV_COLOR_FORMAT_RGB565A8;
        im->header.w = (uint32_t)px;
        im->header.h = (uint32_t)px;
        im->header.stride = (uint32_t)px * 2;
        im->data = b + 8 + one * k;
        im->data_size = (uint32_t)one;
    }
    return true;
}

/* the score pill (HEAD) and the spin statistics (a line on the pay table): each text built here only */
static void slot_texts(void)
{
    char b[48];
    snprintf(b, sizeof b, T(S_SCORE_D_ODDS) /* Score %d · pay table */, s_score_n);
    ui_pill_set_text(s_score, b);
}
static void slot_stats_text(char *b, size_t n) { snprintf(b, n, T(S_D_SPINS_D_WINS_BEST_D) /* %d spins · %d wins · best %d */, s_spins, s_wins, s_best); }

static void slot_load(void)
{
    s_score_n = s_spins = s_wins = s_best = 0;
    sscanf(cfg_str("slot", ""), "%d %d %d %d", &s_score_n, &s_spins, &s_wins, &s_best);
}

static void slot_save(void)
{
    char b[64];
    snprintf(b, sizeof b, "%d %d %d %d", s_score_n, s_spins, s_wins, s_best);
    cfg_set_str("slot", b);
}

static void lines_hide(void) { for (int l = 0; l < PC_SLOT_LINES; l++) lv_obj_add_flag(s_line[l], LV_OBJ_FLAG_HIDDEN); }

static const char *sym_name(int k)
{
    switch (k) {
    case PC_SYM_CHERRY: return T(S_CHERRY) /* Cherry */;
    case PC_SYM_LEMON: return T(S_LEMON) /* Lemon */;
    case PC_SYM_GRAPE: return T(S_GRAPE) /* Grape */;
    case PC_SYM_BELL: return T(S_BELL) /* Bell */;
    case PC_SYM_STAR: return T(S_STAR) /* Star */;
    case PC_SYM_DIAMOND: return T(S_DIAMOND) /* Diamond */;
    case PC_SYM_SEVEN: return T(S_SEVEN) /* Seven */;
    default: return T(S_CLOVER) /* Clover */;
    }
}

static void rule_line(void)
{
    lv_label_set_text(s_res, T(S_3_ALIKE_WIN_CLOVER_WILD) /* Three of a kind to win · clover is wild */);   /* the cherry rule: the pay table */
    lv_obj_set_style_text_color(s_res, UI_DIM, 0);
}

/* the stroke of line l over its first `cells` cells (screen coordinates in s_abs, printed by `tools lines`) */
static void line_set(int l, int cells)
{
    static lv_point_precise_t pts[PC_SLOT_LINES][2];
    int ra = pc_slot_line_rows[l][0], rb = pc_slot_line_rows[l][cells - 1];
    int x0 = s_bx + 6, x1 = s_bx + cells * CELL_W + (cells - 1) * GAP - 6;
    int y0 = s_by + ra * CELL_H + CELL_H / 2, y1 = s_by + rb * CELL_H + CELL_H / 2;
    if (ra != rb) { y0 += ra < rb ? -CELL_H / 2 + 8 : CELL_H / 2 - 8; y1 += rb < ra ? -CELL_H / 2 + 8 : CELL_H / 2 - 8; }
    s_abs[l][0] = x0; s_abs[l][1] = y0; s_abs[l][2] = x1; s_abs[l][3] = y1;
    int mx = LV_MIN(x0, x1), my = LV_MIN(y0, y1);
    pts[l][0].x = x0 - mx + 3; pts[l][0].y = y0 - my + 3;
    pts[l][1].x = x1 - mx + 3; pts[l][1].y = y1 - my + 3;
    lv_line_set_points(s_line[l], pts[l], 2);
    lv_obj_set_pos(s_line[l], mx - 3, my - 3);
}

/* the result label is one line (UI_SLOT_FOOT: the room between the reels and Back): the paying line when one paid; with more,
 * the total and how many lines paid (the strokes on the reels show which) */
static void win_text(const pc_slot_win_t *w, char *out, size_t n)
{
    if (w->cells == 2) snprintf(out, n, T(S_TWO_CHERRIES_FROM_THE_LEFT_D) /* Two cherries from the left +%d */, w->pay);
    else if (w->wild && w->sym != PC_SYM_CLOVER) snprintf(out, n, T(S_S_3_WILD_D) /* %s×3 (clover substitutes) +%d */, sym_name(w->sym), w->pay);
    else snprintf(out, n, T(S_S_3_D), sym_name(w->sym), w->pay);
}

static void result_text(const pc_slot_win_t *w, int n, int total)
{
    char b[80];
    if (n == 1) win_text(&w[0], b, sizeof b);
    else snprintf(b, sizeof b, T(S_D_ON_D_LINES) /* Total +%d (%d lines) */, total, n);
    lv_label_set_text(s_res, b);
}

static void slot_landed(void *u, int reel) { (void)u; (void)reel; if (ui_screen_on()) ui_haptic_tick(); sfx_play("stop"); }
static void slot_stepped(void *u) { (void)u; sfx_play("tick"); }   /* a reel passed a stop this frame */

static void slot_done(void *u)
{
    (void)u;
    s_spinning = false;
    load_end();
    uint8_t g[PC_SLOT_ROWS][PC_SLOT_REELS];
    pc_slot_grid(s_strips, PC_SLOT_LEN, s_stops, g);
    pc_slot_win_t wins[PC_SLOT_LINES];
    int nl = pc_slot_wins(g, wins), win = 0;
    for (int l = 0; l < PC_SLOT_LINES; l++) s_lpay[l] = 0;
    for (int k = 0; k < nl; k++) { win += wins[k].pay; s_lpay[wins[k].line] = wins[k].pay; }
    s_last_win = win;
    s_spins++;
    uint32_t col = 0;
    for (int k = 0; k < nl; k++) {
        int l = wins[k].line;
        line_set(l, wins[k].cells);
        lv_obj_remove_flag(s_line[l], LV_OBJ_FLAG_HIDDEN);
        if (!col) col = LINE_COL[l];
    }
    if (win) {
        sfx_play(win >= 60 ? "jackpot" : "win");      /* the big pays (the rim chase below) get the jackpot jingle */
        s_wins++;
        s_score_n += win;
        if (win > s_best) s_best = win;
        result_text(wins, nl, win);
        lv_obj_set_style_text_color(s_res, ui_c(col), 0);
        if (win >= 60) { ui_haptic_strong(); ledrim_chase(ui_tok(UI_PAL_gold), 3); }
        else { ui_haptic_tap(); ledrim_flash(col, 600); }
    } else rule_line();
    slot_texts();
    slot_save();
    int fr, ms, sa, sm;
    reel_stat(s_reels, &fr, &ms, &sa, &sm);
    printf("APP tools slot stops=%d,%d,%d grid=%d%d%d/%d%d%d/%d%d%d win=%d lines=%d,%d,%d,%d,%d score=%d spins=%d frames=%d ms=%d step_avg=%d step_max=%d load0=%.0f load1=%.0f\n",
           s_stops[0], s_stops[1], s_stops[2], g[0][0], g[0][1], g[0][2], g[1][0], g[1][1], g[1][2], g[2][0], g[2][1], g[2][2],
           win, s_lpay[0], s_lpay[1], s_lpay[2], s_lpay[3], s_lpay[4], s_score_n, s_spins, fr, ms, sa, sm, (double)s_load[0], (double)s_load[1]);
}

static void slot_spin_to(const int *stops)
{
    if (!s_reels) return;
    if (s_spinning) { reel_stop_early(s_reels); return; }
    for (int r = 0; r < PC_SLOT_REELS; r++) s_stops[r] = stops[r];
    s_spinning = true;
    lines_hide();
    lv_label_set_text(s_res, T(S_TAP_TO_STOP_2) /* Tap to stop */);
    lv_obj_set_style_text_color(s_res, UI_DIM, 0);
    load_begin();
    const reel_cbs_t cb = { .reel_landed = slot_landed, .done = slot_done, .stepped = slot_stepped };
    reel_spin(s_reels, s_stops, &cb);
}

static void slot_spin(void)
{
    int st[PC_SLOT_REELS];
    for (int r = 0; r < PC_SLOT_REELS; r++) st[r] = (int)pc_rng_below(ui_rng(), PC_SLOT_LEN);
    slot_spin_to(st);
}

/* console: a spin that pays at least min points (screenshots of a win) */
static void slot_spin_win(int min)
{
    int st[PC_SLOT_REELS];
    uint8_t g[PC_SLOT_ROWS][PC_SLOT_REELS];
    for (int tries = 0; tries < 200000; tries++) {
        for (int r = 0; r < PC_SLOT_REELS; r++) st[r] = (int)pc_rng_below(ui_rng(), PC_SLOT_LEN);
        pc_slot_grid(s_strips, PC_SLOT_LEN, st, g);
        if (pc_slot_eval(g, NULL) >= min) break;
    }
    slot_spin_to(st);
}

static bool slot_knob(int d) { (void)d; slot_spin(); return true; }

static void line_make(lv_obj_t *p, int l)
{
    lv_obj_t *o = lv_line_create(p);
    lv_obj_set_style_line_width(o, 5, 0);
    lv_obj_set_style_line_rounded(o, true, 0);
    lv_obj_set_style_line_color(o, ui_c(LINE_COL[l]), 0);
    lv_obj_set_style_line_opa(o, LV_OPA_80, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    s_line[l] = o;
    line_set(l, 3);
}

/* ---- the pay table (a tap on the score pill): symbol x3 = pay, the stats, then 說明 (the rules); a tap or Back leaves, the knob scrolls ---- */
static lv_obj_t *s_paylist;
static void pay_leave_tap(void) { ui_pop(); }
static void pay_leave(lv_event_t *e) { (void)e; pay_leave_tap(); }
static bool pay_knob(int d) { return ui_list_knob(s_paylist, d); }
static void pay_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list(p), &s_paylist);
    char b[48];
    for (int k = 0; k < PC_SYM_N; k++) {
        snprintf(b, sizeof b, "%s ×3 = %d", sym_name(k), pc_slot_pay3[k]);
        ui_list_row(s_paylist, b, UI_PANEL, pay_leave, NULL);
    }
    char st[64];
    slot_stats_text(st, sizeof st);
    ui_list_note(s_paylist, st);
    ui_list_help_row(s_paylist, &HELP_SLOT);                 /* the last row: the five lines and the cherry rule */
    ui_page_tap(s_paylist, pay_leave_tap);
    ui_back_button(p);
}
static const ui_screen_t scr_slot_pay = { .name = "tools_slot_pay", .create = pay_create, .on_knob = pay_knob };
static void pay_open(lv_event_t *e) { (void)e; if (!s_spinning) ui_push(&scr_slot_pay, NULL); }

static void slot_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ledrim_create(p);
    slot_load();
    s_spinning = false;
    s_reels = NULL;
    pc_rng_t sr;
    pc_rng_seed(&sr, 0x510750u);                       /* the same three strips every time, as a real machine */
    for (int r = 0; r < PC_SLOT_REELS; r++) { pc_reel_strip(&sr, pc_slot_counts, PC_SYM_N, s_strip[r], PC_SLOT_LEN); s_strips[r] = s_strip[r]; }
    if (s_force) {
        for (int r = 0; r < PC_SLOT_REELS; r++) for (int row = 0; row < PC_SLOT_ROWS; row++) s_strip[r][row] = s_fgrid[row][r];
        s_force = false;
    }
    int bx, by;
    ui_design_place(BLOCK_W, BLOCK_H, &bx, &by);        /* the middle of the 360 design box (the square panel is bigger) */
    by += BLOCK_Y;
    s_bx = bx; s_by = by;
    lv_obj_t *frame = box(p, BLOCK_W + 8, BLOCK_H + 8, UI_PANEL, 12);
    lv_obj_set_pos(frame, bx - 4, by - 4);
    if (syms_load()) {
        s_symz = (reel_zoom_t){ .im = s_sym, .zoom = 256 * CELL_W / 64 };
        const reel_desc_t d = { .nreels = PC_SLOT_REELS, .rows = PC_SLOT_ROWS, .cell_w = CELL_W, .cell_h = CELL_H, .gap = GAP,
                                .len = PC_SLOT_LEN, .strips = s_strips, .draw_cell = reel_cell_image_zoom, .user = &s_symz,
                                .bg = ui_theme()->panel2 };
        s_reels = reel_create(p, &d);
    }
    if (s_reels) {
        lv_obj_set_pos(reel_obj(s_reels), bx, by);
        for (int r = 0; r < PC_SLOT_REELS; r++) s_stops[r] = (int)pc_rng_below(ui_rng(), PC_SLOT_LEN);
        reel_show(s_reels, s_stops);
    }
    for (int l = 0; l < PC_SLOT_LINES; l++) line_make(p, l);
    s_score = ui_pill(p, UI_SLOT_HEAD, NULL, pay_open, NULL);        /* the score; a tap opens the pay table */
    s_res = ui_text(p, UI_SLOT_FOOT, NULL);                            /* one line between the reels and Back (FOOT clears Back by one gap) */
    if (s_reels) rule_line();
    else { lv_label_set_text(s_res, T(S_OUT_OF_MEMORY) /* Out of memory */); lv_obj_set_style_text_color(s_res, UI_DIM, 0); }
    slot_texts();
    ui_back_button(p);
    ui_page_tap(p, slot_spin);
}

static void slot_destroy(void)
{
    reel_destroy(s_reels);
    s_reels = NULL;
    s_spinning = false;
    ledrim_destroy();
}
static const ui_screen_t scr_slot = { .name = "tools_slot", .create = slot_create, .destroy = slot_destroy, .on_knob = slot_knob, .content = true };

/* ================= the tools ring ================= */
/* fourteen, two pages (the last cells are the flashlight, ui_torch.c, and the tuner, scr_tuner.c): three toys, the three list toys (scr_lists.c), Vocabulary (scr_vocab.c), Bubble wrap (scr_bubbles.c, on the shared bubble field), Games (scr_games.c: the hub of the games in games.def), Sound bank (scr_sfxbank.c,
 * the sound bank's audition list, sfx task 2). The moon is a clock face now;
 * `tools moon` (console) opens the clock page on it. */
enum { TL_DICE, TL_SLOT, TL_FORTUNE, TL_LUNCH, TL_PEOPLE, TL_IDEAS, TL_VOCAB, TL_BUBBLES, TL_GAMES, TL_SFXBANK, TL_COUNTER, TL_AMBIENT, TL_TORCH, TL_TUNER, TL_COUNT, TL_MOON = TL_COUNT };   /* TL_MOON: console only, not in the ring */
extern bool clock_screen_open(void);
static void tools_pick(int i)
{
    printf("APP tools pick=%d\n", i);
    if (i == TL_DICE) ui_push(&scr_dice, NULL);
    else if (i == TL_SLOT) ui_push(&scr_slot, NULL);
    else if (i == TL_MOON) { if (clock_screen_open()) ui_push_message(T(S_THE_CLOCK_IS_OPEN) /* The clock is already open */); else ui_push(&scr_clock, (void *)1); }
    else if (i == TL_FORTUNE) ui_push(&scr_fortune, NULL);
    else if (i >= TL_LUNCH && i <= TL_IDEAS) lists_open(i - TL_LUNCH);
    else if (i == TL_VOCAB) vocab_open();
    else if (i == TL_BUBBLES) ui_push(&scr_bubbles, NULL);
    else if (i == TL_GAMES) ui_push(&scr_games, NULL);
    else if (i == TL_SFXBANK) ui_push(&scr_sfxbank, NULL);
    else if (i == TL_COUNTER) ui_push(&scr_counter, NULL);
    else if (i == TL_AMBIENT) ui_push(&scr_ambient, NULL);
    else if (i == TL_TORCH) ui_push(&scr_torch, NULL);
    else if (i == TL_TUNER) ui_push(&scr_tuner, NULL);
}
static lv_obj_t *s_ring;                       /* valid while scr_tools is the top screen (console `tools ring`) */
static void tools_back(void) { ui_pop(); }
static void tools_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    const char *labels[TL_COUNT] = { T(S_DICE) /* Dice */, T(S_SLOT) /* Slot */, T(S_FORTUNE) /* Fortune */,
                                     T(S_LUNCH) /* Lunch */, T(S_DRAW_3) /* Pick */, T(S_IDEAS) /* Idea */, T(S_WORDS) /* Vocab */, T(S_BUBBLE_WRAP) /* Bubble wrap */, T(S_GAMES_2) /* Games */, T(S_SOUND_BANK) /* Sound bank */, T(S_COUNTERS_2) /* Counters */, T(S_AMBIENT) /* Ambient */, T(S_TORCH) /* Flashlight */, T(S_TUNER) /* Tuner */ };
    static const char *const icons[TL_COUNT] = { [TL_DICE] = UI_ICON_DICE, [TL_SLOT] = UI_ICON_SLOT, [TL_FORTUNE] = UI_ICON_FORTUNE, [TL_LUNCH] = UI_ICON_LUNCH, [TL_PEOPLE] = UI_ICON_PICK, [TL_IDEAS] = UI_ICON_IDEAS,
        [TL_VOCAB] = UI_ICON_VOCAB, [TL_BUBBLES] = UI_ICON_BUBBLES, [TL_GAMES] = UI_ICON_GAMES, [TL_SFXBANK] = UI_ICON_SFXBANK, [TL_COUNTER] = UI_ICON_COUNTER, [TL_AMBIENT] = UI_ICON_AMBIENT, [TL_TORCH] = UI_ICON_TORCH, [TL_TUNER] = UI_ICON_TUNER };
    s_ring = ui_ring_create(p, labels, TL_COUNT, tools_pick, tools_back);
    ui_ring_set_icons(s_ring, icons, TL_COUNT);
}
const ui_screen_t scr_tools = { .name = "tools", .create = tools_create };

/* console (tests, screenshots):
 * tools grid ABCDEFGHI           open a fresh slot page and spin to exactly this grid (9 digits 0..7, row by row)
 * tools lines                    the visible strokes of the slot page: line l and its end points
 * tools                          open the ring          tools dice|slot|moon|fortune|bubbles|games|sfxbank|counter|ambient|torch|tuner   open that page (moon = the clock's moon face; bubbles: console `bubbles`, ui_bubbles.h;
 *                                                       sfxbank: console `sfxbank`, scr_sfxbank.c)
 * tools ring                    items and page of the open ring (turns one page first: a one-page ring stays on 0)
 * tools roll | spin              the page's action      tools spin win|big             a spin paying >= 1 | >= 60
 * tools stop                     stop the reels soon    tools dice N                   how many dice (saved: cfg dice_n)
 * tools stat                     last roll / spin numbers
 * tools slotreset                score, spins, wins, best back to 0
 * tools lunch|people|ideas       the list toys          (on one: spin | stop | btn 0|1 | group | stat, scr_lists.c) */
void tools_cmd(const char *a)
{
    static const char *const NAMES[TL_COUNT + 1] = { "dice", "slot", "fortune", "lunch", "people", "ideas", "vocab", "bubbles", "games", "sfxbank", "counter", "ambient", "torch", "tuner", "moon" };
    int n;
    char gbuf[12];
    if (a[0] && lists_cmd(a)) { printf("APP tools top=%s\n", ui_top() ? ui_top()->name : "-"); return; }
    if (!a[0]) { while (ui_top() && ui_top() != &scr_tools && !strncmp(ui_top()->name, "tools", 5)) ui_pop(); if (ui_top() != &scr_tools) ui_push(&scr_tools, NULL); return; }
    for (int i = 0; i <= TL_COUNT; i++) if (!strcmp(a, NAMES[i])) {
        while (ui_top() && ui_top() != &scr_tools && (!strncmp(ui_top()->name, "tools", 5) || !strcmp(ui_top()->name, "clock"))) ui_pop();
        if (ui_top() != &scr_tools) ui_push(&scr_tools, NULL);
        tools_pick(i);
        printf("APP tools top=%s\n", ui_top() ? ui_top()->name : "-");
        return;
    }
    if (!strcmp(a, "ring")) {
        if (ui_top() == &scr_tools) { ui_ring_page(s_ring, 1); printf("APP tools ring items=%d page=%d pages=%d\n", TL_COUNT, ui_ring_get_page(s_ring), ui_ring_get_page(s_ring) ? 2 : 1); }
        else printf("APP tools no ring\n");
    }
    else if (!strcmp(a, "roll") || !strcmp(a, "spin")) {
        if (ui_top() == &scr_dice) dice_roll(); else if (ui_top() == &scr_slot) slot_spin(); else printf("APP tools no action\n");
    }
    else if (!strcmp(a, "spin win") || !strcmp(a, "spin big")) { if (ui_top() == &scr_slot) slot_spin_win(!strcmp(a, "spin win") ? 1 : 60); }
    else if (sscanf(a, "grid %9[0-7]", gbuf) == 1 && strlen(gbuf) == 9) {   /* a spin that ends on exactly this grid (row by row, symbols 0..7) */
        for (int i = 0; i < 9; i++) s_fgrid[i / 3][i % 3] = (uint8_t)(gbuf[i] - '0');
        s_force = true;
        tools_cmd("slot");
        if (ui_top() == &scr_slot) { const int z[PC_SLOT_REELS] = { 0, 0, 0 }; slot_spin_to(z); }
        s_force = false;
        return;
    }
    else if (!strcmp(a, "lines")) {
        for (int l = 0; l < PC_SLOT_LINES; l++)
            if (ui_top() == &scr_slot && s_line[l] && !lv_obj_has_flag(s_line[l], LV_OBJ_FLAG_HIDDEN))
                printf("APP tools line %d x=%d,y=%d x2=%d,y2=%d\n", l, s_abs[l][0], s_abs[l][1], s_abs[l][2], s_abs[l][3]);
    }
    else if (!strcmp(a, "stop")) { if (s_reels && s_spinning) reel_stop_early(s_reels); }
    else if (sscanf(a, "dice %d", &n) == 1) { if (ui_top() != &scr_dice) tools_cmd("dice"); dice_set(n); printf("APP tools dice_n=%d\n", s_dn); }
    else if (!strcmp(a, "stat")) {
        int fr, ms, sa, sm;
        reel_stat(s_reels, &fr, &ms, &sa, &sm);
        printf("APP tools stat dice rolls=%d frames=%d ms=%d slot frames=%d ms=%d step_avg=%d step_max=%d last_win=%d load0=%.0f load1=%.0f lowmem=%d dice_n=%d\n",
               s_drolls, s_dframes, s_dms, fr, ms, sa, sm, s_last_win, (double)s_load[0], (double)s_load[1], lowmem_level(), s_dn);
        ui_fps_stat();
    }
    else if (!strcmp(a, "slotreset")) { cfg_del("slot"); slot_load(); if (ui_top() == &scr_slot) slot_texts(); }
    else printf("APP tools unknown %s\n", a);
    printf("APP tools top=%s\n", ui_top() ? ui_top()->name : "-");
}
