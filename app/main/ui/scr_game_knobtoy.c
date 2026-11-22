// SPDX-License-Identifier: LGPL-2.1-or-later
/* Knob toys (ideas G6): two Playdate-style toys on ONE games-hub page, the 規則 axis picks the toy: Wind-up (wind a spring to its hidden sweet spot and
 * release it; three releases, the total distance) and Fishing (keep the rod in the fish's zone while it swims and dashes: the catch fills inside, the line tension
 * rises outside). Both are ONE number on the shared dial (ui_dial) moved by whole steps of 1: the borrowed knob, a tap on the dial's half, a FakePod key press
 * (the two keys = knob -1 / +1, any key a tap: no analog mechanic, every step is one unit). Rules: pc_game_knobtoy (host-tested). G6b: four more rules are the marble maze
 * (gravity / repel / attract / tilt, scr_game_knobtoy_maze.c on the shared field ui_arena, rules in pc_game_maze): they have a field instead of the dial; tilt is offered only with a
 * tilt source (the FakePod's IMU, imu_svc; the simulator: console `simimu x y` or `maze grav`). Wind-up: the dial is the spring's tension 0..40, the sweet spot is
 * hidden (the dial's glow and the graded haptic point to it), the centre tap releases (the exact spot shows as a mark on the rim afterwards), a spring wound to the end
 * snaps (a buzz), a wound spring slackens by one every 1.5 s. Fishing: the dial is the rod's place 0..99, the fish's zone is the rim arc (it moves), the hub shows
 * the catch in percent, the status the line tension, the needle shakes when the line is tight. The golden spring (distance x2) and the golden fish (slower, tires
 * sooner) are the rare surprise (game_result_gold). Only composes shared pieces (ui_dial, game_ring, game_result, game_style, ui_exit); state in PSRAM (.bss:
 * this file is built with -msmall-data-limit=0). The dial is made again when the rule changes (its scale differs). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "imu_svc.h"
#include "knobtoy_maze.h"
#include "pc/pc_game_knobtoy.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_dial.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

const ui_game_help_t game_help_knobtoy = {
    .zh = "兩個靠旋鈕的小玩意，上方右邊的格子打開選單，規則選哪一個。轉旋鈕（點一下刻度盤之後旋鈕才歸它，停下四秒後回到音量），或點刻度盤的左半邊、右半邊各一格；"
          "按鍵的機器上兩個鍵就是左右一格。中間的圓是放開鍵。"
          "上發條：轉上去上緊發條，轉回來放鬆，上緊的發條每一秒半會鬆掉一格。發條有一個看不見的最佳位置，越靠近，指針旁的光暈越亮、手感越強。點中間的圓放開玩具，"
          "剛好在最佳位置跑得最遠，鬆了或太緊都會變近，轉到底發條就斷了，這一次算零。放三次，比總距離。"
          "釣魚：魚在刻度盤上游來游去，偶爾猛衝一下，邊上的弧線是牠的範圍。把指針留在範圍裡，釣到的進度就上升、線放鬆；離開範圍線就越來越緊，緊到滿線就斷了，離得越遠越快。進度滿了就釣到，成績看轉動的次數和時間。"
          "迷宮（規則裡有四種）：迷宮不會轉，轉的是力的方向。圓形的迷宮有四道圈牆，每道有一個缺口。重力迷宮：彈珠從中心出發，轉旋鈕改變重力的方向（邊上的小弧標出「下」在哪），讓它滾到缺口、掉出去；落到最外面的出口就過關。"
          "斥力迷宮：一直有一股力把彈珠往外推，轉旋鈕讓它沿著圈走，對準缺口就會被推出去。吸力迷宮：反過來，彈珠從外圈出發被往中心吸，轉旋鈕對準缺口，到中心過關。"
          "點一下開始；點場地的左半邊、右半邊各轉一格。左邊的格子換缺口：不動、慢慢漂、快速漂。成績看時間和旋鈕轉的次數。有感應器的機器（FakePod）還有傾斜迷宮：方格迷宮加陷阱洞，傾斜機器讓彈珠滾，掉進洞回到起點，滾到右下角過關。"
          "左邊的格子換光暈的強度。風格換顏色。螢幕關掉或打開選單時暫停，點一下繼續。偶爾會有金色的發條（距離兩倍）、金色的魚（游得慢、容易累）或金色的彈珠（缺口比較寬、滾得順）",
    .en = "Two toys for the knob; the right cell at the top opens the menu, the rule picks the toy. Turn the knob (after a tap on the dial it is the dial's; four seconds "
          "after you stop it is the volume again) or tap the dial's left or right half for one step; on a machine with keys the two keys are one step left and right. "
          "The centre is the release button. Wind-up: turn up to wind the spring, back to ease off; a wound spring slackens by one every second and a half. The spring has a "
          "hidden sweet spot: the closer, the brighter the glow beside the needle and the stronger the haptic. Tap the centre to release the toy: it runs farthest at the sweet "
          "spot and less when wound too little or too far; wind it to the end and it snaps, that release scores zero. Three releases, the total distance counts. Fishing: the fish "
          "swims about and now and then dashes, the arc on the rim is its zone. Keep the needle in the zone: the catch fills and the line relaxes; outside it the line gets "
          "tighter, faster the farther you are, and it snaps when full. A full catch wins; the score is your turns and the time. The left cell changes how strong the glow "
          "is. Style changes the colours. With the screen off or the menu open it pauses; tap to go on. Now and then a golden spring (distance twice), a golden fish "
          "(slower, tires sooner) or a golden marble (wider gaps, rolls smoother) turns up. "
          "The maze (four of the rules): the maze never turns, the force does. The round maze has four ring walls, each with one gap. Gravity maze: the marble starts in the centre, "
          "the knob turns gravity (the small arc on the rim marks where down is); roll it to a gap and let it fall through; the outer exit wins. Repel maze: a constant push outward; "
          "the knob walks the marble round its ring; line up with a gap and it is pushed out. Attract maze: the reverse, from the outer ring in to the centre. Tap to start; a tap "
          "on the left or right half of the field is one detent. The left cell sets the gaps: still, slow drift, fast drift. The score is the time and the knob turns. "
          "A machine with a tilt sensor (the FakePod) also has the tilt maze: a square maze with trap holes; tilt the machine to roll the marble, a hole sends it back to the start, "
          "the bottom right corner wins",
};

static lv_obj_t *s_page, *s_dial, *s_status, *s_low;   /* s_low: the maze's line below its field (UI_SLOT_BASE) */
static bool s_first;                                   /* the first open since power-on has chosen its toy */
static bool s_imu;                                     /* a tilt source exists (imu_svc_present: the FakePod's IMU, the simulator's `simimu`; or the console's `maze grav`): the tilt rule is offered */
static pc_kt_t s_g;
static game_sel_t s_sel;                               /* mode: the glow (0 mid, 1 strong, 2 weak); rule = the toy; style. Kept while the device is on */
static game_style_t s_gold_style;
static game_field_palette_t s_pal;
static bool s_gold, s_paused, s_reveal;                /* s_reveal: the sweet spot of the last release shows on the rim until the next move */
static int s_dial_rule;                                /* the rule the dial on the page was made for + 1 (0: none; a non-zero initial value would be .data) */
static uint32_t s_games, s_rounds;
static char s_stat[64], s_stat2[64];
#define RULES_ALL (PC_KT_RULES + PC_MZ_RULES)           /* the rules: the two dial toys, then the maze rules (pc_game_maze's order) */
static const char *const RULE_ZH[RULES_ALL] = { "上發條", "釣魚", "重力迷宮", "斥力迷宮", "吸力迷宮", "傾斜迷宮" },
                         *const RULE_EN[RULES_ALL] = { "Wind-up", "Fishing", "Gravity", "Repel", "Attract", "Tilt" };
static const int CUE_OF[3] = { 70, 100, 40 };          /* the glow's strength (percent) of the mode */
#define WIN_CM (PC_KT_LAUNCHES * 60)                   /* a total this far is a "win" (the end sound) */
#define LINE_TIGHT 60                                  /* fishing: the line tension (percent) from which the needle shakes */

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}
static bool maze(void) { return s_sel.rule >= PC_KT_RULES; }
static bool wind(void) { return !maze() && s_g.rule == PC_KT_WIND; }
static lv_obj_t *field(void) { return maze() ? kt_maze_field() : s_dial; }   /* where the card goes */

static void show_hub(void)
{
    if (!s_dial) return;
    if (wind()) { ui_dial_hub(s_dial, NULL); return; }
    char b[8];
    snprintf(b, sizeof b, "%d%%", pc_kt_catch_pct(&s_g));
    ui_dial_hub(s_dial, b);
}
/* the glow and the haptic grading: the wind-up's hidden sweet spot, the fish's place; the needle shakes when the line is tight */
static void retarget(void)
{
    if (!s_dial) return;
    const int pct = s_gold ? 100 : CUE_OF[s_sel.mode];
    if (s_g.state == PC_KT_PLAY) ui_dial_target(s_dial, true, wind() ? s_g.sweet : s_g.fish, pc_kt_reach(s_g.rule));
    else ui_dial_target(s_dial, false, 0, 0);
    ui_dial_cue(s_dial, pct);
    ui_dial_shake(s_dial, !wind() && s_g.state == PC_KT_PLAY && s_g.line >= LINE_TIGHT ? (s_g.line - LINE_TIGHT) * 30 / (PC_KT_LINE_FULL - LINE_TIGHT) : 0);
}
/* the rim marks: the wind-up shows where the last release's sweet spot was; fishing the fish's zone (it moves) */
static void zones(void)
{
    if (!s_dial) return;
    if (wind()) {
        if (s_reveal && s_g.launches > 0) ui_dial_zone(s_dial, 0, s_g.prev_sweet - 1, s_g.prev_sweet + 1, s_pal.gold, LV_OPA_COVER);
        else ui_dial_zone_hide(s_dial, 0);
    } else if (s_g.state == PC_KT_PLAY) ui_dial_zone(s_dial, 0, pc_kt_zone_lo(&s_g), pc_kt_zone_hi(&s_g), s_gold ? s_pal.gold : s_pal.power, LV_OPA_COVER);
    else ui_dial_zone_hide(s_dial, 0);
}
static void show_status(void)
{
    char b[sizeof s_stat], b2[sizeof s_stat2] = "";
    if (maze()) {                                         /* two lines round the centred field: progress above, the state (tap to start, paused, the end) below */
        kt_maze_text(b, sizeof b, b2, sizeof b2);
        if (s_paused) snprintf(b2, sizeof b2, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
        ui_frame_hint(s_status, false);
        ui_frame_hint(s_low, s_paused || (kt_maze_game()->state != PC_MZ_DONE && !kt_maze_game()->started));   /* an instruction is popup text, the end line stays in the content */
        if (s_status && strcmp(b, s_stat)) { memcpy(s_stat, b, sizeof s_stat); lv_label_set_text(s_status, s_stat); }
        if (s_low && strcmp(b2, s_stat2)) { memcpy(s_stat2, b2, sizeof s_stat2); lv_label_set_text(s_low, s_stat2); }
        return;
    }
    if (s_low && s_stat2[0]) { s_stat2[0] = 0; lv_label_set_text(s_low, ""); ui_frame_hint(s_low, false); }   /* back to a dial toy: the line below the field goes */
    if (s_paused) snprintf(b, sizeof b, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
    else if (wind()) {
        if (s_g.state == PC_KT_DONE) snprintf(b, sizeof b, T(S_TOTAL_D_CM) /* Ran %d cm in total */, s_g.total);
        else if (s_g.launches == 0) snprintf(b, sizeof b, "%s", T(S_WIND_IT_UP_TAP_THE_CENTRE) /* Turn up to wind, tap the middle to release */);
        else snprintf(b, sizeof b, T(S_D_D_D_CM) /* Release %d/%d  %d cm */, s_g.launches + 1, PC_KT_LAUNCHES, s_g.last_cm);
    } else if (s_g.state == PC_KT_DONE) snprintf(b, sizeof b, "%s", T(S_CAUGHT_IT) /* Caught it! */);
    else if (s_g.state == PC_KT_LOST) snprintf(b, sizeof b, "%s", T(S_LINE_SNAPPED) /* Line snapped */);
    else snprintf(b, sizeof b, T(S_LINE_D) /* Line %d%% */, s_g.line);
    ui_frame_hint(s_status, s_paused || (wind() && !maze() && s_g.state != PC_KT_DONE && s_g.launches == 0)); /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}

static void again(void);
static void apply_style(void);
static char s_title[40];
static void finish(void)
{
    game_end_t e;
    if (maze()) {                                          /* time + knob turns (tilt: the falls in the title, the tries as the moves) */
        const pc_mz_t *m = kt_maze_game();
        if (m->rule == PC_MZ_TILT) snprintf(s_title, sizeof s_title, T(S_DONE_D_FALLS) /* Arrived! %d falls */, m->falls);
        else snprintf(s_title, sizeof s_title, "%s", m->rule == PC_MZ_ATTRACT ? T(S_REACHED_THE_CENTRE) /* Reached the centre! */ : T(S_OUT) /* Out! */);
        e = (game_end_t){ .id = "knobtoy", .variant = s_sel.rule, .outcome = PC_GAME_WIN, .vs_device = false, .moves = m->rule == PC_MZ_TILT ? m->falls + 1 : m->turns, .ms = m->ms,
                          .title = s_title, .gold = s_gold, .style = style() };
    } else if (wind()) e = (game_end_t){ .id = "knobtoy", .variant = s_g.rule, .outcome = s_g.total >= WIN_CM ? PC_GAME_WIN : PC_GAME_LOSS, .vs_device = false, .moves = s_g.total, .ms = s_g.ms,
                                  .mode = GAME_SCORE_HIGH, .unit = T(S_CM) /* cm */, .title = T(S_ALL_RELEASED) /* All three released */, .gold = s_g.gold, .style = style() };
    else e = (game_end_t){ .id = "knobtoy", .variant = s_g.rule, .outcome = s_g.state == PC_KT_DONE ? PC_GAME_WIN : PC_GAME_LOSS, .vs_device = false, .moves = s_g.steps, .ms = s_g.ms,
                           .title = s_g.state == PC_KT_DONE ? T(S_CAUGHT_IT) /* Caught it! */ : T(S_LINE_SNAPPED) /* Line snapped */, .gold = s_g.gold, .style = style() };
    game_result_end(field(), &e, again);
}

/* the call's events: a sound, a haptic, the rim, the end */
static void events(void)
{
    for (int i = 0; i < s_g.n_ev; i++) {
        switch (s_g.ev[i]) {
        case PC_KT_EV_LAUNCH:
            sfx_play("coin");
            ui_haptic_tap();
            ui_dial_flash(s_dial, s_pal.power);
            s_reveal = true;                            /* where the sweet spot was */
            break;
        case PC_KT_EV_PERFECT:
            sfx_play("sparkle");
            ui_haptic_strong();
            ui_dial_wave(s_dial, s_pal.gold);
            break;
        case PC_KT_EV_SNAP:
            sfx_play("buzz");
            ui_haptic_strong();
            ui_dial_flash(s_dial, s_pal.warn);
            break;
        case PC_KT_EV_DASH:
            sfx_play("step");
            ui_haptic_tick();
            break;
        case PC_KT_EV_DONE:
        case PC_KT_EV_CAUGHT:
            ui_haptic_strong();
            ui_dial_wave(s_dial, s_pal.gold);
            retarget();
            finish();
            break;
        case PC_KT_EV_LOST:
            sfx_play("buzz");
            ui_dial_flash(s_dial, s_pal.warn);
            retarget();
            finish();
            break;
        default: break;
        }
    }
    if (ui_dial_value(s_dial) != s_g.v) ui_dial_set(s_dial, s_g.v);
    if (s_g.state == PC_KT_PLAY) retarget();
    zones();
    show_hub();
}

/* ---- the dial's callbacks ---- */
static void changed(int v, int delta, bool knob, void *u)
{
    (void)u; (void)v;
    if (s_g.state != PC_KT_PLAY) {                     /* after the end: a tap on the dial = again; a knob turn does nothing (the card stays, the needle goes back) */
        if (knob) ui_dial_set(s_dial, s_g.v);
        else { ui_haptic_tap(); again(); }
        return;
    }
    s_reveal = false;
    pc_kt_move(&s_g, delta);
    if (wind() && s_g.n_ev == 0) sfx_play("tick");
    events();
    ui_dial_wake(s_dial);
    show_status();
}
static void confirm(int v, void *u)
{
    (void)v; (void)u;
    if (s_g.state != PC_KT_PLAY) { again(); return; }
    if (!wind()) { ui_haptic_tick(); return; }          /* the fishing rod has no release */
    pc_kt_release(&s_g);
    events();
    ui_dial_wake(s_dial);
    show_status();
}
/* every 100 ms of running time: the spring's slack, the fish */
static void step(unsigned ms, void *u)
{
    (void)u;
    pc_kt_tick(&s_g, ms);
    events();
    show_status();
}
static bool running(void *u) { (void)u; return pc_kt_running(&s_g); }
static void paused(bool on, void *u) { (void)u; s_paused = on; show_status(); printf("APP game knobtoy paused=%d\n", on); }
static void dump(void *u)
{
    (void)u;
    printf("APP game knobtoy state=%d rule=%d v=%d sweet=%d launches=%d last=%d total=%d fish=%d catch=%d line=%d inzone=%d steps=%d ms=%u gold=%d paused=%d mode=%d\n",
           s_g.state, s_g.rule, s_g.v, s_g.sweet, s_g.launches, s_g.last_cm, s_g.total, s_g.fish, pc_kt_catch_pct(&s_g), s_g.line, wind() ? 0 : pc_kt_in_zone(&s_g), s_g.steps,
           (unsigned)s_g.ms, s_g.gold, s_paused, s_sel.mode);
}
/* console `dial game ...` (tests and pictures): "set <v>" puts the dial on v (no haptic, no event); "sweet <n>" the wind-up's sweet spot; "fish <n>" the fish's place;
 * "gold" makes the toy golden; "line <n>" the line tension; "catch <n>" the catch (permille) */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int n;
    if (sscanf(arg, "set %d", &n) == 1) s_g.v = n < 0 ? 0 : n > (wind() ? PC_KT_WIND_MAX - 1 : PC_KT_FISH_RANGE - 1) ? (wind() ? PC_KT_WIND_MAX - 1 : PC_KT_FISH_RANGE - 1) : n;
    else if (sscanf(arg, "sweet %d", &n) == 1) s_g.sweet = n;
    else if (sscanf(arg, "fish %d", &n) == 1) s_g.fish = n < 5 ? 5 : n > 94 ? 94 : n;
    else if (sscanf(arg, "line %d", &n) == 1) s_g.line = n < 0 ? 0 : n > PC_KT_LINE_FULL - 1 ? PC_KT_LINE_FULL - 1 : n;
    else if (sscanf(arg, "catch %d", &n) == 1) s_g.catch_ = n < 0 ? 0 : n > PC_KT_CATCH_FULL - 25 ? PC_KT_CATCH_FULL - 25 : n;
    else if (!strcmp(arg, "gold")) { s_g.gold = s_gold = true; apply_style(); }
    else { printf("APP game knobtoy cmd unknown %s\n", arg); return; }
    ui_dial_set(s_dial, s_g.v);
    retarget();
    zones();
    show_hub();
    show_status();
}
static void tick_label(int v, char *buf, int n) { snprintf(buf, (size_t)n, "%d", v); }
static const ui_dial_def_t DIAL_WIND = { .name = "knobtoy", .lo = 0, .hi = PC_KT_WIND_MAX, .wrap = false, .a0 = 135, .span = 270, .major = 10, .minor = 2, .label = tick_label, .cue = true,
                                         .changed = changed, .confirm = confirm, .step = step, .running = running, .paused = paused, .dump = dump, .cmd = cmd };
static const ui_dial_def_t DIAL_FISH = { .name = "knobtoy", .lo = 0, .hi = PC_KT_FISH_RANGE - 1, .wrap = false, .a0 = 135, .span = 270, .major = 10, .minor = 5, .label = tick_label, .cue = true,
                                         .changed = changed, .confirm = confirm, .step = step, .running = running, .paused = paused, .dump = dump, .cmd = cmd };

static void apply_style(void)
{
    const game_style_t *st = style();
    game_style_field(st, &s_pal);
    if (maze()) { kt_maze_style(st, s_gold); return; }
    if (!s_dial) return;
    const ui_dial_colours_t c = { .fill = s_pal.fill, .rim = s_pal.rim, .tick = s_pal.piece, .label = s_pal.piece, .needle = s_pal.player, .hub = s_pal.fill, .peak = s_pal.warn,
                                  .cue = s_gold ? s_pal.gold : s_pal.power };
    ui_dial_colours(s_dial, &c);
    zones();
}
/* the dial of the rule (its scale differs): made again when the rule is not the one on the page; under the HEAD row and the exit (first child). The maze rules have the field
 * (ui_arena) instead, made once and kept over the maze rules; the dial and the field are never both on the page */
static void host_status(void) { show_status(); }
static void host_paused(bool on) { s_paused = on; show_status(); printf("APP game knobtoy paused=%d\n", on); }
static const kt_maze_host_t HOST = { .status = host_status, .finished = finish, .paused = host_paused, .again = again };
static void make_field(void)
{
    if (maze()) {
        if (s_dial) { game_result_hide(); lv_obj_delete(s_dial); s_dial = NULL; s_dial_rule = 0; }
        if (!kt_maze_field()) {
            lv_obj_t *a = kt_maze_create(s_page, &HOST);
            if (a) lv_obj_move_to_index(a, 0);
        }
        return;
    }
    if (kt_maze_field()) { game_result_hide(); lv_obj_delete(kt_maze_field()); }   /* the guard clears the pointer */
    if (s_dial && s_dial_rule == s_sel.rule + 1) return;
    game_result_hide();
    if (s_dial) lv_obj_delete(s_dial);
    s_dial = NULL;
    ui_obj_guard(ui_dial_create(s_page, s_sel.rule == PC_KT_FISH ? &DIAL_FISH : &DIAL_WIND, NULL), &s_dial);
    if (s_dial) lv_obj_move_to_index(s_dial, 0);
    s_dial_rule = s_dial ? s_sel.rule + 1 : 0;
}
/* roll: a new golden decision (重來, again after the end); a rule, a glow and a style keep the one there is */
static void new_game(bool roll)
{
    game_result_hide();
    if (roll) s_gold = game_result_gold("knobtoy", s_rounds++);
    make_field();
    if (maze()) {                                      /* the mode is the gaps' drift: still / slow / fast */
        if (!kt_maze_field()) return;
        s_paused = false;
        const uint32_t seed = games_seed("knobtoy", s_games++);
        const game_style_t *st = style();
        kt_maze_new(s_sel.rule - PC_KT_RULES, s_sel.mode, seed, s_gold, st);
        if (roll && s_gold) sfx_play("gold");
        show_status();
        printf("APP game knobtoy new rule=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
        return;
    }
    if (!s_dial) return;
    pc_kt_init(&s_g, s_sel.rule, games_seed("knobtoy", s_games++), s_gold);
    s_paused = false;
    s_reveal = false;
    ui_dial_unpause(s_dial);
    ui_dial_set(s_dial, s_g.v);
    ui_dial_zones_clear(s_dial);
    ui_dial_shake(s_dial, 0);
    apply_style();
    retarget();
    zones();
    show_hub();
    if (roll && s_gold) sfx_play("gold");
    show_status();
    printf("APP game knobtoy new rule=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); }

static const char *mode_text(int v)
{
    if (maze()) return v == 0 ? T(S_STILL) /* Gap fixed */ : v == 1 ? T(S_DRIFT) /* Gap drifts */ : T(S_FAST_2) /* Gap drifts fast */;
    return v == 0 ? T(S_GLOW_MID) /* Glow medium */ : v == 1 ? T(S_GLOW_HIGH) /* Glow strong */ : T(S_GLOW_LOW) /* Glow weak */;
}
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void ring_rezero(void);
static void changed_ring(int what, int v)
{
    (void)v;
    if (what == GR_RULE) ring_rezero();
    if (what == GR_STYLE) apply_style();
    else if (what == GR_MODE && !maze()) retarget();   /* the glow's strength: the same game (the maze's drift is a new game) */
    else new_game(what == GR_AGAIN);                   /* a rule: a new game, the golden roll kept; 重來: a new roll */
}
/* `games count <n>`: the games started so far = the next game's seed (pictures and tests: the same game whatever ran before) */
static bool ring_cmd(const char *a)
{
    unsigned n;
    if (sscanf(a, "count %u", &n) != 1) return false;
    s_games = n;
    new_game(false);
    return true;
}
static const char *rezero_text(void) { return T(S_RE_ZERO) /* Re-zero */; }
static void rezero_run(void) { kt_maze_rezero(); }
static const game_ring_t RING0 = { .id = "knobtoy", .modes = 3, .mode_text = mode_text, .rules = RULES_ALL, .rule_name = rule_name, .changed = changed_ring, .cmd = ring_cmd, .action_direct = true };

static game_ring_t s_ring;                             /* RING0 with the rule count of this device (filled at create: a mutable struct with an initialiser would be internal .data) */
static void ring_rezero(void)                          /* the ring's 重新歸零 cell exists only in the tilt rule (one tap: it is harmless) */
{
    s_ring.action_text = maze() && s_sel.rule - PC_KT_RULES == PC_MZ_TILT ? rezero_text : NULL;
    s_ring.action_run = rezero_run;
}
static void destroy(void) { imu_svc_zero_clear(); }   /* the neutral pose belongs to the page */
static void exit_pause(void *u) { (void)u; if (maze()) { if (kt_maze_field()) ui_arena_pause(kt_maze_field()); } else ui_dial_pause(s_dial); }
static void exit_resume(void *u, uint32_t ms) { (void)u; (void)ms; if (maze()) { if (kt_maze_field()) ui_arena_resume(kt_maze_field()); } else ui_dial_resume(s_dial); }
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = running, .pause = exit_pause, .resume = exit_resume };   /* the knob (or the keys) and taps both play  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_stat[0] = 0;
    s_stat2[0] = 0;
    s_dial = NULL;
    s_dial_rule = 0;
    s_gold = false;
    s_page = NULL;
    ui_obj_guard(p, &s_page);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    s_low = ui_obj_guard(ui_text(p, UI_SLOT_BASE, NULL), &s_low);
    s_imu = s_imu || imu_svc_present();
    if (!s_first) { s_first = true; s_sel.rule = PC_KT_RULES + (s_imu ? PC_MZ_TILT : PC_MZ_GRAV); }   /* the first toy since power-on (the wind-up toy explains itself badly): the tilt maze where the board has an IMU (FakePod), the gravity maze on the knob otherwise; later the last choice stays */
    s_ring = RING0;
    ring_rezero();
    s_ring.rules = RULES_ALL - (s_imu ? 0 : 1);          /* tilt is the last rule: only with a tilt source */
    game_ring_head(p, &s_ring, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    new_game(false);
}

static bool on_knob(int d) { return kt_maze_field() ? ui_arena_knob(d) : ui_dial_knob(d); }
/* console `maze grav <dx> <dy>` (each -100..100: percent of a full tilt): the tilt vector of the maze (what an IMU feeds later); the first one makes the tilt rule available (the simulator's
 * stand-in for the sensor) */
void knobtoy_maze_cmd(const char *arg)
{
    if (!kt_maze_cmd(arg)) { printf("APP maze usage: maze grav <dx> <dy>\n"); return; }
    s_imu = true;
    if (s_page && s_ring.rules) { s_ring.rules = RULES_ALL; game_ring_refresh(); }
}

const ui_screen_t scr_game_knobtoy = { .name = "tools_game_knobtoy", .create = create, .destroy = destroy, .on_knob = on_knob, .content = true };
