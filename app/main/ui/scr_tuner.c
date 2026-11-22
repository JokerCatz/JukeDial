// SPDX-License-Identifier: LGPL-2.1-or-later
/* Tuner (調音器), in Tools: a list of Listen / Play a note / 說明 and the three pages under it. Composed of shared pieces only.
 * LISTEN (scr_tuner_listen): the microphone (tuner_svc.h, 16 kHz, no task) and a needle gauge (ui_dial in meter mode, -50..+50 cents, the green zone is +-5): the title line is the nearest
 *   note and the pitch (A4  440.2 Hz), the hub the cents; the needle turns green in tune. The music pauses while listening and goes on when it was playing: the page's timer starts listening only while
 *   it is the top page with the screen on and stops it otherwise (a page over it, screen off), and destroy stops it, so the mic runs on no other path. The menu ring (a tap on 選單) holds the A4 reference
 *   (axis cell: the borrowed knob, 430..450 Hz) and 說明.
 * PLAY A NOTE (scr_tuner_tone): one big button plays / stops the reference tone (sfx_tone_set: the toy sounds' mix point, before the device gain; the music pauses meanwhile and goes on after);
 *   the note C2..C6 is the borrowed-knob axis under it; the voice (sine, square 50 / 25 %, triangle, saw) is chosen in a list page (tap the menu's 音色); 吉他弦 steps E2 A2 D3 G3 B3 E4.
 * The instrument app's chip voices (NES, GB, SMS, AY, SCC, PCE, N163, VRC6, 5B, POKEY, YM2612) are not here: see the games-toys design notes (Tuner) for the reasons.
 * Built with -msmall-data-limit=0 (its statics are PSRAM .bss). Console: `tuner` (see tuner_cmd). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_svc.h"
#include "font.h"
#include "pc/pc_tuner.h"
#include "screens.h"
#include "sfx.h"
#include "tuner_svc.h"
#include "ui.h"
#include "ui_axis.h"
#include "ui_dial.h"
#include "ui_exit.h"
#include "ui_frame.h"
#include "ui_help.h"
#include "ui_kit.h"
#include "ui_popring.h"
#include "ui_theme.h"

#define NOTE_LO   36                       /* C2 */
#define NOTE_HI   84                       /* C6 */
#define NOTE_A4   69
#define LISTEN_MS 40
static const int GUITAR[6] = { 40, 45, 50, 55, 59, 64 };   /* E2 A2 D3 G3 B3 E4 */
static const char *wave_name(int w)
{
    switch (w) {
    case PC_TONE_SQ50: return T(S_WAVE_SQ50) /* Square 50% */;
    case PC_TONE_SQ25: return T(S_WAVE_SQ25) /* Square 25% */;
    case PC_TONE_TRI: return T(S_WAVE_TRI) /* Triangle */;
    case PC_TONE_SAW: return T(S_WAVE_SAW) /* Saw */;
    default: return T(S_WAVE_SINE) /* Sine */;
    }
}

/* what the pages keep while the device is on */
static int s_note_d;                        /* the tone page's note as an offset from A4 (zero-initialised: PSRAM .bss, not internal .data) */
#define s_note (NOTE_A4 + s_note_d)
static int s_wave;                         /* PC_TONE_* */
static int s_guitar;                       /* the last guitar string stepped to */

/* ---- music pause shared by both pages: the page paused it, the page gives it back ---- */
static bool s_we_paused;
static void music_pause(void)
{
    if (s_we_paused) return;
    aud_status_t st;
    audio_svc_status(&st);
    if (st.state == AUD_PLAYING && !audio_svc_paused()) { audio_svc_pause(true); s_we_paused = true; }
}
static void music_resume(void)
{
    if (!s_we_paused) return;
    s_we_paused = false;
    audio_svc_pause(false);
}

/* ================= the chooser ================= */
static lv_obj_t *s_list;
static void row_listen(lv_event_t *e) { (void)e; ui_push(&scr_tuner_listen, NULL); }
static void row_tone(lv_event_t *e) { (void)e; ui_push(&scr_tuner_tone, NULL); }
static void list_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_TUNER) /* Tuner */), &s_list);
    ui_list_row(s_list, T(S_TUNER_LISTEN) /* Listen */, UI_PANEL, row_listen, NULL);
    ui_list_row(s_list, T(S_TUNER_PLAY) /* Play a note */, UI_PANEL, row_tone, NULL);
    ui_list_help_row(s_list, &HELP_TUNER);
    ui_back_button(p);
}
static void list_destroy(void) { s_list = NULL; }
static bool list_knob(int d) { return ui_list_knob(s_list, d); }
const ui_screen_t scr_tuner = { .name = "tools_tuner", .create = list_create, .destroy = list_destroy, .on_knob = list_knob };

/* ================= the A4 reference axis (both pages' rings) ================= */
static bool a4_step(int d)
{
    const int v = ui_axis_step(tuner_a4(), PC_A4_MIN, PC_A4_MAX, 1, d, ui_axis_from_tap());
    if (v == tuner_a4()) return true;
    tuner_set_a4(v);
    return true;
}

/* ================= listen ================= */
static lv_obj_t *s_lpage, *s_ltitle, *s_ldial, *s_lcell;
static bool s_lfailed;                     /* the mic could not start this visit */
static bool s_ltuned;                      /* the needle's colour state */
static int s_lshown;                       /* the last cents shown (-1000 = none yet; set in listen_create) */
static char s_ltext[40];
static ui_popring_t s_lpop;

static void dial_look(bool tuned)
{
    const ui_dial_colours_t c = { .fill = UI_PANEL, .rim = UI_PANEL2, .tick = UI_DIM, .label = UI_DIM, .needle = tuned ? UI_ON : UI_FG, .hub = UI_PANEL2, .peak = UI_WARN, .cue = UI_ON };
    ui_dial_colours(s_ldial, &c);
    s_ltuned = tuned;
}
static void show(const tuner_reading_t *r)
{
    if (!s_ltitle || !s_ldial) return;
    char t[40], h[8];
    bool tuned = false;
    int cents = 0;
    if (s_lfailed) { snprintf(t, sizeof t, "%s", T(S_TUNER_NO_MIC) /* Microphone unavailable */); snprintf(h, sizeof h, "--"); }
    else if (r && r->voiced) {
        char nm[8];
        cents = (int)lroundf(r->cents);
        tuned = pc_in_tune(r->cents);
        if (r->hz >= 1000.0f) snprintf(t, sizeof t, "%s  %.0f Hz", pc_note_name(r->midi, nm, sizeof nm), (double)r->hz);
        else snprintf(t, sizeof t, "%s  %.1f Hz", pc_note_name(r->midi, nm, sizeof nm), (double)r->hz);
        if (cents) snprintf(h, sizeof h, "%+d", cents); else snprintf(h, sizeof h, "0");
    } else { snprintf(t, sizeof t, "%s", tuner_listening() ? T(S_TUNER_NONE) /* No note heard */ : " "); snprintf(h, sizeof h, "--"); }
    if (strcmp(t, s_ltext)) { snprintf(s_ltext, sizeof s_ltext, "%s", t); lv_label_set_text(s_ltitle, t); }
    if (tuned != s_ltuned) dial_look(tuned);
    ui_dial_hub(s_ldial, h);
    if (cents != s_lshown) { s_lshown = cents; ui_dial_feed(s_ldial, cents); }
}
static void listen_tick(lv_timer_t *t)
{
    (void)t;
    const bool want = ui_top() == &scr_tuner_listen && ui_screen_on();
    if (want && !tuner_listening() && !s_lfailed) {
        music_pause();
        if (!tuner_start()) { s_lfailed = true; music_resume(); show(NULL); }
    } else if (!want && tuner_listening()) { tuner_stop(); music_resume(); show(NULL); }
    if (!want) return;
    tuner_reading_t r;
    if (tuner_poll(&r)) show(&r);
}
static void listen_stop_all(void)
{
    tuner_stop();
    music_resume();
}

static const char *lcell_label(int i)
{
    static char b[40];
    (void)i;
    snprintf(b, sizeof b, "%s\n%d Hz", T(S_TUNER_A4) /* A4 pitch */, tuner_a4());
    return b;
}
static bool lring_step(int axis, int d) { (void)axis; return a4_step(d); }
static void lring_pick(int i) { if (i < 0) ui_popring_close(); }
static void listen_menu(lv_event_t *e)
{
    (void)e;
    s_lpop = (ui_popring_t){ .n = 1, .label = lcell_label, .pick = lring_pick, .step = lring_step, .axis_mask = 1u, .help = &HELP_TUNER };
    ui_popring_open(s_lpage, &s_lpop, 0);
}
static const ui_exit_def_t LISTEN_EXIT = { .mode = UI_EXIT_A };
static void listen_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_lpage = ui_obj_guard(p, &s_lpage);
    s_lfailed = false;
    s_ltuned = false;
    s_lshown = -1000;
    s_ltext[0] = 0;
    s_ltitle = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, font_main()), &s_ltitle);
    ui_frame_option(ui_choice_row(p, UI_SLOT_HEAD, 1, listen_menu, &s_lcell));
    ui_obj_guard(s_lcell, &s_lcell);
    if (s_lcell) ui_choice_set(s_lcell, T(S_MENU) /* Menu */, UI_LOOK_NORMAL);
    static const ui_dial_def_t DIAL = { .name = "tuner", .lo = -50, .hi = 50, .wrap = false, .a0 = 210, .span = 120, .major = 25, .minor = 5, .meter = true, .smooth_shift = 2 };
    ui_obj_guard(ui_dial_create(p, &DIAL, NULL), &s_ldial);
    if (s_ldial) {
        dial_look(false);
        ui_dial_zone(s_ldial, 0, -5, 5, UI_ON, LV_OPA_COVER);
        ui_dial_hub(s_ldial, "--");
    }
    ui_exit_attach(p, &LISTEN_EXIT, NULL);             /* the page takes no taps: a tap on it shows the popup (the menu, Back) */
    ui_timer_bind(p, listen_tick, LISTEN_MS, NULL);
    show(NULL);
}
static void listen_destroy(void)
{
    listen_stop_all();
    s_lpage = s_ltitle = s_ldial = s_lcell = NULL;
}
static bool listen_knob(int d) { return ui_axis_knob(d); }
const ui_screen_t scr_tuner_listen = { .name = "tools_tuner_listen", .create = listen_create, .destroy = listen_destroy, .on_knob = listen_knob, .content = true };

/* ================= play a note ================= */
static lv_obj_t *s_tpage, *s_ttitle, *s_tvoice, *s_tbig, *s_tbiglbl, *s_tnote;
static bool s_tplay;
static ui_popring_t s_tpop;
enum { TC_VOICE, TC_GUITAR, TC_A4, TC_N };

static float tone_hz(void) { return pc_note_hz(s_note, (float)tuner_a4()); }
static void tone_apply(void) { sfx_tone_set(s_wave, tone_hz(), s_tplay); }
static const char *note_label(int axis);
static void tone_show(void)
{
    char nm[8], t[40];
    if (s_ttitle) { snprintf(t, sizeof t, "%s  %.1f Hz", pc_note_name(s_note, nm, sizeof nm), (double)tone_hz()); lv_label_set_text(s_ttitle, t); }
    if (s_tvoice) lv_label_set_text(s_tvoice, wave_name(s_wave));
    if (s_tnote && lv_obj_get_child_count(s_tnote)) lv_label_set_text(lv_obj_get_child(s_tnote, 0), note_label(0));   /* the axis button's label (child 0): the guitar cell and the console change the note too */
    if (s_tbiglbl) lv_label_set_text(s_tbiglbl, s_tplay ? T(S_STOP) /* Stop */ : T(S_PLAY) /* Play */);
}
static void tone_play(bool on)
{
    if (on == s_tplay) return;
    s_tplay = on;
    if (on) music_pause();
    tone_apply();
    if (!on) music_resume();
    tone_show();
    printf("APP tuner tone play=%d note=%d hz=%.2f wave=%d\n", on, s_note, (double)tone_hz(), s_wave);
}
static void big_cb(lv_event_t *e) { (void)e; tone_play(!s_tplay); }
static bool note_step(int axis, int d)
{
    (void)axis;
    s_note_d = ui_axis_step(s_note, NOTE_LO, NOTE_HI, 1, d, ui_axis_from_tap()) - NOTE_A4;
    tone_apply();
    tone_show();
    return true;
}
static const char *note_label(int axis)
{
    static char b[16];
    (void)axis;
    char nm[8];
    snprintf(b, sizeof b, "%s", pc_note_name(s_note, nm, sizeof nm));
    return b;
}
static const char *tcell_label(int i)
{
    static char b[TC_N][40];
    char nm[8];
    if (i == TC_VOICE) snprintf(b[i], sizeof b[i], "%s\n%s", T(S_TUNER_VOICE) /* Voice */, wave_name(s_wave));
    else if (i == TC_GUITAR) snprintf(b[i], sizeof b[i], "%s\n%s", T(S_TUNER_GUITAR) /* Guitar string */, pc_note_name(GUITAR[s_guitar], nm, sizeof nm));
    else snprintf(b[i], sizeof b[i], "%s\n%d Hz", T(S_TUNER_A4) /* A4 pitch */, tuner_a4());
    return b[i];
}
static bool tring_step(int axis, int d)
{
    if (axis == TC_GUITAR) {
        s_guitar = ((s_guitar + d) % 6 + 6) % 6;
        s_note_d = GUITAR[s_guitar] - NOTE_A4;
    } else if (axis == TC_A4) a4_step(d);
    else return false;
    tone_apply();
    tone_show();
    return true;
}
static void tring_pick(int i)
{
    if (i == TC_VOICE) { ui_popring_close(); ui_push(&scr_tuner_voice, NULL); }
    else if (i < 0) ui_popring_close();
}
static void tone_menu(lv_event_t *e)
{
    (void)e;
    s_tpop = (ui_popring_t){ .n = TC_N, .label = tcell_label, .pick = tring_pick, .step = tring_step, .axis_mask = 1u << TC_GUITAR | 1u << TC_A4, .help = &HELP_TUNER };
    ui_popring_open(s_tpage, &s_tpop, 0);
}
static void tone_tick(lv_timer_t *t)
{
    (void)t;
    if (s_tplay && ((ui_top() != &scr_tuner_tone && ui_top() != &scr_tuner_voice) || !ui_screen_on())) tone_play(false);   /* another page over it (the voice list keeps it: the new voice is heard at once), the screen off: the tone stops */
}
static void tone_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_tpage = ui_obj_guard(p, &s_tpage);
    s_tplay = false;
    s_ttitle = ui_obj_guard(ui_text(p, UI_SLOT_TITLE, font_main()), &s_ttitle);
    lv_obj_t *cell = NULL;
    ui_frame_option(ui_choice_row(p, UI_SLOT_HEAD, 1, tone_menu, &cell));
    if (cell) ui_choice_set(cell, T(S_MENU) /* Menu */, UI_LOOK_NORMAL);
    s_tvoice = ui_obj_guard(ui_text_dim(p, UI_SLOT_UPPER, NULL), &s_tvoice);
    s_tbig = ui_obj_guard(ui_big_button(p, UI_SLOT_CENTER, big_cb, NULL, &s_tbiglbl), &s_tbig);
    ui_obj_guard(s_tbiglbl, &s_tbiglbl);
    s_tnote = ui_obj_guard(ui_axis_button(p, UI_SLOT_FOOT, 0, note_step, note_label), &s_tnote);
    ui_back_button(p);
    ui_timer_bind(p, tone_tick, 100, NULL);
    tone_show();
}
static void tone_destroy(void)
{
    sfx_tone_set(s_wave, tone_hz(), false);
    s_tplay = false;
    music_resume();
    s_tpage = s_ttitle = s_tvoice = s_tbig = s_tbiglbl = s_tnote = NULL;
}
static void tone_resume(void) { tone_show(); }
static bool tone_knob(int d) { return ui_axis_knob(d); }
const ui_screen_t scr_tuner_tone = { .name = "tools_tuner_tone", .create = tone_create, .destroy = tone_destroy, .on_knob = tone_knob, .on_resume = tone_resume, .content = true };

/* ================= the voice list ================= */
static lv_obj_t *s_vlist;
static void voice_row(lv_event_t *e)
{
    s_wave = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_tplay) tone_apply();
    printf("APP tuner voice=%d\n", s_wave);
    ui_pop();
}
static void voice_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_obj_guard(ui_list_titled(p, T(S_TUNER_VOICE) /* Voice */), &s_vlist);
    for (int w = 0; w < PC_TONE_WAVES; w++) ui_list_row_mark(s_vlist, wave_name(w), w == s_wave, voice_row, (void *)(intptr_t)w);
    ui_back_button(p);
}
static void voice_destroy(void) { s_vlist = NULL; }
static bool voice_knob(int d) { return ui_list_knob(s_vlist, d); }
const ui_screen_t scr_tuner_voice = { .name = "tools_tuner_voice", .create = voice_create, .destroy = voice_destroy, .on_knob = voice_knob };

/* ================= console ================= */
/* `tuner`            "APP tuner page=<top>" and the service's counters (tuner_print)
 * `tuner listen|tone|voice`   open that page (a fresh one on the chooser)       `tuner shown`  the listen page's line: "APP tuner shown title=.. hub=.. tuned=.. dial=.. failed=.. mic=.. music=<paused|go>"
 * `tuner play|stop`  the tone page's big button           `tuner note <midi>`  `tuner wave <n>`  `tuner a4 <hz>`  `tuner guitar <n>` (the menu's cell, steps by n)   `tuner tone`  "APP tuner tonestate ..." */
void tuner_cmd(const char *a)
{
    int n;
    if (!a[0]) { printf("APP tuner page=%s\n", ui_top() ? ui_top()->name : "-"); tuner_print(); return; }
    if (!strcmp(a, "listen")) { ui_push(&scr_tuner_listen, NULL); return; }
    if (!strcmp(a, "tone")) { ui_push(&scr_tuner_tone, NULL); return; }
    if (!strcmp(a, "voice")) { ui_push(&scr_tuner_voice, NULL); return; }
    if (!strcmp(a, "shown")) {
        aud_status_t st;
        audio_svc_status(&st);
        printf("APP tuner shown title=%s hub=%d tuned=%d failed=%d mic=%d music=%s paused_by_page=%d\n", s_ltext, s_lshown == -1000 ? 0 : s_lshown, s_ltuned, s_lfailed, tuner_listening(),
               st.state == AUD_PLAYING && !audio_svc_paused() ? "go" : "paused", s_we_paused);
        return;
    }
    if (!strcmp(a, "tonestate")) {
        printf("APP tuner tonestate note=%d hz=%.3f wave=%d play=%d sfx=%d blocks=%u paused_by_page=%d a4=%d\n", s_note, (double)tone_hz(), s_wave, s_tplay, sfx_tone_active(), (unsigned)sfx_tone_blocks(), s_we_paused, tuner_a4());
        return;
    }
    if (!strcmp(a, "play")) { if (ui_top() == &scr_tuner_tone) tone_play(true); else printf("APP tuner not open\n"); return; }
    if (!strcmp(a, "stop")) { if (ui_top() == &scr_tuner_tone) tone_play(false); else printf("APP tuner not open\n"); return; }
    if (sscanf(a, "note %d", &n) == 1) { s_note_d = (n < NOTE_LO ? NOTE_LO : n > NOTE_HI ? NOTE_HI : n) - NOTE_A4; tone_apply(); tone_show(); return; }
    if (sscanf(a, "wave %d", &n) == 1) { s_wave = n < 0 ? 0 : n >= PC_TONE_WAVES ? PC_TONE_WAVES - 1 : n; if (s_tplay) tone_apply(); tone_show(); return; }
    if (sscanf(a, "a4 %d", &n) == 1) { tuner_set_a4(n); tone_apply(); tone_show(); return; }
    if (sscanf(a, "guitar %d", &n) == 1) { tring_step(TC_GUITAR, n); return; }
    printf("APP tuner unknown %s\n", a);
}
