// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* The sound effects as device state (spec an internal note §3.3, §4, §5): the values of every fx app, the
 * file /sdcard/_player/config/fx.txt, the posting to the chain, the mode bits. Portable (built in the simulator); the audio half is fx_audio.[ch].
 * LVGL task unless said. */
#include <stdbool.h>
#include "pc/pc_paths.h"
#include <stdint.h>
#include "pc/pc_fxapp.h"
#include "pc/pc_fxstate.h"
#define FX_DIR PC_SOUND_DIR              /* the import/export library: chipchill preset files, untouched by the state file */
#define FX_STATE_PATH PC_CFG_DIR "/fx.txt"
#define FX_P_MIX PC_FXSTATE_MIX                    /* fx_get/fx_set: a MIX app -> its 濃度; an app with an own "mix" parameter -> that parameter; neither -> get 1, set ignored */
typedef enum { FX_OFF, FX_RUN, FX_DIM } fx_look_t;

void fx_svc_start(void);                           /* loads fx.txt (migrates cfg "sound" once), creates the chain, hands it to fx_audio_start */
int  fx_count(void);  const pc_fxapp_t *fx_app(int i);
bool fx_on(int i);    void fx_set_on(int i, bool on);
float fx_get(int i, int p);  void fx_set(int i, int p, float v);   /* clamped, posted now, stored 3 s later */
void fx_reset_app(int i);                          /* defaults, on unchanged */
fx_look_t fx_look(int i, const char **why);        /* why: pc_fx_mode_why, "記憶體不足"/"Out of memory", "準備中"/"Preparing" (a rebuild is pending); NULL when there is nothing to say */
void fx_mode_set(uint32_t reason_bit, bool on);    /* any task: a PC_FXM_* reason on or off; logs APP fx mode=<bits> heavy=<run|off> on change */
uint8_t fx_mode_bits(void);                        /* any task */
int  fx_import_text(const char *toml, bool store, const char **unsup, int max);   /* the whole sound from a preset: 0 ok, <0 bad TOML (nothing changed); the reports go to unsup (pointers valid until the next import) */
char *fx_export_text(void);                        /* the state as TOML format = 1 (malloc, PSRAM; the caller frees), NULL when it cannot be made */
int  fx_list(char (*names)[48], int max);          /* the library's file names, sorted */
char *fx_read(const char *name);                   /* a library file's text (PSRAM, caller frees with heap_caps_free), NULL */
int  fx_select(const char *name);                  /* import that file, stored; "" = every app off, values kept; 0 ok */
int  fx_save(const char *name, const char *text);  /* checks the text loads, writes FX_DIR/name, then fx_select(name) */
const char *fx_state_from(void);                   /* the file of the last stored import, "" after any other change */
int  fx_on_count(void);
void fx_state_running(int *run, int *paused, const char **why);   /* the 現況 page: of the apps that are on, how many run (look RUN) and how many are paused (look DIM); why: the first pause reason (fx_look's text), NULL when none paused; LVGL task */
int  fx_unsupported(const char **out, int max);    /* the reports of the last import (stored or live): count */
void fx_state_save_now(void);                      /* before a reboot: writes fx.txt now when anything changed */
void fx_set_change_cb(void (*cb)(void));           /* one callback, run on the LVGL task after a look, mode or value change (every caller of the fx_ functions is the LVGL task; fx_mode_set from other tasks hops through ui_async) */
bool fx_console(const char *c);                    /* the "fx ..." lines and "fxs"; true = handled */
