// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* The arena (shared piece, ideas G1): a fixed-step play field inside the display's design circle for the action games and toys (ring breakout first;
 * rotating deflector, stack tower, beat tap, breathing coach, the ambient scenes ... later: triage section 2). Shared by every field game, never rebuilt in a page.
 * - Layout: a circle in the band between the HEAD row and Back (the band ui_board uses), the largest that fits the band and the design circle
 *   (round 360 panel: radius 91 px; the square panel gets its own from ui_geom.h). One LVGL object (a custom draw event, no object per piece).
 * - Objects: up to `objs` slots the game sets every step: balls, arcs (annular sectors: bricks, paddles) and rounded rects, in px from the field's
 *   centre, colour and opacity given by the game (from its game_style palette). Setting a slot to what it already holds does nothing; a change
 *   invalidates only the slot's old and new boxes, so a step costs the areas that moved (console `arena`: inv_n, inv_px against field_px). Within a
 *   batch (a step, a touch, a turn, ui_arena_begin/end) the boxes merge into at most 6 areas (fix round 1 I1: LVGL keeps 32 areas a frame and redraws
 *   the whole screen past that; a turning ring of 36 bricks becomes one band).
 * - Canvas (ideas G5, spirograph): ui_arena_canvas makes ONE picture the size of the field's box (ARGB8888, PSRAM: 4 bytes a px, 2 x (R + 2)
 *   squared: ~138 KB on the round panel (186 x 186), transparent at first), drawn by the field over its disc and under the objects and effects. The game adds
 *   lines in px from the centre (ui_arena_canvas_line, anti-aliased, round ends); they queue (128) and are drawn in ONE pass when the batch ends
 *   (a step, a touch, a turn, ui_arena_begin/end), each line's box invalidated like an object's. ui_arena_canvas_clear wipes it. The picture goes
 *   with the field (the one PSRAM buffer is freed in its delete handler).
 * - Effects (game content inside the field, game-feel.md 3): burst (particles), text (a short rising, fading text: "+30", "x5"),
 *   flash (the rim pulses: a lost ball), wave (a ring runs out from the centre: a cleared level). Each <= 12 frames (~400 ms), never blocks input.
 * - Time: a timer bound to the object (ui_timer_bind, created with it, deleted with it) at 30 fps; each tick runs as many fixed steps (1/30 s) as
 *   real time asks (at most 3: no spiral after a stall), so the game's speed does not follow the frame rate. The timer pauses itself when the game
 *   is not running (def->running), no effect moves and the energy feed is off: no CPU while the ball waits.
 *   A step's work is the game's maths, a compare of every slot and the invalidation of the boxes that changed; the drawing is LVGL's, of those boxes only.
 * - Pause: with the screen off, a ring open over the page (game_ring) or another page on top, a running game PAUSES (def->paused(true): the page
 *   says so) and the timer stops (no polling while covered, fix round 1); effects jump to their end. The next tap on the field only resumes
 *   (def->paused(false)) and borrows the knob again, so a wake never costs a ball; a new game of the page clears the pause (ui_arena_unpause).
 * - Input: a touch on the field = UI_ARENA_TAP with its point (px from the centre); a tap also BORROWS THE KNOB for the field (ui_axis_borrow,
 *   the borrowed-knob rule: 4 s after the last turn the knob is volume again; every tap, the resuming one too). While the game runs the borrow is kept alive (a running rally never
 *   turns into a volume change); once the ball rests, the 4 s run out as usual. The knob (and the FakePod keys, ui_knob_inject) reach the game as
 *   UI_ARENA_STEP with the detents. The page's on_knob is ui_arena_knob.
 * - Energy (optional): ui_arena_energy_on samples the music's level (the newest 256 samples of audio_svc_wave, 10 times a second, no FFT) for a
 *   style that pulses (game_style.pulse); 0..255.
 * State: one PSRAM block (header + 28 bytes a slot) freed with the object; the one static pointer (the console's) is .bss in PSRAM (this file is built
 * with -msmall-data-limit=0). All calls from the LVGL task.
 * Console (ui_arena_cmd, `arena ...`): "" dumps "APP arena name=<game> objs=<live>/<slots> ticks=<steps> fps=<steps in the last second> run=<0|1>
 * hold=<0|1> paused=<0|1> knob=<0|1> energy=<0..255> fx=<effects> inv_n=<areas> inv_px=<px> field_px=<px>" (the areas of the last step) and then the
 * game's own line (def->dump); "step <n>" runs n fixed steps now (deterministic: tests and pictures); "hold 1|0" stops / restarts the timer's
 * own steps; "tap <x> <y>" a touch at that point from the field's centre (as the touch does it); each of these then dumps. "game <arg>" the game's own console (def->cmd). "at <id>" an object's slot ("APP arena at <id>
 * kind=<ball|arc|rect|-> x= y= r= a0= a1= box=x1,y1,x2,y2"). */

#define UI_ARENA_FPS      30
#define UI_ARENA_OBJS_MAX 64
typedef enum { UI_ARENA_TAP = 0, UI_ARENA_STEP } ui_arena_in_t;
typedef struct {
    const char *name;                                         /* the game (console) */
    int objs;                                                 /* object slots, 1..UI_ARENA_OBJS_MAX */
    void (*step)(void *user);                                 /* one fixed step (1/30 s): the game moves and sets its objects */
    bool (*running)(void *user);                              /* steps change something now (else the timer pauses); NULL: always */
    void (*input)(ui_arena_in_t kind, int v, int x, int y, void *user);   /* TAP: (x, y) px from the centre; STEP: v detents (+ = clockwise) */
    void (*paused)(bool on, void *user);                      /* the game paused (screen off, covered) / the tap that resumes it; may be NULL */
    void (*dump)(void *user);                                 /* the console's game line; may be NULL */
    void (*cmd)(const char *arg, void *user);                 /* console `arena game <arg>`: the game's own test setup (a level, a brick); may be NULL */
    uint8_t fps;                                              /* 0 / 30: the timer ticks at 30 fps; 15: every 66 ms, two fixed steps a tick (a toy that is heavy to draw; console `arena rate 15|30` tries it on the board) */
    bool full_sq;                                             /* SQ2: `full` on the square panel only (the round glass keeps its field under the status line) */
    bool centred;                                             /* a frame page: the field is centred on the panel between the title slot's line and the base slot's line (ui_frame_field_centred) */
    bool full;                                                /* G6 Game of Life: the field is the whole design circle's square (R = UI_DESIGN_D / 2 - 2), centred on the screen: the page makes it FIRST, the HEAD row and the exit above it take their own touches */
    bool toy;                                                 /* G5 ambient scenes: a scene that always runs: the knob's borrow is NOT kept alive by the steps (a tap borrows it for 4 s: the page's STEP, then volume again; a toy never pins the knob). A toy is never 'paused' by a cover (screen off, a ring, another page): its timer only stops and ui_arena_kick restarts it */
} ui_arena_def_t;

/* The field in `parent` (a page root); def and user must outlive it. NULL: no memory (the page shows nothing to play). */
lv_obj_t *ui_arena_create(lv_obj_t *parent, const ui_arena_def_t *def, void *user);
int  ui_arena_radius(const lv_obj_t *arena);                  /* the field's radius in px (0 for NULL): the game scales its units to it */
/* the background: a disc of `fill` (opa), a rim line, and a faint track ring between track_in and track_out px (0, 0: none); redraws the field */
void ui_arena_field(lv_obj_t *arena, lv_color_t fill, lv_opa_t opa, lv_color_t rim, int track_in, int track_out, lv_color_t track);
/* objects: id 0..objs-1, drawn in id order. Coordinates in px from the centre; angles in degrees, 0 = 3 o'clock, clockwise (a1 > a0, a1 - a0 <= 360) */
void ui_arena_ball(lv_obj_t *arena, int id, int x, int y, int r, lv_color_t c, lv_opa_t opa);
void ui_arena_arc(lv_obj_t *arena, int id, int r_in, int r_out, int a0, int a1, bool round_ends, lv_color_t c, lv_opa_t opa);
void ui_arena_rect(lv_obj_t *arena, int id, int x, int y, int w, int h, int radius, lv_color_t c, lv_opa_t opa);
void ui_arena_hide(lv_obj_t *arena, int id);
void ui_arena_clear(lv_obj_t *arena);                         /* every object and effect gone (one redraw of the field) */
/* a batch of sets outside the field's own step / touch / turn (those are batches already): their boxes merge into at most 6 areas, sent at the end */
void ui_arena_begin(lv_obj_t *arena);
void ui_arena_end(lv_obj_t *arena);
/* effects */
void ui_arena_burst(lv_obj_t *arena, int x, int y, lv_color_t c, int n);   /* n 3..8 particles out of (x, y) */
void ui_arena_text(lv_obj_t *arena, int x, int y, const char *text, lv_color_t c);   /* up to 11 bytes */
void ui_arena_flash(lv_obj_t *arena, lv_color_t c);           /* the rim pulses */
void ui_arena_wave(lv_obj_t *arena, lv_color_t c);            /* a ring runs out from the centre */
/* time */
void ui_arena_kick(void);                                     /* a toy's timer restarts when nothing covers it (ui.c: the screen woke; ui_popring: a ring closed; the page's on_resume) */
void ui_arena_wake(lv_obj_t *arena);                          /* the game runs again (a serve): the timer resumes now, no catch-up for the time it waited */
void ui_arena_pause(lv_obj_t *arena);                         /* pause a running game now (as being covered does: def->paused(true)); the shared exit (ui_exit.h) calls it */
void ui_arena_resume(lv_obj_t *arena);                        /* undo ui_arena_pause as the resuming tap does (def->paused(false)), without a tap */
void ui_arena_unpause(lv_obj_t *arena);                       /* the page started over (a new game): no pause left, no tap needed (def->paused is not called) */
void ui_arena_energy_on(lv_obj_t *arena, bool on);            /* sample the music's energy (keeps the timer running while on) */
int  ui_arena_energy(const lv_obj_t *arena);                  /* 0..255, 0 when off or silent */
/* input */
bool ui_arena_has_knob(void);                                 /* the build has a rotary knob (UI_HAS_KNOB); without one the keys arrive as UI_ARENA_STEP, with one a STEP is a knob turn (volume unless the game borrowed it)  */
bool ui_arena_knob(int detents);                              /* the page's on_knob: true when the knob is the field's (or another borrowed axis of the page) */
/* the canvas (see above): created on the first call (a hidden LVGL canvas the field paints itself); NULL: no memory (the game shows nothing to draw on) */
lv_obj_t *ui_arena_canvas(lv_obj_t *arena);
void ui_arena_canvas_line(lv_obj_t *arena, int x0, int y0, int x1, int y1, int w, lv_color_t c, lv_opa_t opa);   /* w 1..40 px */
void ui_arena_canvas_clear(lv_obj_t *arena);
/* G6 Game of Life: the canvas as raw ARGB8888 pixels (the game writes cells itself, no LVGL draw pass: 1 px cells are 130 000 of them). (0, 0) = the box's top-left; stride in px.
 * ui_arena_canvas_dirty(x0, y0, x1, y1) (box px, inclusive) tells the field what changed: the boxes of a batch merge into the <= 6 areas as everything else does. */
/* the opaque canvas (G6 Game of Life, perf): the same picture as RGB565 (2 bytes a px, half of the PSRAM), cleared with the field's fill (call ui_arena_field first, and
 * ui_arena_canvas_clear again after a fill change), drawn over the whole square box (no rim, no clip). The game writes opaque pre-blended colours with ui_arena_canvas_pixels16
 * (NULL for an ARGB canvas). The field's fill must be opaque. ui_arena_canvas_line is not for it. */
bool ui_arena_round(void);   /* the build's panel is round (a game's field is then the circle of the box's width, else the whole square) */
lv_obj_t *ui_arena_canvas565(lv_obj_t *arena);
uint16_t *ui_arena_canvas_pixels16(lv_obj_t *arena, int *stride);
void ui_arena_canvas_dirty(lv_obj_t *arena, int x0, int y0, int x1, int y1);
void ui_arena_cmd(const char *arg);
