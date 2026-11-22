// SPDX-License-Identifier: LGPL-2.1-or-later
/* Safe cracker (ideas G2): the shared dial (ui_dial) with a secret combination of 0..99 numbers; the HEAD row (the hint's strength, the rule
 * which opens the game's ring: 規則 Classic / Expert / Timed, 風格, 重來, 說明), the status line and Back. Rules: pc_game_safe (host-tested). Turn the dial by the
 * knob (a tap on the dial lends it, the borrowed-knob way; FakePod: the keys) or by taps on its left / right half (one number each); the haptic tells how near
 * the number is (the closer, the stronger; the distinct click is the number itself) and the number gives a tick; a tap on the hub confirms it. The right number
 * locks (a mark on the rim, a coin), a wrong one resets the whole combination (a red flash). In-field feedback (ui_dial effects): the rim flashes at a lock
 * or a miss, the last number opens the door (a golden ring runs out of the hub, the hub says "open"). Surprise: the rare golden safe (the shared gold roll): a
 * gold dial and twice the hint's reach. The end: the result card (attempts, time, the best of the session per rule). Only composes shared pieces
 * (ui_dial, game_ring, game_result, game_style); state in PSRAM (.bss: this file is built with -msmall-data-limit=0). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_safe.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_dial.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

const ui_game_help_t game_help_safe = {
    .zh = "保險箱的密碼是幾個 0 到 99 的數字，要依序轉到每一個數字再確認。轉旋鈕（點一下刻度盤之後旋鈕才歸它，停下四秒後回到音量），或點刻度盤的左半邊、右半邊各轉一格；"
          "轉到數字附近手感會變強，刻度盤上指針旁也會亮起一片光暈，越近越強，剛好停在數字上是一下明顯的卡答並有滴答聲。點中間的圓確認這個數字：對了就鎖上，邊緣留下記號；"
          "錯了整串密碼從頭來過。全部對了保險箱就打開，結果卡片會顯示嘗試次數和時間。"
          "上方右邊的格子打開選單：規則有三種——經典是三個數字；專家是四個數字而且提示更弱；計時是三個數字，六十秒內要打開。"
          "左邊的格子換提示的強度。風格換顏色。螢幕關掉或打開選單時計時會暫停，點一下繼續。偶爾會出現金色的保險箱，提示的範圍加倍",
    .en = "The safe's code is a few numbers from 0 to 99, to be turned to and confirmed in order. Turn the knob (after a tap on the dial it is the dial's; four "
          "seconds after you stop it is the volume again) or tap the dial's left or right half to turn one number. Near a number the haptic gets stronger and a glow lights up beside the needle, "
          "the closer the stronger; resting exactly on it is a distinct click and a tick. Tap the centre to confirm: the right number locks and leaves a mark "
          "on the rim, a wrong one starts the whole code again. With every number right the safe opens and the result card shows the tries and the time. "
          "The right cell at the top opens the menu. Rules: Classic: three numbers; Expert: four numbers and a weaker hint; Timed: three numbers, open it in sixty "
          "seconds. The left cell changes how strong the hint is. Style changes the colours. With the screen off or the menu open the clock pauses; tap to go "
          "on. Now and then the safe is golden: the hint reaches twice as far",
};

static lv_obj_t *s_dial, *s_status;
static pc_safe_t s_g;
static game_sel_t s_sel;                               /* mode: the hint (0 normal, 1 strong, 2 weak); rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static game_field_palette_t s_pal;
static bool s_gold, s_paused;
static uint32_t s_games, s_rounds;
static char s_stat[64];
static const char *const RULE_ZH[PC_SAFE_RULES] = { "經典", "專家", "計時" }, *const RULE_EN[PC_SAFE_RULES] = { "Classic", "Expert", "Timed" };
static const int HINT_OF[3] = { 1, 0, 2 };             /* the mode's hint setting: pc_safe_reach 0 strong, 1 normal, 2 weak */

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}

static void retarget(void)
{
    ui_dial_target(s_dial, s_g.state == PC_SAFE_PLAY, pc_safe_target(&s_g), pc_safe_reach(s_g.rule, HINT_OF[s_sel.mode], s_gold));
    ui_dial_cue(s_dial, pc_safe_cue_strength(s_g.rule, HINT_OF[s_sel.mode]));   /* the visual hint follows the hint's strength (a weaker hint: a dimmer glow) */
}
/* the locked numbers as marks on the rim */
static void zones(void)
{
    for (int k = 0; k < PC_SAFE_MAX; k++) {
        if (k < s_g.stage || (s_g.state == PC_SAFE_OPEN && k < s_g.n)) ui_dial_zone(s_dial, k, s_g.target[k] - 1, s_g.target[k] + 1, s_g.state == PC_SAFE_OPEN ? s_pal.gold : s_pal.power, LV_OPA_COVER);
        else ui_dial_zone_hide(s_dial, k);
    }
}
static void show_status(void)
{
    char b[sizeof s_stat];
    if (s_paused) snprintf(b, sizeof b, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
    else if (s_g.state == PC_SAFE_OPEN) snprintf(b, sizeof b, "%s", T(S_OPEN) /* Open! */);
    else if (s_g.state == PC_SAFE_TIMEUP) snprintf(b, sizeof b, "%s", T(S_TIME_IS_UP) /* Time's up */);
    else if (s_g.rule == PC_SAFE_TIMED) snprintf(b, sizeof b, T(S_D_D_DS) /* Number %d/%d  %d s */, s_g.stage + 1, s_g.n, (int)((pc_safe_left_ms(&s_g) + 999) / 1000));
    else snprintf(b, sizeof b, T(S_D_D_D_TRIES) /* Number %d/%d  %d tries */, s_g.stage + 1, s_g.n, s_g.attempts);
    ui_frame_hint(s_status, s_paused); /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}

static void again(void);
static void finish(void)
{
    const bool win = s_g.state == PC_SAFE_OPEN;
    const game_end_t e = { .id = "safe", .variant = s_g.rule, .outcome = win ? PC_GAME_WIN : PC_GAME_LOSS, .vs_device = false, .moves = s_g.attempts, .ms = s_g.ms,
                           .title = win ? T(S_CRACKED) /* Cracked! */ : T(S_TIME_IS_UP) /* Time's up */, .gold = s_gold, .style = style() };
    game_result_end(s_dial, &e, again);
    s_gold = false;                                    /* the next game is golden only by a new roll (as the other games) */
}

/* the call's events: a sound, a haptic, the rim, the marks */
static void events(void)
{
    for (int i = 0; i < s_g.n_ev; i++) {
        switch (s_g.ev[i]) {
        case PC_SAFE_EV_DETENT:
            sfx_play("tick");
            if (pc_safe_dist(&s_g) != 0) ui_haptic_strong();   /* turned past it in one go: the click the dial did not give */
            break;
        case PC_SAFE_EV_LOCK:
            sfx_play("coin");
            ui_dial_flash(s_dial, s_pal.power);
            zones();
            retarget();
            break;
        case PC_SAFE_EV_WRONG:
            sfx_play("hit");
            ui_haptic_strong();
            ui_dial_flash(s_dial, s_pal.warn);
            zones();
            retarget();
            break;
        case PC_SAFE_EV_OPEN:
            ui_haptic_strong();
            ui_dial_wave(s_dial, s_pal.gold);          /* the door opens: a ring runs out of the hub, the hub says so, every number is lit */
            ui_dial_hub(s_dial, T(S_OPEN_2) /* Open */);
            zones();
            retarget();
            finish();
            break;
        case PC_SAFE_EV_TIMEUP:
            ui_dial_flash(s_dial, s_pal.warn);
            retarget();
            finish();
            break;
        default: break;
        }
    }
}

/* ---- the dial's callbacks ---- */
static void changed(int v, int delta, bool knob, void *u)
{
    (void)u; (void)v;
    if (s_g.state != PC_SAFE_PLAY) {                  /* after the end: a tap on the dial = again; a knob turn does nothing (the card stays, the needle goes back) */
        if (knob) ui_dial_set(s_dial, s_g.pos);
        else { ui_haptic_tap(); again(); }
        return;
    }
    pc_safe_turn(&s_g, delta);
    events();
    ui_dial_wake(s_dial);
    show_status();
}
static void confirm(int v, void *u)
{
    (void)v; (void)u;
    if (s_g.state != PC_SAFE_PLAY) { again(); return; }
    pc_safe_confirm(&s_g);
    events();
    ui_dial_wake(s_dial);
    show_status();
}
static void step(unsigned ms, void *u)
{
    (void)u;
    pc_safe_tick(&s_g, ms);
    events();
    show_status();
}
static bool running(void *u) { (void)u; return pc_safe_running(&s_g); }
static void paused(bool on, void *u) { (void)u; s_paused = on; show_status(); printf("APP game safe paused=%d\n", on); }
static void dump(void *u)
{
    (void)u;
    printf("APP game safe state=%d rule=%d n=%d stage=%d pos=%d target=%d,%d,%d,%d attempts=%d turns=%d ms=%u left=%u reach=%d hint=%d gold=%d paused=%d\n", s_g.state, s_g.rule, s_g.n,
           s_g.stage, s_g.pos, s_g.target[0], s_g.target[1], s_g.target[2], s_g.n > 3 ? s_g.target[3] : -1, s_g.attempts, s_g.turns, (unsigned)s_g.ms, (unsigned)pc_safe_left_ms(&s_g),
           pc_safe_reach(s_g.rule, HINT_OF[s_sel.mode], s_gold), HINT_OF[s_sel.mode], s_gold, s_paused);
}
/* console `dial game ...` (tests and pictures): "pos <n>" puts the dial on n (no haptic, no event: the state as if it had been turned there) */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int n;
    if (sscanf(arg, "pos %d", &n) == 1) { s_g.pos = ((n % PC_SAFE_RANGE) + PC_SAFE_RANGE) % PC_SAFE_RANGE; ui_dial_set(s_dial, s_g.pos); }
    else { printf("APP game safe cmd unknown %s\n", arg); return; }
    show_status();
}
static const ui_dial_def_t DIAL = { .name = "safe", .lo = 0, .hi = PC_SAFE_RANGE - 1, .wrap = true, .a0 = 270, .span = UI_DIAL_FULL, .major = 10, .minor = 5, .peak = true, .cue = true, .changed = changed,
                                    .confirm = confirm, .step = step, .running = running, .paused = paused, .dump = dump, .cmd = cmd };

static void apply_style(void)
{
    const game_style_t *st = style();
    game_style_field(st, &s_pal);
    const ui_dial_colours_t c = { .fill = s_pal.fill, .rim = s_pal.rim, .tick = s_pal.piece, .label = s_pal.piece, .needle = s_pal.player, .hub = s_pal.fill, .peak = s_pal.warn, .cue = s_pal.power };
    ui_dial_colours(s_dial, &c);
    zones();
}

/* roll: a new golden decision (重來, again after the end); a rule, a hint, a style and opening the page keep the one there is */
static void new_game(bool roll)
{
    game_result_hide();
    if (roll) s_gold = game_result_gold("safe", s_rounds++);
    pc_safe_init(&s_g, s_sel.rule, games_seed("safe", s_games++));
    s_paused = false;
    ui_dial_unpause(s_dial);
    ui_dial_set(s_dial, 0);
    ui_dial_hub(s_dial, NULL);
    ui_dial_zones_clear(s_dial);
    apply_style();
    retarget();
    if (roll && s_gold) sfx_play("gold");
    show_status();
    printf("APP game safe new rule=%d style=%d mode=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); }

static const char *mode_text(int v) { return v == 0 ? T(S_HINT_MID) /* Hint medium */ : v == 1 ? T(S_HINT_HIGH) /* Hint strong */ : T(S_HINT_LOW) /* Hint weak */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed_ring(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else if (what == GR_MODE) retarget();              /* the hint's strength: the same game, a new reach */
    else new_game(what == GR_AGAIN);                   /* a rule, 重來: a new game (only 重來 rolls the golden game) */
}
/* `games count <n>`: the games started so far = the next combination's seed (pictures and tests: the same safe whatever ran before) */
static bool ring_cmd(const char *a)
{
    unsigned n;
    if (sscanf(a, "count %u", &n) != 1) return false;
    s_games = n;
    new_game(false);
    return true;
}
static const game_ring_t RING = { .id = "safe", .modes = 3, .mode_text = mode_text, .rules = PC_SAFE_RULES, .rule_name = rule_name, .changed = changed_ring, .cmd = ring_cmd };

static void exit_pause(void *u) { (void)u; ui_dial_pause(s_dial); }
static void exit_resume(void *u, uint32_t ms) { (void)u; (void)ms; ui_dial_resume(s_dial); }
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = running, .pause = exit_pause, .resume = exit_resume };   /* G4b: knob and taps turn the dial  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_stat[0] = 0;
    ui_obj_guard(ui_dial_create(p, &DIAL, NULL), &s_dial);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    new_game(false);
}

const ui_screen_t scr_game_safe = { .name = "tools_game_safe", .create = create, .on_knob = ui_dial_knob, .content = true };
