// SPDX-License-Identifier: LGPL-2.1-or-later
/* Beat tap (ideas G3): the shared field (ui_arena) with a target circle in the middle; rings run in from the rim and arrive on the beat of the
 * playing file (pc_beat through ui_beat, the spectrum the player already makes), a tap as one arrives is a hit: perfect / good / miss by ms window,
 * the combo grows, a miss resets it. A round is 32 rings; the card says the points; the best is kept (pc_gamesave). Rules and judging: pc_game_beat
 * (host-tested). 規則: Classic (follow the file), Metronome (a fixed BPM 60..120: the knob, the borrowed way, changes it; each arrival ticks), Off-beat (tap between
 * the beats). The left cell is the timing window (Standard / Loose / Strict). No music or no tempo found: the page says so and a tap switches to the metronome.
 * In-field feedback: a hit bursts at the centre and the target circle flashes, a combo shows its number, a golden ring (1 in 16) doubles; a miss flashes
 * the rim. Only composes shared pieces (ui_arena, ui_beat, game_ring, game_result, game_style); state in PSRAM (built with -msmall-data-limit=0). FakePod: its
 * two keys come as knob steps; with no knob they count as taps here. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "pc/pc_game_beat.h"
#include "pc/pc_game_score.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_arena.h"
#include "ui_beat.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_frame.h"

enum { ID_TARGET = 0, ID_RING, ID_DOT = ID_RING + PC_GB_RINGS, ID_N };
#define TURN        (4 * 90)                            /* a ring: a full-circle arc */
#define STEP_MS     (1000 / UI_ARENA_FPS + 1)           /* one fixed step, rounded up */
#define BPM_STEP    5
#define LISTEN_MS   5000                                /* after opening: the detector may still be listening */

const ui_game_help_t game_help_beat = {
    .zh = "圓圈會從外緣往中間縮，剛好縮到中間那個圓的時候點一下：越準分數越高。誤差在五十毫秒內是完美，一百一十毫秒內是不錯，再差就算失誤；"
          "連續命中是連擊，連擊越長每下的分數越高（最多四倍），失誤就歸零；偶爾出現一圈金色的，分數加倍。一局三十二圈，結束時顯示分數與最佳。"
          "規則有三種：經典——圓圈跟著正在播放的歌的拍子（要先播音樂；程式要聽幾秒才找得到拍子，找不到時畫面會告訴你，點一下就改用節拍器）；"
          "節拍器——固定速度，不需要音樂，每一拍會滴答一聲，點一下場地之後轉旋鈕可以在六十到一百二十 BPM 之間調速度（停手四秒回到音量）；"
          "反拍——跟經典一樣，但要點在兩拍之間。上方左邊的格子換判定的寬窄（標準、寬鬆、嚴格），右邊的格子打開選單。螢幕關掉或打開選單時會暫停，點一下繼續",
    .en = "Rings shrink from the rim toward the middle; tap just as one reaches the circle in the centre: the closer, the more points. Within fifty "
          "milliseconds is perfect, within one hundred and ten good, anything further is a miss. Hits in a row are a combo and every hit is worth more "
          "the longer it is (up to four times), a miss resets it; now and then a golden ring is worth double. A round is thirty-two rings, then the "
          "points and your best are shown. Rules: Classic follows the beat of the file that is playing (start the music first; the page listens for a "
          "few seconds to find the beat, and when there is none it says so and a tap switches to the metronome); Metronome is a fixed tempo that needs "
          "no music and ticks on every beat, and after a tap on the field the knob changes it between 60 and 120 BPM (four seconds after you stop it "
          "is the volume again); Off-beat is like Classic but you tap between the beats. The left cell at the top changes how strict the timing is "
          "(Standard, Wide, Tight), the right cell opens the menu. With the screen off or the menu open the game pauses; tap to go on",
};

static lv_obj_t *s_arena, *s_status;
static pc_gb_t s_g;
static game_sel_t s_sel;                                /* mode: the timing window; rule; style. Kept while the device is on */
static game_style_t s_gold_style;
static game_field_palette_t s_pal;
static bool s_gold, s_paused;
static uint32_t s_last_step, s_games, s_rounds, s_t0, s_open, s_steps, s_prev_now;
static char s_stat[64];
static const char *const RULE_ZH[PC_GB_RULES] = { "經典", "節拍器", "反拍" }, *const RULE_EN[PC_GB_RULES] = { "Classic", "Metro", "Off-beat" };

static const game_style_t *style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_gold) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    return st;
}
static int field_r(void) { return ui_arena_radius(s_arena); }
static int target_r(void) { return LV_MAX(6, field_r() * 2 / 9); }

/* ---- the objects: the target circle, the rings coming in, a dot in the middle ---- */
static void sync(void)
{
    const int R = field_r(), Rt = target_r();
    ui_arena_begin(s_arena);
    ui_arena_arc(s_arena, ID_TARGET, Rt - 1, Rt + 2, 0, TURN, false, s_pal.player, LV_OPA_COVER);
    for (int i = 0; i < PC_GB_RINGS; i++) {
        float p = pc_gb_progress(&s_g, i);
        if (p < 0) { ui_arena_hide(s_arena, ID_RING + i); continue; }
        float q = p < 0 ? 0 : p > 1 ? 1 : p;
        int r = Rt + (int)lroundf((float)(R - 4 - Rt) * (1.0f - p));
        r = LV_MAX(r, 3);
        int t = 2 + (int)(3.0f * q);
        ui_arena_arc(s_arena, ID_RING + i, LV_MAX(r - t / 2, 1), r + t - t / 2, 0, TURN, false, s_g.ring[i].gold ? s_pal.gold : s_pal.piece, (lv_opa_t)(90 + (int)(165.0f * q)));
    }
    ui_arena_ball(s_arena, ID_DOT, 0, 0, LV_MAX(2, Rt / 6), s_pal.ball, LV_OPA_COVER);
    ui_arena_end(s_arena);
}

static bool listening(void) { return lv_tick_elaps(s_open) < LISTEN_MS; }
static void show_status(void)
{
    char b[sizeof s_stat];
    ui_beat_info_t bi;
    ui_beat_info(&bi);
    if (s_paused) snprintf(b, sizeof b, "%s", T(S_PAUSED_TAP_TO_GO_ON) /* Paused: tap to resume */);
    else if (s_g.state == PC_GB_OVER) snprintf(b, sizeof b, "%s", T(S_DONE_3) /* End */);
    else if (s_g.state == PC_GB_READY) {
        if (s_g.rule == PC_GB_METRO) snprintf(b, sizeof b, T(S_TAP_TO_START_D_BPM) /* Tap to start %d BPM */, s_g.bpm);
        else if (bi.locked) snprintf(b, sizeof b, T(S_TAP_TO_START_D_BPM) /* Tap to start %d BPM */, (int)lroundf(bi.bpm));
        else if (listening()) snprintf(b, sizeof b, "%s", T(S_LISTENING_2) /* Listening for the beat... */);
        else snprintf(b, sizeof b, "%s", T(S_NO_BEAT_TAP_FOR_METRONOME) /* No beat: tap to use the metronome */);
    }
    else if (pc_gb_no_beat(&s_g)) snprintf(b, sizeof b, "%s", T(S_NO_BEAT_TAP_FOR_METRONOME) /* No beat: tap to use the metronome */);
    else if (s_g.rule == PC_GB_METRO) snprintf(b, sizeof b, T(S_X_D_U_DBPM) /* %d combo %u pts %dBPM */, s_g.combo, (unsigned)s_g.points, s_g.bpm);
    else snprintf(b, sizeof b, T(S_X_D_U_PTS) /* %d combo %u pts */, s_g.combo, (unsigned)s_g.points);
    ui_frame_hint(s_status, s_paused || s_g.state == PC_GB_READY || pc_gb_no_beat(&s_g));                    /* GS: an instruction is popup text, the score stays in the content */
    if (!s_status || !strcmp(b, s_stat)) return;        /* the label only when its text changed */
    memcpy(s_stat, b, sizeof s_stat);
    lv_label_set_text(s_status, s_stat);
}
/* the detector's FFT only while it is wanted: the rules that follow the file (not the metronome), not paused, the screen on */
static void update_feed(void) { ui_beat_hold(s_g.rule == PC_GB_METRO || s_paused || !ui_screen_on()); }
static void status_cb(lv_timer_t *t) { (void)t; update_feed(); show_status(); }      /* READY: the lock and the listening time change without a step */

static void again(void);
static void finish(void)
{
    const game_end_t e = { .id = "beat", .variant = s_g.rule, .outcome = PC_GAME_LOSS, .vs_device = false, .moves = s_g.perfect + s_g.good, .ms = lv_tick_elaps(s_t0),
                           .title = T(S_ROUND_OVER) /* Round over */, .gold = s_gold, .mode = GAME_SCORE_POINTS, .points = s_g.points, .level = 0, .style = style() };
    ui_haptic_strong();
    game_result_end(s_arena, &e, again);
    s_gold = false;                                    /* the next game is golden only by a new roll */
}

/* the step's events: effects, one sound (the most important), graded haptics */
static void events(void)
{
    static const char *const SND[] = { [PC_GB_EV_START] = "move", [PC_GB_EV_PERFECT] = "coin", [PC_GB_EV_GOOD] = "tap", [PC_GB_EV_MISS] = "hit", [PC_GB_EV_STRAY] = "tick",
                                       [PC_GB_EV_COMBO] = "combo" };
    static const uint8_t RANK[] = { [PC_GB_EV_START] = 1, [PC_GB_EV_STRAY] = 2, [PC_GB_EV_GOOD] = 3, [PC_GB_EV_PERFECT] = 4, [PC_GB_EV_MISS] = 5, [PC_GB_EV_COMBO] = 6 };
    const int Rt = target_r();
    int top = 0, haptic = 0;
    bool over = false;
    const char *gold_sound = NULL;
    for (int i = 0; i < s_g.n_ev; i++) {
        const pc_gb_ev_t *v = &s_g.ev[i];
        char t[12];
        switch (v->kind) {
        case PC_GB_EV_PERFECT:
            ui_arena_burst(s_arena, 0, -Rt, v->gold ? s_pal.gold : s_pal.fx, 6);
            snprintf(t, sizeof t, "+%u", (unsigned)v->points);
            ui_arena_text(s_arena, 0, Rt + 10, t, v->gold ? s_pal.gold : s_pal.fx);
            haptic = LV_MAX(haptic, 2);
            if (v->gold) gold_sound = "power_up";
            break;
        case PC_GB_EV_GOOD:
            ui_arena_burst(s_arena, 0, -Rt, v->gold ? s_pal.gold : s_pal.power, 3);
            snprintf(t, sizeof t, "+%u", (unsigned)v->points);
            ui_arena_text(s_arena, 0, Rt + 10, t, v->gold ? s_pal.gold : s_pal.power);
            haptic = LV_MAX(haptic, 1);
            if (v->gold) gold_sound = "power_up";
            break;
        case PC_GB_EV_COMBO: snprintf(t, sizeof t, "x%d", v->value); ui_arena_text(s_arena, 0, -Rt - 14, t, s_pal.power); haptic = 3; break;
        case PC_GB_EV_MISS: ui_arena_flash(s_arena, s_pal.warn); haptic = LV_MAX(haptic, 2); break;
        case PC_GB_EV_STRAY: ui_arena_flash(s_arena, s_pal.warn); haptic = LV_MAX(haptic, 1); break;
        case PC_GB_EV_OVER: over = true; break;
        default: break;
        }
        if (v->kind < (int)sizeof RANK && RANK[v->kind] > RANK[top]) top = v->kind;
    }
    if (over) { finish(); return; }                    /* the end sound is the result card's */
    if (gold_sound) sfx_play(gold_sound);
    else if (top && top < (int)(sizeof SND / sizeof SND[0]) && SND[top]) sfx_play(SND[top]);
    if (haptic == 3) ui_haptic_strong(); else if (haptic == 2) ui_haptic_tap(); else if (haptic == 1) ui_haptic_tick();
}

/* ---- the field's callbacks ---- */
static void step(void *u)
{
    (void)u;
    s_steps++;
    s_last_step = lv_tick_get();
    uint32_t dt = (uint32_t)((uint64_t)s_steps * 1000u / UI_ARENA_FPS) - (uint32_t)((uint64_t)(s_steps - 1) * 1000u / UI_ARENA_FPS);
    ui_beat_info_t bi = { 0 };
    if (s_g.rule != PC_GB_METRO) ui_beat_info(&bi);
    s_prev_now = s_g.now;
    pc_gb_step(&s_g, dt, bi.locked, bi.to_next_ms, bi.period_ms);
    if (s_g.rule == PC_GB_METRO && s_g.state == PC_GB_PLAY && !s_g.n_ev)                 /* the metronome ticks as a ring arrives */
        for (int i = 0; i < PC_GB_RINGS; i++)
            if (s_g.ring[i].live && s_g.ring[i].target > s_prev_now && s_g.ring[i].target <= s_g.now) { sfx_play("tick"); break; }
    events();
    sync();
    show_status();
}
static bool running(void *u) { (void)u; return pc_gb_running(&s_g); }
static void paused(bool on, void *u) { (void)u; s_paused = on; update_feed(); show_status(); printf("APP game beat paused=%d\n", on); }
static void new_game(bool roll);
static void input(ui_arena_in_t kind, int v, int x, int y, void *u)
{
    (void)u; (void)x; (void)y;
    if (s_g.state == PC_GB_OVER) { if (kind == UI_ARENA_TAP) { ui_haptic_tap(); again(); } return; }
    if (kind == UI_ARENA_STEP && ui_arena_has_knob()) {        /* the knob: the metronome's tempo */
        if (s_g.rule == PC_GB_METRO) { pc_gb_set_bpm(&s_g, s_g.bpm + v * BPM_STEP); show_status(); }
        return;
    }
    /* a tap (or, with no knob, a key) */
    if (s_g.rule != PC_GB_METRO && !s_paused) {
        ui_beat_info_t bi;
        ui_beat_info(&bi);
        const bool lost = s_g.state == PC_GB_READY ? (!bi.locked && !listening()) : pc_gb_no_beat(&s_g);
        if (lost) { game_ring_set(GR_RULE, PC_GB_METRO); return; }               /* the fallback: the metronome (a new game; the next tap starts) */
    }
    const bool start = s_g.state == PC_GB_READY;
    uint32_t since = lv_tick_elaps(s_last_step);                                 /* the tap's time: the last step's clock plus the time since it (at most one step) */
    pc_gb_tap(&s_g, since > STEP_MS ? STEP_MS : since);
    if (start) { ui_haptic_tap(); ui_arena_wake(s_arena); }
    events();
    sync();
    show_status();
}
static void dump(void *u)
{
    (void)u;
    ui_beat_info_t bi;
    ui_beat_info(&bi);
    printf("APP game beat state=%d rule=%d window=%d bpm=%d combo=%d combo_best=%d points=%u perfect=%d good=%d miss=%d stray=%d spawned=%d resolved=%d golds=%d locked=%d beat_bpm=%.0f nobeat=%d gold=%d paused=%d\n",
           s_g.state, s_g.rule, s_g.window, s_g.bpm, s_g.combo, s_g.combo_best, (unsigned)s_g.points, s_g.perfect, s_g.good, s_g.miss, s_g.stray, s_g.spawned, s_g.resolved, s_g.golds,
           bi.locked, bi.bpm, pc_gb_no_beat(&s_g), s_gold, s_paused);
}
/* console `arena game ...` (tests): "bpm <n>" sets the metronome's tempo */
static void cmd(const char *arg, void *u)
{
    (void)u;
    int n;
    if (sscanf(arg, "bpm %d", &n) == 1) pc_gb_set_bpm(&s_g, n);
    else { printf("APP game beat cmd unknown %s\n", arg); return; }
    show_status();
}
static const ui_arena_def_t ARENA = { .name = "beat", .objs = ID_N, .step = step, .running = running, .input = input, .paused = paused, .dump = dump, .cmd = cmd };

static void apply_style(void)
{
    game_style_field(style(), &s_pal);
    ui_arena_field(s_arena, s_pal.fill, LV_OPA_COVER, s_pal.rim, 0, 0, s_pal.track);
    sync();
}

/* roll: a new golden decision (重來, again after the end); a rule, a window, a style and opening the page keep the one there is */
static void new_game(bool roll)
{
    game_result_hide();
    if (roll) s_gold = game_result_gold("beat", s_rounds++);
    const int bpm = s_g.bpm ? s_g.bpm : PC_GB_BPM_DEF;                           /* the metronome's tempo survives a new game */
    pc_gb_init(&s_g, s_sel.rule, s_sel.mode, games_seed("beat", s_games++));
    pc_gb_set_bpm(&s_g, bpm);
    s_paused = false;
    s_steps = 0;
    s_last_step = lv_tick_get();
    ui_arena_unpause(s_arena);
    ui_arena_clear(s_arena);
    apply_style();
    if (roll && s_gold) sfx_play("gold");
    s_t0 = lv_tick_get();
    update_feed();
    show_status();
    printf("APP game beat new rule=%d style=%d window=%d gold=%d\n", s_sel.rule, s_sel.style, s_sel.mode, s_gold);
}
static void again(void) { new_game(true); }

static const char *mode_text(int v) { return v == 0 ? T(S_STANDARD) /* Standard */ : v == 1 ? T(S_WIDE) /* Loose */ : T(S_TIGHT) /* Strict */; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else new_game(what == GR_AGAIN);
}
static const game_ring_t RING = { .id = "beat", .modes = PC_GB_WINDOWS, .mode_text = mode_text, .rules = PC_GB_RULES, .rule_name = rule_name, .changed = changed };

static void exit_pause(void *u) { (void)u; ui_arena_pause(s_arena); }
static void exit_resume(void *u, uint32_t ms) { (void)u; s_t0 += ms; s_last_step += ms; ui_arena_resume(s_arena); }
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = running, .pause = exit_pause, .resume = exit_resume };   /* G4b: taps are the game  (CF2: popup after a free knob turn) */
static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_stat[0] = 0;
    ui_obj_guard(ui_arena_create(p, &ARENA, NULL), &s_arena);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    s_g.bpm = 0;                                       /* a new visit: the default tempo */
    s_open = lv_tick_get();
    if (s_arena) { ui_beat_on(s_arena); ui_timer_bind(s_arena, status_cb, 500, NULL); }
    new_game(false);
}

const ui_screen_t scr_game_beat = { .name = "tools_game_beat", .create = create, .on_knob = ui_arena_knob, .content = true };
