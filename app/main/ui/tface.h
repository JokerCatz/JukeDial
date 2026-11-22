// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"
/* The timing faces (HUD task): one shared drawing of a "time value + a few state fields" that the clock (HUD face only: its other faces are
 * clock_face.c), the pomodoro, the stopwatch and the countdown give their data to. The feature owns the numbers; a face only draws them.
 * Faces (pc_tf_*, pc_clockface.h): info = big digits and lines, rings = a progress ring round the digits, hud = the designed face of that feature and board
 * (face_art.c, TFACE task: fixed art, slots with fixed-format values). All calls from the LVGL task. */
#include "face_art.h"
enum { TF_CLOCK, TF_SW, TF_CD, TF_POMO, TF__FEATS };   /* which feature a face shows (the HUD has a designed screen per feature and board) */
typedef struct {
    /* info / rings */
    char big[16];            /* the time value: "14:32:08", "01:23.45", "04:59" */
    char tag[24];            /* a small line above the digits when set */
    char line[5][44];        /* the lines under the digits (rings shows 3) */
    int  nline;
    int  prog_pm;            /* 0..1000 progress (the ring), -1 none */
    bool red;                /* the digits and the ring in the warning colour (countdown last 10 s) */
    /* hud: the designed face's slot values (fixed formats) and its warning state */
    face_vals_t art;
} tface_data_t;

/* the slot values every HUD face can show: the clock's HH:MM (clock_hm), sync (0 no / 1 yes), power (0 unknown, 1 USB, 2 battery), battery % (FakePod: "087")
 * and its 5-segment bar. Returns the real warnings: bit 0 time not synced, bit 1 FakePod battery low (<= 15 %, not charging). Rebuilt once a second. */
int  tface_common(face_vals_t *v);

/* One block per face (PSRAM, freed with its box): the clock's HUD face and a page pushed over the clock (the pomodoro) can both exist. The box is the handle. */
/* the HUD art is a plug-in (_player/faces.zip): tface_hud_ok() = it is there and readable. Without it HUD is not offered (tface_step skips it) and a stored HUD choice is kept
 * (the zip may come later) but shows tface_fallback(feat) (info, rings for the pomodoro): tface_pref_feat reads a choice that way. tface_kind(box) is what a box really shows. */
bool tface_hud_ok(void);
int  tface_fallback(int feat);
int  tface_step(int kind, int d, int feat);           /* the next face of a feature (d = +1 / -1), skipping HUD when the zip is missing */
int  tface_pref_feat(const char *key, int dflt, int feat);
lv_obj_t *tface_create(lv_obj_t *parent, int kind, int feat);   /* kind: PC_TF_*; feat: TF_*; builds the face into a UI_CONTENT_D box centred in parent and returns the box (not clickable); NULL on no memory */
void tface_set(lv_obj_t *box, const tface_data_t *d); /* only what changed is set / redrawn; NULL box: nothing */
int  tface_kind(lv_obj_t *box);                       /* the face of a box (PC_TF_*), -1 none */
void tface_dump(lv_obj_t *box);                       /* console: "APP tface kind= feat= big= red= alert= <field>=<value> ..." the data as last set */

/* a feature's own face choice (cfg key per feature: pomoface, swface, cdface) */
int  tface_pref(const char *key, int dflt);           /* the stored face (PC_TF_*); unset: dflt */
void tface_pref_set(const char *key, int kind);
const char *tface_label(int kind);                    /* "資訊" "圓環" "HUD" in the language; HUD without the faces zip: the reason */
int  tface_hudcol(void);                              /* the HUD colour scheme (PC_HC_*, cfg hudcol; one choice for every HUD face) */
void tface_hudcol_set(int c);
const char *tface_hudcol_label(int c);
void tface_stats(void);                               /* console `tface stats`: "APP tface stats redraw_px=<since boot> mem=<bytes of the faces open>" */
