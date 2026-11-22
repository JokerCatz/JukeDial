// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_sfx.h"
/* Toy sound effects: an entry of the chiptune sound bank (sfx_bank.def: NES, GB wave and AY voices; recipes and short jingles;
 * pc_sfx.h) is mixed over whatever plays, BEFORE the device gain, so it follows the volume like the music (audio_svc.c, the block loop); while the
 * music is paused (the block's gain goes to 0) it is added AFTER the gain instead, times the device-volume gain and the sleep fade (pc_sfx_route).
 * The shared service of every toy and game (spec 2026-11-13-ui-shared-area.md): a page calls sfx_play("pop"); nothing else. Setting cfg "toysfx"
 * (default 1, 設定「小工具音效」): 0 = sfx_play posts nothing. The 音效庫 page (scr_sfxbank.c) auditions every entry with sfx_audition.
 * State: the parsed bank and the synth in PSRAM (internal static RAM 0); the bank text is in flash. */
void sfx_init(void);                                      /* parses the bank once (before the audio task starts; again: nothing) */
bool sfx_play(const char *name);                          /* LVGL task only (the single producer); false: off, unknown name or the queue is full */
bool sfx_audition(int id);                                /* the same for an entry id, whatever toysfx says (the audition page: a tap asks for it) */
const pc_sfx_bank_t *sfx_bank(void);                      /* the parsed bank (entries, labels, groups, credits) */
void sfx_mix(float *lr, int frames, uint32_t rate, float g);   /* audio task only (the single consumer): starts what was posted, adds the sounding voices times g; never allocates, locks or blocks */
void sfx_print(void);                                     /* console `sfx`: "APP sfx on=.. posted=.. off=.. full=.. stale=.. started=.. stolen=.. mixed=.. last=<newest last>" */
void sfx_print_bank(void);                                /* console `sfx bank`: "APP sfx bank n= dropped= notes= waves= credits=" and one "APP sfx entry" line an entry */
/* The tuner's reference tone (tools ring: 調音器, pc_tuner.h): one sine / square / triangle / saw at an exact pitch, added by sfx_mix after the effects with the
 * same gain g (so before the device gain when playing, after it times the device volume when paused: never unity on its own; nothing in audio_svc.c changed).
 * LVGL task only; takes effect within one block, with a 10 ms ramp. Not affected by the toysfx setting (the page asked for it). */
void sfx_tone_set(int wave, float hz, bool on);          /* wave: PC_TONE_*; on false: ramps to silence */
bool sfx_tone_active(void);                              /* the last request was "on" */
uint32_t sfx_tone_blocks(void);                        /* blocks the tone was added to so far (console, tests) */
