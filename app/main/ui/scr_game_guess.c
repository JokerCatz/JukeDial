// SPDX-License-Identifier: LGPL-2.1-or-later
/* Guess the song (ideas wave 1, G4): a clip of a song from the library plays (2 or 5 s, from somewhere inside the file), four sectors (ui_sectors) show
 * candidate titles (artists in the Artist rule), a tap on the right one scores; the hub replays the clip. The HEAD row: the number of questions (5 / 10 / 20)
 * and the rule (opens the game's ring: 規則 Classic / Short / Artist, 風格, 重來, 說明). Rules, combo and score: pc_game_guess (host-tested).
 * AUDIO: the clips go through the player's own start (mp3_ctl_play_clip: the same file reader, backend, ReplayGain and owner as a user's play, as a restart: no history,
 * no recent list, no play counted); nothing else touches audio. At the first clip the page notes what the player was doing (the song, its byte position, paused);
 * when the game ends, 重來 starts a new one, or Back leaves, that song is started again at that byte (paused again if it was paused) or the player is stopped if it was
 * idle. Another source (radio, chiptune, noise...) playing: the page says so and does not start (it would have to stop it for good).
 * Only composes shared pieces (ui_sectors, game_result, game_ring, game_style); state in PSRAM (.bss: this file is built with -msmall-data-limit=0). */
#include <stdio.h>
#include <string.h>
#include "audio_svc.h"
#include "font.h"
#include "game_result.h"
#include "game_ring.h"
#include "game_style.h"
#include "games.h"
#include "gamesave_svc.h"
#include "library_svc.h"
#include "mp3_ctl.h"
#include "pc/pc_game_guess.h"
#include "pc/pc_game_score.h"
#include "player_ctl.h"
#include "screens.h"
#include "sfx.h"
#include "ui.h"
#include "ui_exit.h"
#include "ui_kit.h"
#include "ui_sectors.h"
#include "ui_frame.h"

#define REVEAL_MS 1400                        /* the answer shows this long, then the next question (or the card) */
#define TAP_MS    220

const ui_game_help_t game_help_guess = {
    .zh = "先放一小段曲庫裡的歌，下面四個扇形是候選答案，點中正確的那一個。點中間可以重聽。連續答對有連擊倍率（連對 3 題 ×2、5 題 ×3、7 題以上 ×4），"
          "偶爾出現金色題，分數加倍。一局固定題數（上方左邊的格子選 5、10 或 20 題），結果卡寫總分，最佳分數與最長連對會記下來。"
          "右邊的格子打開選單：規則——經典（5 秒、猜歌名）、短（只有 2 秒）、藝人（猜演出者）；風格換顏色；重來馬上重新開始。"
          "曲庫要有至少 4 首標題不同的歌（藝人規則要 4 位不同的演出者）。玩的時候正在播的歌會暫停，離開或結束後從原來的位置接著播。"
          "如果正在播的是電台、晶片音樂或白噪音，要先停掉才能玩",
    .en = "A short clip of a song from your library plays; the four sectors are the candidate answers, tap the right one. Tap the middle to hear it again. "
          "Right answers in a row build a combo (3 in a row x2, 5 x3, 7 or more x4); now and then a question is golden and scores double. A game has a fixed number "
          "of questions (the left cell at the top: 5, 10 or 20); the result card shows the score, and the best score and the longest streak are remembered. "
          "The right cell opens the menu: Rules - Classic (5 s, guess the title), Short (only 2 s), Artist (guess who plays); Style changes the colours; Restart "
          "starts again. The library needs at least 4 songs with different titles (4 different artists for the Artist rule). The song that was playing is paused "
          "while you play and carries on from where it was when you leave or the game ends. If radio, chiptune or noise is playing, stop it first",
};

enum { P_READY = 0, P_CLIP, P_ASK, P_REVEAL, P_OVER, P_BLOCKED };
enum { Q_N = 3 };
static const int QUESTIONS[Q_N] = { 5, 10, 20 };
static const char *const TONES[PC_GUESS_CHOICES] = { "sector_1", "sector_2", "sector_3", "sector_4" };
static const char *const RULE_ZH[PC_GUESS_RULES] = { "經典", "短", "藝人" }, *const RULE_EN[PC_GUESS_RULES] = { "Classic", "Short", "Artist" };

static lv_obj_t *s_sec, *s_status;
static lv_timer_t *s_timer;                  /* the clip's end, the answer's display */
static pc_guess_t s_g;
static game_sel_t s_sel;
static game_style_t s_gold_style;
static const game_style_t *s_style;
static uint8_t s_phase;
static bool s_over_taps, s_gold_hit;         /* the card is up: a tap on the field = again / a golden question was answered right */
static int8_t s_force_gold;                  /* console `games golden 1|0|auto`: 0 auto, 1 golden, 2 never (zero = .bss, no initialised static) */
static uint32_t s_games, s_t0, s_clip_t0;   /* games started / the game's start / the clip's start (lv_tick ms) */
static uint8_t s_fails;                      /* clips in a row that would not play */
/* what the player was doing before the first clip */
static bool s_taken, s_had, s_paused;
static char s_path[300];
static uint32_t s_byte;
static const pc_track_t *s_lib_tracks;       /* the library the game started on: a rescan that swapped it ends the game */
static int s_lib_n;
static const char *s_lib_pool;

static const char *key_cb(void *u, int i)
{
    (void)u;
    const pc_track_t *t = &g_lib.tracks[i];
    return pc_str(&g_lib, s_g.rule == PC_GUESS_ARTIST ? t->artist : t->title);
}

static void status(const char *t) { lv_label_set_text(s_status, t); ui_frame_hint(s_status, false); }
static void status_hint(const char *t) { lv_label_set_text(s_status, t); ui_frame_hint(s_status, true); } /* GS: an instruction is popup text, the score stays in the content */

static void apply_style(void)
{
    const game_style_t *st = game_style(s_sel.style);
    if (s_phase >= P_CLIP && s_phase <= P_REVEAL && s_g.golden) { game_style_gold(st, &s_gold_style); st = &s_gold_style; }
    s_style = st;
    ui_sectors_set_style(s_sec, st);
}

static void schedule(uint32_t ms)
{
    if (!s_timer) return;
    lv_timer_set_period(s_timer, ms);
    lv_timer_reset(s_timer);
    lv_timer_resume(s_timer);
}

/* ---- the player: note, clip, put back ---- */
static void note_player(void)
{
    if (s_taken) return;
    s_taken = true;
    s_had = mp3_ctl_current() >= 0;
    if (!s_had) return;
    aud_status_t st;
    audio_svc_status(&st);
    s_byte = st.pos_byte;
    s_paused = st.state == AUD_PAUSED || audio_svc_paused();
    snprintf(s_path, sizeof s_path, "%s", pc_str(&g_lib, g_lib.tracks[mp3_ctl_current()].path));
    printf("APP game guess note idx=%d byte=%lu paused=%d\n", mp3_ctl_current(), (unsigned long)s_byte, s_paused);
}

static void stop_clip(void) { if (mp3_ctl_current() >= 0) mp3_ctl_stop(); }

static void restore_player(void)
{
    if (!s_taken) return;
    s_taken = false;
    if (s_had) {
        int i = pc_library_find(&g_lib, s_path);
        if (i >= 0 && mp3_ctl_play_back(i, s_byte)) { if (s_paused) audio_svc_pause(true); }
        else stop_clip();
        printf("APP game guess restore idx=%d byte=%lu paused=%d\n", i, (unsigned long)s_byte, s_paused);
    } else {
        stop_clip();
        printf("APP game guess restore idle\n");
    }
}

static bool play_clip(void)
{
    bool ok = mp3_ctl_play_clip(s_g.target, s_g.clip_pct);   /* the player converts the percentage to its backend's start (byte, FLAC frame, or the start) */
    printf("APP game guess clip idx=%d pct=%d ms=%lu ok=%d\n", s_g.target, s_g.clip_pct, (unsigned long)pc_guess_clip_ms(s_g.rule), ok);
    s_clip_t0 = lv_tick_get();
    return ok;
}

/* why the game cannot start now (NULL: it can) */
static const char *blocked(void)
{
    const now_ops_t *n = player_ctl_now();
    if (n && n->src != NOW_SRC_MP3) return T(S_STOP_WHAT_IS_PLAYING_FIRST) /* Stop the current source first */;
    if (pc_guess_ready(g_lib.n, key_cb, NULL) < PC_GUESS_CHOICES)
        return s_g.rule == PC_GUESS_ARTIST ? T(S_NEEDS_SONGS_BY_AT_LEAST_4_ARTISTS) /* The library needs songs by at least 4 different artists */
                                           : T(S_NEEDS_AT_LEAST_4_SONGS_WITH_DIFFERENT) /* The library needs at least 4 songs with different titles */;
    return NULL;
}

/* ---- a question ---- */
static void hub_say(const char *t) { ui_sectors_set_hub(s_sec, t); }

static void show_status(void)
{
    static char b[64];
    int m = pc_guess_mult(s_g.streak + 1);
    if (s_g.golden) snprintf(b, sizeof b, T(S_Q_D_D_GOLDEN_X2) /* Q %d/%d  golden question x2 */, s_g.asked, s_g.rounds);
    else if (m > 1) snprintf(b, sizeof b, T(S_Q_D_D_STREAK_D_X_D) /* Q %d/%d  streak %d  x%d */, s_g.asked, s_g.rounds, s_g.streak, m);
    else snprintf(b, sizeof b, T(S_QUESTION_D_OF_D) /* Q %d/%d */, s_g.asked, s_g.rounds);
    status(b);
}

static void to_ready(const char *why)
{
    s_phase = why ? P_BLOCKED : P_READY;
    ui_sectors_set_enabled(s_sec, false);
    hub_say(why ? "?" : T(S_START) /* Start */);
    if (why) status(why); else status_hint(T(S_TAP_TO_START_3) /* Tap the middle to start */);
    if (why) printf("APP game guess blocked reason=%s\n", why);
}

static void new_game(void);
/* a rescan swapped the library under a game in progress: the track numbers of the question are the old library's (they could point past the new one's end).
 * Every entry point checks first; the game ends (the player's song is put back) and the page waits for a new start. */
static bool lib_lost(void)
{
    if (!s_taken || (s_lib_tracks == g_lib.tracks && s_lib_n == g_lib.n && s_lib_pool == g_lib.pool)) return false;
    printf("APP game guess library changed\n");
    new_game();
    status_hint(T(S_THE_LIBRARY_CHANGED_TAP_TO_RESTART) /* The library was updated, tap the middle to restart */);
    return true;
}

static void begin_round(void)
{
    if (s_fails >= 4) { s_fails = 0; restore_player(); to_ready(T(S_THESE_SONGS_WILL_NOT_PLAY) /* These songs cannot be played */); return; }
    for (int tries = 0; tries < 4; tries++) {
        if (!pc_guess_ask(&s_g, g_lib.n, key_cb, NULL)) { restore_player(); to_ready(blocked()); return; }
        if (s_force_gold) s_g.golden = s_force_gold == 1;
        note_player();
        if (play_clip()) goto playing;
        pc_guess_skip(&s_g);
    }
    restore_player();
    to_ready(T(S_THESE_SONGS_WILL_NOT_PLAY) /* These songs cannot be played */);
    return;
playing:;
    char lab[PC_GUESS_LABEL], all[4 * PC_GUESS_LABEL + 4] = "";
    for (int c = 0; c < PC_GUESS_CHOICES; c++) {
        pc_guess_label(lab, key_cb(NULL, s_g.track[c]));
        ui_sectors_set_text(s_sec, c, lab);
        strncat(all, c ? "|" : "", 2);
        strncat(all, lab, sizeof all - strlen(all) - 1);
    }
    printf("APP game guess ask n=%d answer=%d target=%d golden=%d labels=%s\n", s_g.asked, s_g.answer, s_g.target, s_g.golden, all);
    s_phase = P_CLIP;
    apply_style();
    ui_sectors_unlight(s_sec);
    ui_sectors_set_looks(s_sec, UI_SEC_NORMAL);
    ui_sectors_set_enabled(s_sec, true);
    hub_say(T(S_REPLAY) /* Replay */);
    show_status();
    if (s_g.golden) sfx_play("sparkle");
    schedule(100);                                                /* the clip is watched every 100 ms: its end, or a failure the player reports */
}

static void again(void);
static void finish(void)
{
    restore_player();
    s_phase = P_OVER;
    s_over_taps = false;
    ui_sectors_set_enabled(s_sec, false);
    schedule(GAME_RESULT_DELAY_MS);
    ui_haptic_strong();
    char key[20];
    snprintf(key, sizeof key, "guess.bs%d", s_g.rule);
    uint32_t best = gamesave_get_or(key, 0);
    if (s_g.best_streak > best) { best = s_g.best_streak; gamesave_set(key, best); }
    static char title[48], st[64];
    snprintf(title, sizeof title, T(S_RIGHT_D_D_STREAK_D) /* Correct %d/%d  streak %d */, s_g.correct, s_g.rounds, s_g.best_streak);
    snprintf(st, sizeof st, T(S_BEST_STREAK_U) /* Best streak %u */, (unsigned)best);
    status(st);
    hub_say(T(S_END) /* End */);
    printf("APP game guess over correct=%d rounds=%d points=%lu streak=%d best_streak=%u\n", s_g.correct, s_g.rounds, (unsigned long)s_g.points, s_g.best_streak, (unsigned)best);
    const game_end_t e = { .id = "guess", .variant = s_sel.mode == 1 ? s_g.rule : -1, .outcome = PC_GAME_WIN, .vs_device = false, .moves = (int)s_g.points,
                           .ms = lv_tick_elaps(s_t0), .title = title, .gold = s_gold_hit, .mode = GAME_SCORE_HIGH, .unit = T(S_SCORE) /* Score */, .style = s_style };
    game_result_end(s_sec, &e, again);
}

static void timer_cb(lv_timer_t *t)
{
    if (lib_lost()) { lv_timer_pause(t); return; }
    if (s_phase == P_OVER) { lv_timer_pause(t); s_over_taps = true; ui_sectors_set_enabled(s_sec, true); return; }
    if (s_phase == P_CLIP) {
        int st = mp3_ctl_clip_state();
        if (st == MP3_CLIP_FAILED) {                              /* it would not open or decode: this question is taken back, another track is asked */
            lv_timer_pause(t);
            s_fails++;
            stop_clip();
            pc_guess_skip(&s_g);
            printf("APP game guess clip failed fails=%d\n", s_fails);
            begin_round();
            return;
        }
        if (st == MP3_CLIP_PLAYING && lv_tick_elaps(s_clip_t0) < pc_guess_clip_ms(s_g.rule)) return;   /* still playing: look again in 100 ms */
        lv_timer_pause(t);                                        /* the clip is over (its time, or the song ended inside it): silence, the player may replay it */
        stop_clip();
        s_fails = 0;
        s_phase = P_ASK;
        return;
    }
    if (s_phase == P_REVEAL) {
        lv_timer_pause(t);
        if (s_g.over) finish(); else begin_round();
        return;
    }
    lv_timer_pause(t);
}

static void answered(int i)
{
    stop_clip();
    int right = s_g.answer;
    int streak_before = s_g.streak;
    int r = pc_guess_answer(&s_g, i);
    if (r == PC_GUESS_IGNORED) return;
    s_phase = P_REVEAL;
    ui_sectors_set_enabled(s_sec, false);
    ui_sectors_set_looks(s_sec, UI_SEC_DIM);
    ui_sectors_set_look(s_sec, right, UI_SEC_NORMAL);
    ui_sectors_flash(s_sec, right, REVEAL_MS - 200, false);
    ui_sectors_set_progress(s_sec, s_g.asked, s_g.rounds);
    static char b[96], h[12];
    if (r == PC_GUESS_RIGHT) {
        uint32_t pts = pc_guess_points(s_g.streak, s_g.golden);
        if (s_g.golden) s_gold_hit = true;
        sfx_play(s_g.streak >= 3 && s_g.streak % 2 == 1 ? "combo" : "coin");
        ui_haptic_tap();
        snprintf(b, sizeof b, T(S_RIGHT_U_STREAK_D) /* Correct! +%u  streak %d */, (unsigned)pts, s_g.streak);
    } else {
        char lab[PC_GUESS_LABEL];
        pc_guess_label(lab, key_cb(NULL, s_g.target));
        sfx_play("error");
        ui_haptic_tick();
        snprintf(b, sizeof b, T(S_WRONG_S) /* Wrong, it was %s */, lab);
        (void)streak_before;
    }
    status(b);
    snprintf(h, sizeof h, "%lu", (unsigned long)s_g.points);
    hub_say(h);
    printf("APP game guess answer=%d right=%d result=%d streak=%d points=%lu\n", i, right, r, s_g.streak, (unsigned long)s_g.points);
    schedule(REVEAL_MS);
}

static void tapped(int i, void *user)
{
    (void)user;
    if (lib_lost()) return;
    if (s_phase == P_OVER) { again(); return; }
    if (s_phase == P_READY) return;
    if (s_phase == P_CLIP || s_phase == P_ASK) answered(i);
}

static void start(void)
{
    const char *why = blocked();
    if (why) { to_ready(why); return; }
    s_t0 = lv_tick_get();
    s_lib_tracks = g_lib.tracks;
    s_lib_n = g_lib.n;
    s_lib_pool = g_lib.pool;
    printf("APP game guess start rule=%d rounds=%d\n", s_sel.rule, s_g.rounds);
    begin_round();
}

static bool hub_tapped(void *user)
{
    (void)user;
    if (lib_lost()) return true;
    if (s_phase == P_READY || s_phase == P_BLOCKED) { start(); return true; }
    if (s_phase == P_OVER && s_over_taps) { again(); return true; }
    if (s_phase == P_CLIP || s_phase == P_ASK) {                  /* replay: the same clip from the same place */
        if (play_clip()) { s_phase = P_CLIP; schedule(100); }
        return true;
    }
    return false;
}

static void new_game(void)
{
    if (s_timer) lv_timer_pause(s_timer);
    game_result_hide();
    restore_player();
    ui_sectors_unlight(s_sec);
    s_phase = P_READY;
    s_fails = 0;
    s_gold_hit = false;
    pc_guess_init(&s_g, s_sel.rule, QUESTIONS[s_sel.mode % Q_N], games_seed("guess", s_games++));
    apply_style();
    for (int c = 0; c < PC_GUESS_CHOICES; c++) { ui_sectors_set_text(s_sec, c, ""); ui_sectors_set_paint(s_sec, c, c); }
    ui_sectors_set_looks(s_sec, UI_SEC_NORMAL);
    ui_sectors_set_progress(s_sec, 0, s_g.rounds);
    to_ready(blocked());
    if (s_phase == P_READY) ui_sectors_set_enabled(s_sec, false);   /* the field does nothing before the start: the hub starts it */
    printf("APP game guess new rule=%d rounds=%d style=%d\n", s_sel.rule, s_g.rounds, s_sel.style);
}
static void again(void) { new_game(); start(); }

static const char *mode_text(int v) { static char b[16]; snprintf(b, sizeof b, T(S_D_QS) /* %d questions */, QUESTIONS[v % Q_N]); return b; }
static const char *rule_name(int v) { return T_RAW(RULE_ZH[v], RULE_EN[v]); }
static void changed(int what, int v)
{
    (void)v;
    if (what == GR_STYLE) apply_style();
    else if (what == GR_AGAIN) again();
    else new_game();
}
/* console words (tests, the leak action, pictures): "right" / "wrong" tap the right / another sector while a question waits, "count <n>" sets the game's own count (its next seed) and
 * makes a ready game, "golden 1|0|auto" forces the golden question, "clip <track> <pct>" a clip of that track now (the player's conversion of the start per backend), "end" ends the clip now (as its timer would) */
static bool game_cmd(const char *a)
{
    unsigned n;
    if (sscanf(a, "count %u", &n) == 1) { s_games = n; new_game(); return true; }
    unsigned idx;
    if (sscanf(a, "clip %u %u", &idx, &n) == 2) { printf("APP game guess clipnow ok=%d\n", mp3_ctl_play_clip((int)idx, (int)n)); return true; }   /* test hook: a clip of any track at n %, as the question would */
    if (!strcmp(a, "golden auto")) { s_force_gold = 0; return true; }
    if (sscanf(a, "golden %u", &n) == 1) { s_force_gold = n ? 1 : 2; return true; }
    if (!strcmp(a, "state")) {
        printf("APP game guess state phase=%d asked=%d correct=%d streak=%d points=%lu taken=%d had=%d\n", s_phase, s_g.asked, s_g.correct, s_g.streak, (unsigned long)s_g.points, s_taken, s_had);
        return true;
    }
    bool right = !strcmp(a, "right");
    if (!right && strcmp(a, "wrong")) return false;
    int i = (s_phase != P_CLIP && s_phase != P_ASK) ? -1 : right ? s_g.answer : (s_g.answer + 1) % PC_GUESS_CHOICES;
    printf("APP game guess %s=%d\n", a, i);
    if (i >= 0) ui_sectors_tap(s_sec, i);
    return true;
}
static const game_ring_t RING = { .id = "guess", .modes = Q_N, .mode_text = mode_text, .rules = PC_GUESS_RULES, .rule_name = rule_name, .changed = changed, .cmd = game_cmd };

static bool game_knob(int d) { return ui_axis_knob(d) || ui_sectors_knob(s_sec, d); }

/* G4b: the shared exit's pause: a clip that is playing is stopped through the same call the end of a clip uses (stop_clip) and the question waits as P_ASK
 * (a tap replays it, as after any clip); the exit pauses the page's timers; Back from there puts the user's song back (destroy -> restore_player) */
static void exit_pause(void *u)
{
    (void)u;
    if (s_phase != P_CLIP) return;
    if (s_timer) lv_timer_pause(s_timer);
    stop_clip();
    s_phase = P_ASK;
    printf("APP game guess pause clip stopped\n");
}
static void exit_resume(void *u, uint32_t ms) { (void)u; s_t0 += ms; s_clip_t0 += ms; }
static bool in_flight(void *u) { (void)u; return s_phase >= P_CLIP && s_phase <= P_REVEAL; }   /* CF2: see simon */
static const ui_exit_def_t EXIT = { .mode = UI_EXIT_C, .running = in_flight, .pause = exit_pause, .resume = exit_resume };   /* G4b: taps answer  (CF2: popup after a free knob turn) */

static void create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_sel.mode = 1;
    s_taken = false;
    const ui_sectors_def_t def = { .n = PC_GUESS_CHOICES, .rot = 45, .hub_pct = 36, .tap_flash_ms = TAP_MS, .tones = TONES, .progress = true, .field = true };
    ui_obj_guard(ui_sectors_create(p, &def, tapped, hub_tapped, NULL), &s_sec);
    s_status = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, NULL), &s_status);
    game_ring_head(p, &RING, &s_sel);
    ui_exit_attach(p, &EXIT, NULL);
    s_timer = lv_timer_create(timer_cb, 1000, NULL);
    lv_timer_pause(s_timer);
    ui_timer_guard(p, &s_timer);
    new_game();
}

static void destroy(void)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    restore_player();                         /* Back (or any leaving) puts the user's song back */
}

const ui_screen_t scr_game_guess = { .name = "tools_game_guess", .create = create, .destroy = destroy, .on_knob = game_knob, .content = true };
