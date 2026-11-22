// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Diff hook: if set and returns true, the rotation is consumed and not used for focus. */
typedef bool (*bsp_knob_hook_t)(int32_t diff);

/* Direction-contact click decoder on GPIO interrupts (see bsp_knob.c) (A=BSP_PIN_KNOB_A, B=BSP_PIN_KNOB_B), an LVGL encoder
 * indev and a default lv_group. Call with the LVGL lock held. */
esp_err_t bsp_knob_start(lv_disp_t *disp, bool swap_ab, lv_indev_t **out_indev);
int32_t   bsp_knob_get_count(void);          /* detents, swap applied */
int32_t   bsp_knob_get_edges(void);          /* total interrupt edges seen (diagnostic) */
void      bsp_knob_set_swap(bool swap);      /* flips sign only */
bool      bsp_knob_get_swap(void);
lv_group_t *bsp_knob_group(void);
void      bsp_knob_set_hook(bsp_knob_hook_t hook);

/* Diagnostic edge trace. Pass the cursor returned last time (start with 0); returns the new cursor. */
typedef struct { int64_t t_us; uint8_t a, b; } bsp_knob_trace_t;
uint32_t  bsp_knob_trace_read(uint32_t from, bsp_knob_trace_t *out, uint32_t max);

#ifdef __cplusplus
}
#endif
