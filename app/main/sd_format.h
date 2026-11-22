// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pc/pc_fmtguard.h"
/* 記憶卡格式化 (cardfmt): the service behind the 記憶卡 page (ui/scr_card.c). Offered only for a card whose last mount failed with
 * "gpt" or "nofs", never for a mounted card, never for "nocard", never in the USB drive mode (pc_fmt_entry_allowed). The page owns the guard
 * (pc_fmtguard_t: warning, Konami code, armed tap, in that order, in one session of the page); sd_format_start takes it and starts only when the
 * guard hands over GO (pc_fmtguard_take) and the entry rule still holds. Upper half (this file, sd_format.c): firmware and simulator alike.
 * Lower half (sd_hw_*): the board's sd_hw.c (bsp_sdcard_format), the simulator's sim/fake_sd.c. All calls from the LVGL task. */
typedef enum { SDF_IDLE, SDF_RUN, SDF_DONE, SDF_FAIL } sdf_state_t;

const char *sd_format_reason(void);      /* the SD layer's word for the last failed mount: "nocard" "stuck" "gpt" "nofs" "err"; NULL after a good mount */
bool sd_format_guide_row(void);          /* cardsafe: the settings list shows the card row (any failed mount, pc_sd_guide_row); its page carries the what-to-do line */
bool sd_format_offered(void);            /* the entry rule now (and no format running) */
/* the card's size, read once more from the raw card (the 記憶卡 page, entering the flow): false when it cannot be read; *plan = what it becomes */
bool sd_format_probe(uint64_t *bytes, pc_fmt_plan_t *plan);
bool sd_format_start(pc_fmtguard_t *g);  /* false: refused (printed: APP sd format refused why=...); true: running (SDF_RUN) */
int  sd_format_state(void);              /* sdf_state_t */
int  sd_format_err(void);                /* the esp_err_t of the last SDF_FAIL (0 otherwise) */
const pc_fmt_plan_t *sd_format_plan(void);   /* the plan of the last start */
int  sd_format_outcome(void);            /* pc_fmt_out_t of the last finished format (what the result page says) */
void sd_format_print(void);              /* console: APP sd format state= err= stage= out= ... (the guard's half: cardfmt, scr_card.c) */

/* ---- the lower half ---- */
const char *sd_hw_reason(void);
bool sd_hw_card(uint64_t *bytes, uint32_t *serial);   /* the raw card: size and CID serial (false: cannot be read, or mounted) */
/* start formatting with `p` for the card with `serial`; true: running, sd_format_finish(err, stage) follows on the LVGL task; false: could not start.
 * The lower half checks, before any write, that the card in the slot does not mount now and is the card that was shown (pc_fmt_same_card). */
bool sd_hw_format(const pc_fmt_plan_t *p, uint32_t serial);
void sd_format_finish(int err, int stage);   /* LVGL task: the lower half is done (err 0: formatted and mounted; stage: pc_fmt_stage_t, how far it got) */
