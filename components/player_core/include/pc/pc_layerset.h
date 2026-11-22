// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_power.h"
#include "pc/pc_scenes.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Which layers of a white-noise scene play (spec 5.6): at most `max` at once, the first `max` by default.
 * Turning one on while `max` are on is refused (nothing turns off by itself). */
typedef struct { bool on[PC_SCENE_MAX_LAYERS]; int n, max; } pc_layerset_t;
typedef enum { PC_LS_OFF = 0, PC_LS_ON = 1, PC_LS_FULL = 2 } pc_ls_result_t;
void pc_layerset_init(pc_layerset_t *s, int nlayers, int max);
int  pc_layerset_toggle(pc_layerset_t *s, int i);       /* pc_ls_result_t, or -1 for a bad index */
void pc_layerset_set(pc_layerset_t *s, int i, bool on);  /* forced (a layer that failed to open goes off) */
int  pc_layerset_count(const pc_layerset_t *s);
/* a sound added to the scene (spec 5.6 B): on when fewer than max are on, else off; -1 when the scene is full */
int  pc_layerset_add(pc_layerset_t *s);                  /* the new layer's index */

/* The source kind the audio task passes to the post hook (aud_process_fn). Every source gets the fx chain; what runs when is the mode rule (pc_fx_allowed). */
typedef enum { PC_SRC_MP3, PC_SRC_CHIP, PC_SRC_NOISE } pc_src_t;
#ifdef __cplusplus
}
#endif
