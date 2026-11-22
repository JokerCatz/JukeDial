// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
/* Designed faces (TFACE, "特製畫面然後挖空填入數字"): a face is a fixed drawing (greyscale layers tinted per colour scheme at draw time)
 * plus slots that take fixed-format values (seven-segment digit sprites, pre-drawn state plates, segment bars), so the layout never moves.
 * The art is a PLUGIN on the TF card (FACEZIP): one uncompressed (STORE) zip _player/faces.zip made by tools/gen/make_faces.py, read in place with
 * the chip-zip reader (format: the ui design notes 設計錶面). Without it (missing, not a zip, compressed) no face opens and the features use their plain faces. The background is composed once into PSRAM; a value change copies the
 * background under its slot back and draws the slot again: only slot rectangles are invalidated. Shared piece (tface.c). LVGL task only. */

/* the fields a slot can show; the face.txt name of each is in face_art.c */
enum {
    FAV_TIME_HM, FAV_TIME_S, FAV_DATE, FAV_WDAY, FAV_ALARM, FAV_ALARM_HM, FAV_SYNC, FAV_POWER, FAV_BATT, FAV_BATTBAR, FAV_CLOCK_HM,
    FAV_MAIN, FAV_CC, FAV_HOURS, FAV_STATE, FAV_SET, FAV_PROG, FAV_LAPS, FAV_LAP1_NO, FAV_LAP1, FAV_LAP2_NO, FAV_LAP2, FAV_LAP3_NO, FAV_LAP3,
    FAV_BEST, FAV_PHASE, FAV_ROUND, FAV__N
};
typedef struct {
    char v[FAV__N][12];    /* digit slots: the text ("12:34", "2026-11-18"); pick / bar slots: a number ("2") */
    bool warn;             /* the warning layer (header 警戒 / WARNING, the red scheme): only for a real state */
} face_vals_t;

enum { FA_GREEN, FA_RED, FA_CYAN, FA_TRI, FA__SCHEMES };   /* = PC_HC_* */
typedef struct face_art face_art_t;
/* name: "rd_clock" "sq_sw" ...; builds an image child of box (the box's size); NULL when the zip or the face is missing or no memory. Freed with the box. */
face_art_t *face_art_open(lv_obj_t *box, const char *name);
void face_art_set(face_art_t *fa, const face_vals_t *v, int scheme);   /* redraws only what changed */
enum { FACE_PACK_OK, FACE_PACK_MISSING, FACE_PACK_BAD };
int face_art_pack_state(void);                                           /* the faces zip: FACE_PACK_* (opens the file and reads its directory: cheap, ask when a menu is built) */
uint32_t face_art_redraw_px(void);                                       /* pixels invalidated since boot (console `tface stats`, sim measure) */
uint32_t face_art_mem(const face_art_t *fa);                             /* PSRAM bytes this face holds */
const char *face_art_field_name(int f);
