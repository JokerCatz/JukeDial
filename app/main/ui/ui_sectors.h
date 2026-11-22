// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* Sectors (shared piece, ideas wave 0a G1; triage section 2): N equal sectors (2, 4 or 8) of a disc, drawn by ONE LVGL object (custom draw event).
 * Built once for every user: four-colour memory (4 colours) now; 2048 / Sokoban (4 directions), counters (value cells), chess clock (2 halves), Morse practice, guess the song (4 answers),
 * drink reminder / habits (with the progress ring), USB macro keys / HID remote (8 keys), Nim later.
 * - Layout: `full` = the design circle (a page without a HEAD row); else the largest disc in the band between the HEAD row and Back (the band ui_board
 *   uses), centred in it. Sector 0's centre is `rot` degrees clockwise from the top, the others follow clockwise (4 at 45: the classic quadrants top
 *   right, bottom right, bottom left, top left; 4 at 0: up, right, down, left). A gap of a few degrees between sectors; an optional hub in the middle
 *   (`hub_pct` of the outer radius) with its own text and tap; an optional progress ring round the outside (`progress`: ui_sectors_set_progress).
 * - Looks: each sector has a look NORMAL / LIT / DIM, painted with a colour: the style's sector colour k (game_style_sector; ui_sectors_set_paint, default
 *   k = i % 4) or the theme (no style: PANEL2 / A1 / PANEL). NORMAL = a darker mix of the colour, LIT = the colour itself, DIM = a faint mix (another
 *   player's turn, the wrong answer's others). A big text per sector (a number, a glyph, a short word) and the hub's text, in font_large.
 * - Flash: ui_sectors_flash lights a sector for a set time and then puts its look back: no fade, no animation (a bound check timer runs only while a
 *   sector is lit). The sector's tone plays with it when asked (sfx_play of def.tones[i], the toy-sound path).
 * - Touch: a touch-down (PRESSED: a game reacts at once, like the instrument's pads) takes the NEAREST sector by angle when it lands between the hub and a
 *   margin past the outer edge; in the hub it runs hub(), which says whether it did something (only then a haptic tap: no false feedback); anywhere else nothing. Every taken sector touch gives a haptic tap; an enabled sector also lights for
 *   def.tap_flash_ms, plays its tone and calls cb. Disabled (ui_sectors_set_enabled false: the device's turn) a sector touch does nothing at all; the hub
 *   still works. No pressed look beyond the sector's own light (haptic only).
 * - Keys (FakePod: no knob, two keys through ui_knob_inject): in key mode (default: on when the device has no knob) a cursor ring marks one sector;
 *   ui_sectors_knob(+1) moves it to the next sector, ui_sectors_knob(-1) presses the cursor's sector as a touch would. With a knob the mode is off and
 *   ui_sectors_knob returns false, so the knob stays the volume.
 * State: one PSRAM block (header + a few bytes a sector) freed with the object; the one static pointer (the console's) is .bss in PSRAM (the file is
 * built with -msmall-data-limit=0). All calls from the LVGL task. A redraw is only the changed sector's box (a flash) or the hub.
 * Console (ui_sectors_cmd, `sectors ...`): "" dumps ("APP sectors n=<n> cx=<x> cy=<y> ro=<px> ri=<px> en=<0|1> keys=<0|1> cursor=<i> lit=<bit mask>
 * looks=<n|l|d per sector> paint=<k per sector> hub=<text> progress=<n>/<max>" and "APP sectors text <i> <text>" for each non-empty text and "APP sectors status <text>" when the status line is set);
 * "tap <i>" a touch on sector i (then the dump), "hub" a touch on the hub, "at <i>" the screen point of a sector's middle, "hit <x> <y>" what a touch
 * there would take (-1 nothing, -2 the hub), "key <d>" ui_sectors_knob(d), "keys 1|0" key mode. */

#define UI_SECTORS_MAX 8
typedef enum { UI_SEC_NORMAL = 0, UI_SEC_LIT, UI_SEC_DIM, UI_SEC_LOOKS } ui_sec_look_t;
typedef struct {
    int n;                        /* 2, 4 or 8 */
    int rot;                      /* degrees clockwise from the top of sector 0's centre */
    bool full;                    /* the design circle; false: the band between the HEAD row and Back */
    int hub_pct;                  /* the hub's radius, % of the outer radius (0: no hub: the sectors meet near the centre) */
    int tap_flash_ms;             /* a touch lights its sector this long (0: no light) */
    const char *const *tones;     /* n sfx names (sfx_bank.def; NULL entries or NULL: silent) */
    bool progress;                /* a progress ring round the outside (ui_sectors_set_progress) */
    bool field;                   /* a frame page that keeps a status line at the top: the round field of ui_frame_field (SQ2), not the full circle */
    bool frame;                   /* a page with a content-first frame (ui_frame.h) takes the full layout, as `full`; without a frame the band layout */
} ui_sectors_def_t;
typedef void (*ui_sectors_tap_cb)(int sector, void *user);
struct game_style;                                       /* game_style.h */

/* The sectors in `parent`; cb (may be NULL) runs for every taken touch on an enabled sector, hub (may be NULL) for a touch on the hub (true: it acted, the touch
 * gets its haptic tap; false: nothing happened, no haptic). NULL: n is not 2, 4 or 8, or no memory. */
typedef bool (*ui_sectors_hub_cb)(void *user);
lv_obj_t *ui_sectors_create(lv_obj_t *parent, const ui_sectors_def_t *def, ui_sectors_tap_cb cb, ui_sectors_hub_cb hub, void *user);
void ui_sectors_set_style(lv_obj_t *s, const struct game_style *st);   /* kept by pointer (the caller's static or the table); NULL: the theme; redraws */
void ui_sectors_set_paint(lv_obj_t *s, int i, int k);    /* sector i takes the style's colour k (four-colour memory's Shuffle moves the colours) */
void ui_sectors_set_look(lv_obj_t *s, int i, ui_sec_look_t look);
void ui_sectors_set_looks(lv_obj_t *s, ui_sec_look_t look);   /* every sector */
void ui_sectors_set_text(lv_obj_t *s, int i, const char *text);   /* up to 15 bytes, cut at a character boundary; NULL or "": none */
void ui_sectors_set_hub(lv_obj_t *s, const char *text);
void ui_sectors_flash(lv_obj_t *s, int i, int ms, bool tone);   /* lit for ms, then its look again; tone: its sfx too */
void ui_sectors_unlight(lv_obj_t *s);                    /* every flash ends now */
void ui_sectors_set_status(lv_obj_t *s, const char *text);   /* the status the user needs, a small line inside the disc (under the hub, or in the middle without one; up to 32 bytes; NULL or "": none) */
void ui_sectors_set_enabled(lv_obj_t *s, bool on);
void ui_sectors_set_progress(lv_obj_t *s, int n, int max);   /* max segments round the ring (0: none drawn), n of them filled; needs def.progress */
void ui_sectors_set_keys(lv_obj_t *s, bool on);          /* key mode (the cursor) */
bool ui_sectors_knob(lv_obj_t *s, int d);                /* key mode: +1 the next sector, -1 press the cursor's; false when key mode is off */
void ui_sectors_tap(lv_obj_t *s, int i);                 /* a touch on sector i as the finger gives it (haptic, light, tone, cb; nothing when disabled): tests, the console */
int  ui_sectors_count(const lv_obj_t *s);                /* 0 for NULL */
int  ui_sectors_hit(const lv_obj_t *s, int x, int y);    /* screen coordinates -> the sector, -2 the hub, -1 nothing */
bool ui_sectors_centre(const lv_obj_t *s, int i, int *x, int *y);   /* the screen point in the middle of a sector (layout updated) */
void ui_sectors_cmd(const char *arg);
