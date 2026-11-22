// SPDX-License-Identifier: LGPL-2.1-or-later
/* Radio hunt (ideas G3): the shared dial (ui_dial) as a frequency dial (a gauge, 80.0 to 99.8 MHz in the hub) with hidden stations; the HEAD row
 * (the glow's strength, the rule which opens the game's ring: 規則 Classic / Timed / Faint, 風格, 重來, 說明), the status line and Back. Rules: pc_game_radio (host-tested).
 * Tune by the knob (a tap on the dial lends it, the borrowed-knob way; FakePod: the keys) or by taps on the dial's left / right half (one step each). Between
 * the stations there is static; near one the static thins out and a melody fragment gets clearer, the glow beside the needle (ui_dial's proximity cue)
 * lights up, the needle shakes in the static and the haptic gets stronger. A tap on the hub locks the station when the signal is strong enough. Sound: only the
 * toy sounds of the bank through sfx_play, one a 100 ms step (the existing path has no continuous noise level; sound and shake start with the first tune and go quiet after 10 s without input: static is a sparse train of "step" noise bursts,
 * denser far from a station and in the weak rule; the fragment is the sector notes, more of them the clearer the signal). The rare golden station (1 in 10)
 * takes 10 s off the time. The end: the result card (tries and time, or the stations of the 60 s). Only composes shared pieces (ui_dial, game_ring,
 * game_result, game_style); state in PSRAM (.bss: this file is built with -msmall-data-limit=0). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_radio.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_dial.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

const ui_game_help_t game_help_radio = {
    .zh = "收音機的頻道上藏著幾個電台，要用旋鈕把它們找出來。轉旋鈕（點一下刻度盤之後旋鈕才歸它，停下四秒後回到音量），或點刻度盤的左半邊、右半邊各調一格；中間的圓顯示頻率。"
          "沒有電台的地方只有雜訊，越靠近電台雜訊越稀，一段旋律越來越清楚，指針旁邊會亮起光暈，手感也越來越強，指針在雜訊裡會抖動。"
          "訊號夠強時點中間的圓就鎖定這個電台；沒有電台就點，算一次失誤。"
          "上方右邊的格子打開選單：規則有三種——經典是找出三個電台，成績看嘗試次數和時間；限時是六十秒內盡量多找，找到一個就出現下一個；微弱是三個電台，鎖定的範圍更窄、雜訊更密。"
          "左邊的格子換光暈的強度。風格換顏色。螢幕關掉或打開選單時計時會暫停，點一下繼續。偶爾會有金色的隱藏電台，訊號很窄，鎖定它會減少十秒",
    .en = "A few stations are hidden on the radio's band; find them with the knob. Turn the knob (after a tap on the dial it is the dial's; four seconds after you "
          "stop it is the volume again) or tap the dial's left or right half to tune one step; the centre shows the frequency. Between stations there is only static; "
          "the closer to a station the thinner the static, a melody gets clearer, a glow lights up beside the needle and the haptic gets stronger; the needle shakes in "
          "the static. When the signal is strong enough, tap the centre to lock the station; a tap with no station is a miss. The right cell at the top opens the menu. "
          "Rules: Classic: find three stations, scored by tries and time; Timed: find as many as you can in sixty seconds, a new one appears after each; Weak: three "
          "stations, a narrower lock window and denser static. The left cell changes how strong the glow is. Style changes the colours. With the screen off or the menu "
          "open the clock pauses; tap to go on. Now and then a golden station hides on the band: a very narrow signal, and locking it takes ten seconds off",
};

static lv_obj_t *s_dial, *s_status;
static pc_radio_t s_g;
static game_sel_t s_sel;                               /* mode: the glow (0 mid, 1 strong, 2 weak); rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static game_field_palette_t s_pal;
static bool s_gold, s_paused;
static uint32_t s_games, s_step, s_sounds, s_idle;   /* s_idle: ms since the last tune or lock */
#define IDLE_MS 10000u                                   /* static, melody and needle shake go quiet after this long without input (the next input resumes them) */
static int s_last_snd;                                 /* the last sound code + 2 (0: none yet; a non-zero initial value would be .data, internal RAM) */
static char s_stat[64];
static const char *const RULE_ZH[PC_RADIO_RULES] = { "經典", "限時", "微弱" }, *const RULE_EN[PC_RADIO_RULES] = { "Classic", "Timed", "Weak" };
static const int CUE_OF[3] = { 70, 100, 40 };          /* the glow's strength (percent) of the mode */
#define NOTE_COUNT 4
static const char *const NOTE[NOTE_COUNT] = { "sector_1", "sector_2", "sector_3", "sector_4" };

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}

static bool active(void) { return s_g.state == PC_RADIO_PLAY && s_g.started && s_idle < IDLE_MS; }   /* the radio sounds and shakes only while played */
static void show_hub(void)
{
    char b[8];
    const int f = pc_radio_hub10(s_g.pos);
    snprintf(b, sizeof b, "%d.%d", f / 10, f % 10);
    ui_dial_hub(s_dial, b);
}
/* the target of the dial's haptics and glow: the nearest station still to find; the needle's shake follows the signal */
static void retarget(void)
{
    const int i = pc_radio_nearest(&s_g);
    ui_dial_target(s_dial, i >= 0, i >= 0 ? s_g.st[i].pos : 0, i >= 0 ? pc_radio_reach(s_g.rule, s_g.st[i].gold) : 0);
    ui_dial_cue(s_dial, s_g.rule == PC_RADIO_WEAK ? CUE_OF[s_sel.mode] * 3 / 4 : CUE_OF[s_sel.mode]);
    ui_dial_shake(s_dial, active() ? pc_radio_shake10(pc_radio_signal(&s_g)) : 0);
}
/* the locked stations as marks on the rim (timed: the stations vanish, nothing stays) */
static void zones(void)
{
    for (int k = 0; k < PC_RADIO_MAX; k++) {
        if (s_g.rule != PC_RADIO_TIMED && k < s_g.n && s_g.st[k].locked) ui_dial_zone(s_dial, k, s_g.st[k].pos - 1, s_g.st[k].pos + 1, s_g.st[k].gold ? s_pal.gold : s_pal.power, LV_OPA_COVER);
        else ui_dial_zone_hide(s_dial, k);
    }
}
static void show_status(void)
{
    char b[sizeof s_stat];
    if (s_paused) snprintf(b, sizeof b, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
    else if (s_g.state == PC_RADIO_WON) snprintf(b, sizeof b, "%s", T(S_ALL_LOCKED) /* All locked! */);
    else if (s_g.state == PC_RADIO_TIMEUP) snprintf(b, sizeof b, "%s", T(S_TIME_IS_UP) /* Time's up */);
    else if (s_g.rule == PC_RADIO_TIMED) snprintf(b, sizeof b, T(S_D_FOUND_DS_SIG_D) /* Found %d  %d s left  signal %d%% */, s_g.found, (int)((pc_radio_left_ms(&s_g) + 999) / 1000), pc_radio_signal(&s_g));
    else snprintf(b, sizeof b, T(S_D_D_SIG_D) /* Found %d/%d  signal %d%% */, s_g.found, s_g.n, pc_radio_signal(&s_g));
    ui_frame_hint(s_status, s_paused); /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}

static void again(void);
static void apply_style(void);
static void finish(void)
{
    const bool win = s_g.state == PC_RADIO_WON, timed = s_g.rule == PC_RADIO_TIMED;
    const game_end_t e = { .id = "radio", .variant = s_g.rule, .outcome = win || (timed && s_g.found >= PC_RADIO_MAX) ? PC_GAME_WIN : PC_GAME_LOSS, .vs_device = false,
                           .moves = timed ? s_g.found : s_g.attempts, .ms = pc_radio_time_ms(&s_g), .mode = timed ? GAME_SCORE_HIGH : GAME_SCORE_MOVES, .unit = T(S_STATIONS_2) /* Radio */,
                           .title = win ? T(S_ALL_LOCKED) /* All locked! */ : T(S_TIME_IS_UP) /* Time's up */, .gold = s_g.gold_found, .style = style() };
    game_result_end(s_dial, &e, again);
}

/* the call's events: a sound, a haptic, the rim, the marks */
static void events(void)
{
    for (int i = 0; i < s_g.n_ev; i++) {
        switch (s_g.ev[i]) {
        case PC_RADIO_EV_LOCK:
            sfx_play("coin");
            ui_dial_flash(s_dial, s_pal.power);
            zones();
            break;
        case PC_RADIO_EV_GOLD:
            sfx_play("gold");
            ui_haptic_strong();
            s_gold = true;                             /* the golden one: the dial goes gold, a ring runs out of the hub */
            apply_style();
            ui_dial_wave(s_dial, s_pal.gold);
            break;
        case PC_RADIO_EV_MISS:
            sfx_play("buzz");
            ui_haptic_strong();
            ui_dial_flash(s_dial, s_pal.warn);
            break;
        case PC_RADIO_EV_WON:
            ui_haptic_strong();
            ui_dial_wave(s_dial, s_pal.gold);
            ui_dial_hub(s_dial, "OK");
            retarget();
            finish();
            break;
        case PC_RADIO_EV_TIMEUP:
            ui_dial_flash(s_dial, s_pal.warn);
            retarget();
            finish();
            break;
        default: break;
        }
    }
    if (s_g.state == PC_RADIO_PLAY) { retarget(); show_hub(); }
}

/* ---- the dial's callbacks ---- */
static void changed(int v, int delta, bool knob, void *u)
{
    (void)u; (void)v;
    if (s_g.state != PC_RADIO_PLAY) {                  /* after the end: a tap on the dial = again; a knob turn does nothing (the card stays, the needle goes back) */
        if (knob) ui_dial_set(s_dial, s_g.pos);
        else { ui_haptic_tap(); again(); }
        return;
    }
    s_idle = 0;
    pc_radio_tune(&s_g, delta);
    events();
    ui_dial_wake(s_dial);
    show_status();
}
static void confirm(int v, void *u)
{
    (void)v; (void)u;
    if (s_g.state != PC_RADIO_PLAY) { again(); return; }
    s_idle = 0;
    pc_radio_lock(&s_g);
    events();
    ui_dial_wake(s_dial);
    show_status();
}
/* every 100 ms of running time: the clock, then the sound of this step (static / a note of the fragment) */
static void step(unsigned ms, void *u)
{
    (void)u;
    const bool was = active();
    s_idle += ms;
    pc_radio_tick(&s_g, ms);
    events();
    if (was && !active() && s_g.state == PC_RADIO_PLAY) retarget();   /* idle: the shake stops */
    if (active()) {
        const int i = pc_radio_nearest(&s_g);
        const int snd = pc_radio_sound(s_g.rule, pc_radio_signal(&s_g), i >= 0 ? s_g.st[i].tune : 0, s_step++);
        s_last_snd = snd + 2;
        if (snd == 0) { sfx_play("step"); s_sounds++; }
        else if (snd > 0) { sfx_play(NOTE[(snd - 1) % NOTE_COUNT]); s_sounds++; }
    }
    show_status();
}
static bool running(void *u) { (void)u; return pc_radio_running(&s_g); }   /* from the first tune or lock (the clock starts with it); quiet again after IDLE_MS without input */
static void paused(bool on, void *u) { (void)u; s_paused = on; show_status(); printf("APP game radio paused=%d\n", on); }
static void dump(void *u)
{
    (void)u;
    printf("APP game radio state=%d rule=%d n=%d found=%d pos=%d st=%d%s,%d%s,%d%s locked=%d%d%d attempts=%d misses=%d ms=%u left=%u signal=%d can=%d near=%d gold=%d bonus=%u snd=%d sounds=%u paused=%d mode=%d\n",
           s_g.state, s_g.rule, s_g.n, s_g.found, s_g.pos, s_g.st[0].pos, s_g.st[0].gold ? "g" : "", s_g.st[1].pos, s_g.st[1].gold ? "g" : "", s_g.st[2].pos, s_g.st[2].gold ? "g" : "",
           s_g.st[0].locked, s_g.st[1].locked, s_g.st[2].locked, s_g.attempts, s_g.misses, (unsigned)s_g.ms, (unsigned)pc_radio_left_ms(&s_g), pc_radio_signal(&s_g), pc_radio_can_lock(&s_g),
           pc_radio_nearest(&s_g), s_g.gold_found, (unsigned)s_g.bonus_ms, s_last_snd - 2, (unsigned)s_sounds, s_paused, s_sel.mode);
}
/* console `dial game ...` (tests and pictures): "pos <n>" puts the dial on n (no haptic, no event); "on <k>" puts it on station k;
 * "gold <k>" makes station k golden (the golden one hides a narrower signal) */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int n;
    if (sscanf(arg, "pos %d", &n) == 1) s_g.pos = n < 0 ? 0 : n > PC_RADIO_RANGE - 1 ? PC_RADIO_RANGE - 1 : n;
    else if (sscanf(arg, "on %d", &n) == 1 && n >= 0 && n < PC_RADIO_MAX) s_g.pos = s_g.st[n].pos;
    else if (sscanf(arg, "gold %d", &n) == 1 && n >= 0 && n < PC_RADIO_MAX) s_g.st[n].gold = true;
    else { printf("APP game radio cmd unknown %s\n", arg); return; }
    ui_dial_set(s_dial, s_g.pos);
    retarget();
    show_hub();
    show_status();
}
static void tick_label(int v, char *buf, int n) { snprintf(buf, (size_t)n, "%d", pc_radio_hub10(v) / 10); }   /* the scale's labels are MHz, as the hub */
static const ui_dial_def_t DIAL = { .name = "radio", .lo = 0, .hi = PC_RADIO_RANGE - 1, .wrap = false, .a0 = 135, .span = 270, .major = 10, .minor = 5, .label = tick_label, .cue = true, .changed = changed,
                                    .confirm = confirm, .step = step, .running = running, .paused = paused, .dump = dump, .cmd = cmd };

static void apply_style(void)
{
    const game_style_t *st = style();
    game_style_field(st, &s_pal);
    const ui_dial_colours_t c = { .fill = s_pal.fill, .rim = s_pal.rim, .tick = s_pal.piece, .label = s_pal.piece, .needle = s_pal.player, .hub = s_pal.fill, .peak = s_pal.warn,
                                  .cue = s_gold ? s_pal.gold : s_pal.power };
    ui_dial_colours(s_dial, &c);
    zones();
}

static void new_game(void)
{
    game_result_hide();
    s_gold = false;
    pc_radio_init(&s_g, s_sel.rule, games_seed("radio", s_games++));
    s_paused = false;
    s_step = 0;
    s_idle = 0;
    ui_dial_unpause(s_dial);
    ui_dial_set(s_dial, 0);
    ui_dial_zones_clear(s_dial);
    apply_style();
    retarget();
    show_hub();
    ui_dial_wake(s_dial);
    show_status();
    printf("APP game radio new rule=%d style=%d mode=%d\n", s_sel.rule, s_sel.style, s_sel.mode);
}
static void again(void) { new_game(); }

static const char *mode_text(int v) { return v == 0 ? T(S_GLOW_MID) /* Glow medium */ : v == 1 ? T(S_GLOW_HIGH) /* Glow strong */ : T(S_GLOW_LOW) /* Glow weak */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed_ring(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else if (what == GR_MODE) retarget();              /* the glow's strength: the same game */
    else new_game();                                   /* a rule, 重來: a new game */
}
/* `games count <n>`: the games started so far = the next stations' seed (pictures and tests: the same band whatever ran before) */
static bool ring_cmd(const char *a)
{
    unsigned n;
    if (sscanf(a, "count %u", &n) != 1) return false;
    s_games = n;
    new_game();
    return true;
}
static const game_ring_t RING = { .id = "radio", .modes = 3, .mode_text = mode_text, .rules = PC_RADIO_RULES, .rule_name = rule_name, .changed = changed_ring, .cmd = ring_cmd };

static void exit_pause(void *u) { (void)u; ui_dial_pause(s_dial); }
static void exit_resume(void *u, uint32_t ms) { (void)u; (void)ms; ui_dial_resume(s_dial); }
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = running, .pause = exit_pause, .resume = exit_resume };   /* G4b: knob and taps tune the dial  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_stat[0] = 0;
    ui_obj_guard(ui_dial_create(p, &DIAL, NULL), &s_dial);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    new_game();
}

const ui_screen_t scr_game_radio = { .name = "tools_game_radio", .create = create, .on_knob = ui_dial_knob, .content = true };
