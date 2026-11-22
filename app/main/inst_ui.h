// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "inst_kit.h"
#include "pc/pc_instr.h"
#include "ui/ui.h"
/* The instrument app (spec 7, taps only) on the shared page stack (UI unify T10): ui_start_host(k_inst_host) with the rim's 12-segment LED
 * ring (spec 18) as the host underlay; the play page is the bottom of the stack. This file keeps the start, the 30 Hz tick (notes, beats,
 * the ring effect, the chip and kit debounce), the stats, the settings the pages change and the console aids; it builds no widget: the pages
 * are in ui/scr_inst.c (T10, T11), the pads in ui/inst_pads.c. LVGL lock held for every call. */
void inst_ui_start(lv_display_t *disp, lv_indev_t *touch, pc_instr_cfg_t *cfg);
void inst_ui_page(int page);         /* console / tests: 0 = the play page (ui_home); else ui_home, then push 1 pads, 2 metronome, 3 settings, 4 chips, 5 looper; prints INST ui page=N */
void inst_ui_exit(void);             /* all notes off, cfg saved, restart into the player */
void inst_ui_knob(int detents);      /* console: a knob turn, the knob's own path (ui_knob_inject) */
void inst_ui_row(int r);             /* console: a tap on row r of the instrument list on top (settings: INST_ROW_*, chips, kits) */
void inst_ui_metro(int what);         /* console: 0 start/stop, 1 a Tap press, 2 beats per bar, 3 clock int/ext */
void inst_ui_pad(int pad);            /* console: a pad tap (0..7): note on, then note off */
void inst_ui_fx(int mode);            /* console and the settings row: the ring effect (pc_fx_mode_t), in memory only */
void inst_ui_stat(void);              /* console: frame time, ring draw, FFT since the last call, cur=<row> (scr_inst_cur); resets */
void inst_ui_ring_tune(int group, int div);   /* console A/B: segments per invalidated area, 30 Hz / div */
void inst_ui_loop_more(int k);        /* console "inst loop more N": a tap on row N of the looper's more list (opened first: the looper page, then the list) */
/* the UI half of the instrument's console (inst page|knob|row|metro|tap|pad|fx|loop ...|uistat|ring|exit|bpm|clock|kit): true when it ran the command;
 * instrument.c asks it first, the simulator's console_app_cmd is this function. Runs on the LVGL task. */
bool inst_cmd_ui(const char *c);

/* ---- what the pages (ui/scr_inst.c) use ---- */
pc_instr_cfg_t *inst_ui_cfg(void);
void inst_ui_save(void);              /* the settings into cfg "inst" (debounced write) */
void inst_ui_set_volume(int v);       /* the instrument's master volume (0..100), saved */
void inst_ui_pick_chip(int i);        /* now (a pending chip step is dropped) */
void inst_ui_chip_step(int d);        /* the play ring's chip axis: the pending chip moves by d; it is applied 350 ms after the last step */
int  inst_ui_chip_shown(void);        /* the chip the play page names: the pending one while it waits, else the engine's */
void inst_ui_set_clock_ext(bool ext);
/* the metronome (T11): the tempo, the beats per bar, tap tempo; saved like the other settings */
#define INST_BPM_LO 40                /* the tempo cell's range (a bounded axis); tap tempo is clamped to it too; the console keeps PC_BPM_MIN..MAX */
#define INST_BPM_HI 240
const char *inst_ui_tempo_why(void);  /* NULL: the tempo is the metronome's own; else why not (a loop, the external clock): 速度 and Tap are inert */
const char *inst_ui_metre_why(void);  /* NULL: the beats per bar are the metronome's own; else why not (a loop only: a MIDI clock has no metre): 拍號 is inert */
void inst_ui_set_bpm(int bpm);        /* clamped PC_BPM_MIN..PC_BPM_MAX */
void inst_ui_set_bpb(int bpb);        /* 2..7 */
int  inst_ui_tap(void);               /* a Tap press now (pc_tap on the press times): the BPM it set (INST_BPM_LO..HI) once four taps are in, 0 before, -1 inert */
int  inst_ui_taps(void);              /* taps in the current run (it ends 2 s after the last) */
void inst_ui_metro_toggle(void);      /* start / stop */
/* the pads (T11): the pads page passes these to inst_pads_create; the flash, then the note (audio first) */
void inst_ui_pad_down(int pad);
void inst_ui_pad_up(int pad);
const inst_kit_info_t *inst_ui_kit(void);   /* the current kit (read now; the pads page's labels; the pads' notes) */
const char *inst_ui_kit_err(int e);   /* the text of an INST_KIT_E_* code */

/* the pages (ui/scr_inst.c); the settings rows in list order (`inst row N` = INST_ROW_*) */
enum { INST_ROW_CHIP, INST_ROW_MAP, INST_ROW_CH, INST_ROW_VOL, INST_ROW_TIME, INST_ROW_KIT, INST_ROW_LAYER, INST_ROW_CLOCK,
       INST_ROW_CLICK, INST_ROW_BHAP, INST_ROW_FM, INST_ROW_FX, INST_ROW_EXIT, INST_ROW_N };
extern const ui_screen_t scr_inst_play, scr_inst_chips, scr_inst_kits, scr_inst_set;
extern const ui_screen_t scr_inst_pads, scr_inst_metro, scr_inst_loop, scr_inst_loopmore;   /* T11 */
void scr_inst_frame(bool note);       /* inst_ui.c's 30 Hz tick: `note` = a note arrived (the play page's label); the page on top refreshes what it shows */
void scr_inst_beat(int beat, int acc);   /* a beat was heard: the metronome page's dots */
void scr_inst_loop_more(int r);       /* inst_ui_loop_more */
void scr_inst_row(int r);             /* inst_ui_row (also the looper's more list) */
int  scr_inst_cur(void);              /* the row of the current value in the instrument list on top (chips: the engine's chip; kits: 0 = none; settings: the borrowed row), -1 none */
