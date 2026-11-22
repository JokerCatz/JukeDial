// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The dial (shared piece, ideas G2): a needle gauge in the design circle with tick marks and number labels, for the toys and games that turn a value
 * (safe cracker first; the metronome's tempo, a safe, a radio dial, a thermostat, tuner, decibel meter, BPM: triage section 2). Never rebuilt in a page.
 * - Layout: the circle in the band between the HEAD row and Back (the band ui_arena and ui_board use; round 360 panel: radius 91 px, from ui_geom.h,
 *   no panel literal). ONE LVGL object with a custom draw event: the face, zones, ticks, labels, the needle, the hub with its text (HUB LIMIT: the hub is R/4 = 22 px, two characters in the large font fit, three or four use the main font; a longer text is cut: a page puts at most 4 characters, e.g. a BPM of 3 digits, in it).
 * - The scale: values lo..hi; `wrap` (a lock's 0..99: the dial goes round, `span` 360) or a gauge (clamped, `a0`/`span` the sweep, e.g. 135 and 270).
 *   Angles in degrees, 0 = 3 o'clock, clockwise. A tick every `minor` units, a longer one with the number every `major` (0: none).
 * - DIAL mode: the value is changed by the knob (the borrowed-knob way: a tap on the dial borrows it, 4 s after the last turn it is the volume again;
 *   the page's on_knob is ui_dial_knob) or by taps on the left (-1) / right (+1) half; a tap on the hub = confirm. One detent = one unit. Every change
 *   calls def->changed(value, delta). TARGET HAPTICS: ui_dial_target(target, reach) tells the piece where the page's number is; after each change the
 *   piece plays the graded haptic of its distance (pc_dial_grade: on it = the distinct strong click of a detent, within a third of reach = a tap,
 *   within reach = a tick, beyond = nothing): the closer, the stronger. ZONES: up to 4 coloured arcs on the rim (target zones, locked numbers).
 * - METER mode (ui_dial_meter, or def->meter): read-only; a value fed by code (ui_dial_feed) is smoothed (pc_meter: 1/2^def->smooth_shift of the way each
 *   1/30 s, default a quarter) and, with def->peak, a peak marker holds 1 s and then falls (its own box is invalidated, so it leaves no trace). Touch and knob do nothing. The timer runs only while the needle or the peak moves.
 * - PROXIMITY CUE (def->cue, off = the look before): a visual aid next to the haptics. A soft wedge on the face behind the ticks, centred on the needle, and the hub's
 *   border, light up the nearer the value is to the target (ui_dial_target's target and reach): pc_dial_cue gives the level 0..255 (0 beyond the reach, the
 *   strength on the target); the colour is `cue` of ui_dial_colours (black = the peak colour). `ui_dial_cue(strength)` is the percent (def->cue starts it at 100):
 *   a weaker hint = a dimmer wedge. Redrawn only when the level or the needle's place changes. NEEDLE SHAKE: `ui_dial_shake(amp10)` jitters the needle by up to amp10
 *   tenths of a degree each timer frame (static on a radio); 0 stops it. The jitter is drawn only: the value does not change.
 * - LABELS: def->label(v, buf, n) writes the text of the tick of value v (up to 7 characters; the hub keeps its own text), so a scale can show its unit; NULL = the number.
 * - Effects (a pulse of the rim: flash; a ring running out of the hub: wave): <= 12 frames, never block input.
 * - Time: a timer bound to the object at 30 fps, running only while something needs it (the game's `running`, an effect, a moving meter). def->step is
 *   called every 100 ms of running time (unpaused: the page's clock). With the screen off, a ring open over the page or another page on top a running
 *   game PAUSES (def->paused(true)) and the next tap on the dial only resumes it (as ui_arena).
 * State: one PSRAM block, freed with the object; one static pointer (the console's) in PSRAM .bss (built with -msmall-data-limit=0). LVGL task only.
 * Console (ui_dial_cmd, `dial ...`): "" dumps "APP dial name= mode=<dial|meter> v= lo= hi= target= reach= zones= shown= peak= fx= run= hold= paused= knob= ticks= fps=
 * inv_px= face_px= cue= cuepct= shake=" and then the game's line (def->dump); "turn <n>" the knob by n detents; "tap <x> <y>" a touch (px from the centre); "step <n>" n timer
 * steps now (1/30 s each, deterministic); "hold 1|0" stop / restart the timer's own steps; "meter 1|0" force meter mode (tests); "feed <v>" a meter value;
 * "game <arg>" the page's own console (def->cmd). */
#define UI_DIAL_ZONES 4
#define UI_DIAL_FULL  (4 * 90)                                       /* a span that goes all the way round (a wrapped dial); a page names no angle literal */
typedef struct {
    const char *name;                                         /* the page (console) */
    int lo, hi;                                               /* the scale's values */
    bool wrap;                                                /* the value goes round (hi + 1 is lo again); else clamped */
    int a0, span;                                             /* degrees: lo's angle and the sweep (a wrapped dial: 360) */
    int major, minor;                                         /* units between labelled ticks / plain ticks, 0 none */
    bool meter;                                               /* starts as a read-only meter */
    bool peak;                                                /* METER: a peak marker that holds 1 s and then falls (decibel meter); false: none */
    int smooth_shift;                                         /* METER: the needle goes 1/2^shift of the way each 1/30 s: 1 fast .. 6 slow; 0 = the default 2 */
    void (*label)(int v, char *buf, int n);                   /* the text of a labelled tick (a radio's MHz); NULL: the value */
    bool cue;                                                 /* DIAL: the proximity cue is on (strength 100 until ui_dial_cue says otherwise) */
    void (*changed)(int v, int delta, bool knob, void *user); /* DIAL: the value changed by a turn (knob: the knob or the keys) or a tap; may be NULL */
    void (*confirm)(int v, void *user);                       /* DIAL: the hub was tapped; may be NULL */
    void (*step)(unsigned ms, void *user);                    /* every 100 ms of running (unpaused) time; may be NULL */
    bool (*running)(void *user);                              /* the page's clock runs (else the timer sleeps); NULL: never */
    void (*paused)(bool on, void *user);                      /* paused (screen off, covered) / resumed by the tap; may be NULL */
    void (*dump)(void *user);                                 /* the console's page line; may be NULL */
    void (*cmd)(const char *arg, void *user);                 /* console `dial game <arg>`; may be NULL */
} ui_dial_def_t;
typedef struct { lv_color_t fill, rim, tick, label, needle, hub, peak, cue; } ui_dial_colours_t;   /* cue: black = the peak colour */

lv_obj_t *ui_dial_create(lv_obj_t *parent, const ui_dial_def_t *def, void *user);   /* NULL: no memory */
void ui_dial_colours(lv_obj_t *dial, const ui_dial_colours_t *c);                   /* the page's palette (game_style); redraws */
void ui_dial_set(lv_obj_t *dial, int v);                                            /* the value by code (no haptic, no callback) */
int  ui_dial_value(const lv_obj_t *dial);
void ui_dial_target(lv_obj_t *dial, bool on, int target, int reach);                /* the haptic grading's target; off: none */
void ui_dial_cue(lv_obj_t *dial, int strength);                                     /* the cue's strength, percent 0..100 (0: dark); needs def->cue */
void ui_dial_shake(lv_obj_t *dial, int amp10);                                      /* the needle's jitter, tenths of a degree (0: none) */
void ui_dial_zone(lv_obj_t *dial, int id, int lo, int hi, lv_color_t c, lv_opa_t opa);   /* a rim arc from lo to hi (inclusive, wraps round the end) */
void ui_dial_zone_hide(lv_obj_t *dial, int id);
void ui_dial_zones_clear(lv_obj_t *dial);
void ui_dial_hub(lv_obj_t *dial, const char *text);                                 /* the hub's text (up to 7 bytes); NULL: the value */
void ui_dial_flash(lv_obj_t *dial, lv_color_t c);
void ui_dial_wave(lv_obj_t *dial, lv_color_t c);
void ui_dial_meter(lv_obj_t *dial, bool on);                                        /* meter / dial mode (the needle starts at the value) */
void ui_dial_feed(lv_obj_t *dial, int v);                                           /* METER: the value to go to */
void ui_dial_wake(lv_obj_t *dial);                                                  /* the page's clock runs now: the timer resumes */
void ui_dial_pause(lv_obj_t *dial);                                                 /* pause a running game now (as being covered does: def->paused(true)); the shared exit (ui_exit.h) calls it */
void ui_dial_resume(lv_obj_t *dial);                                                /* undo ui_dial_pause as the resuming tap does, without a tap */
void ui_dial_unpause(lv_obj_t *dial);                                               /* a new game: no pause left (def->paused is not called) */
bool ui_dial_knob(int detents);                                                     /* the page's on_knob */
void ui_dial_cmd(const char *arg);
