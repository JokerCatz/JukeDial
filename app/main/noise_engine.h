// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_paths.h"
#include "pc/pc_scenes.h"
/* White noise (spec 4.3, 5.6): scenes from /sdcard/_player/sleep/library.muk (stored zip); at most NOISE_MAX_ON layers
 * of a scene play at once, each a seamless loop (pc_loop) of an Ogg Vorbis / MP3 / FLAC entry, mixed at 44.1 kHz.
 * Layers can be turned on and off while playing (opened off the audio task, faded in/out). LVGL context. */
#define NOISE_MAX_ON 3
typedef enum { NOISE_LAYER_OFF, NOISE_LAYER_LOADING, NOISE_LAYER_ON, NOISE_LAYER_FAILED } noise_layer_state_t;
#define NOISE_MUK PC_NOISE_MUK
#define NOISE_CUSTOM PC_CFG_DIR "/scenes.json"
int  noise_load(void);                       /* scenes count; reads the zip directory + JSON once */
int  noise_scene_count(void);
const pc_scene_t *noise_scene(int i);
const char *noise_scene_name(int i);         /* the name in the UI language (en_name in English, 簡中 via data.tsv, else zh-TW) */
int  noise_engine_start(int scene);          /* 0 ok */
int  noise_engine_toggle_layer(int scene, int layer);   /* pc_ls_result_t (PC_LS_FULL: already NOISE_MAX_ON on), <0 bad */
noise_layer_state_t noise_engine_layer_state(int scene, int layer);
const char *noise_layer_name(int scene, int layer);       /* the sound's name from seed.json, "" if unknown */
void noise_engine_set_layer_volume(int layer, float v);   /* 0..1, on top of the scene's own volume */
float noise_engine_layer_volume(int layer);
void noise_engine_set_breath(int permille);   /* ideas G7 (the breathing coach): a gain 500..1000 permille (clamped) on top of the layers' volumes, so never above the user's level; 1000 = off; it holds only 1 s (the page asks again every step: a covered page or a screen off lets go by itself), start and stop reset it; the gain ramps inside each block. Scenes only (a coloured noise has no volume of its own) */
int  noise_engine_breath(void);              /* the permille now (the simulator's test) */
void noise_engine_stop(bool fade_10s);
int  noise_current(void);                    /* scene index, -1 */
int  noise_save_custom(int scene);           /* the scene as edited (on layers first, volumes) as a new custom scene; its index; plays it if it was playing */
/* custom scenes (spec 5.6 B): add a sound of library.muk to a scene being edited. 0 added and on, 1 added off
 * (NOISE_MAX_ON already on), -EEXIST already there, -ENOSPC 16 sounds. Switching scenes drops unsaved edits. */
int  noise_engine_add_sound(int scene, int sound);
bool noise_scene_modified(int scene);
int  noise_delete_custom(int scene);          /* custom scenes only; stops it if playing; 0 ok */
int  noise_cat_count(void);                  /* seed.json categories + "uncategorised" */
const char *noise_cat_name(int cat);
int  noise_cat_sounds(int cat, int *out, int max);   /* sound indices in that category */
const char *noise_sound_name(int sound);
