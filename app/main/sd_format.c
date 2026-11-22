// SPDX-License-Identifier: LGPL-2.1-or-later
/* 記憶卡格式化: the upper half (sd_format.h). The safety rules in one place: sd_format_start is the only caller of sd_hw_format. */
#include "sd_format.h"
#include "pc/pc_paths.h"
#include <stdio.h>
#include <sys/stat.h>
#include "cfg.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "library_svc.h"
#include "usb_mode.h"
#include "pc/pc_sdguide.h"

static int s_state, s_err, s_stage, s_out;
static int64_t s_t0_us;
static pc_fmt_plan_t s_plan;
static uint64_t s_bytes;
static uint32_t s_serial;
static bool s_probed;                    /* s_bytes / s_serial belong to the card the page showed */

const char *sd_format_reason(void) { return g_sd_ok ? NULL : sd_hw_reason(); }
bool sd_format_guide_row(void) { return pc_sd_guide_row(sd_format_reason(), g_sd_ok, usb_mode_active()); }
bool sd_format_offered(void) { return s_state != SDF_RUN && pc_fmt_entry_allowed(sd_format_reason(), g_sd_ok, usb_mode_active()); }

bool sd_format_probe(uint64_t *bytes, pc_fmt_plan_t *plan)
{
    s_probed = sd_format_offered() && sd_hw_card(&s_bytes, &s_serial) && pc_fmt_plan(s_bytes, 512, &s_plan);
    if (bytes) *bytes = s_probed ? s_bytes : 0;
    if (plan) *plan = s_plan;
    return s_probed;
}

static bool refuse(const char *why) { printf("APP sd format refused why=%s\n", why); return false; }

bool sd_format_start(pc_fmtguard_t *g)
{
    if (s_state == SDF_RUN) return refuse("running");
    if (!pc_fmtguard_take(g)) return refuse("steps");                 /* warning, code, armed tap: all three, in order, in this session */
    if (!sd_format_offered()) return refuse("entry");                 /* mounted, no card, other error, USB drive mode */
    if (!s_probed) return refuse("size");                              /* the page showed no size: nothing was planned */
    s_probed = false;                                                  /* one plan, one start */
    printf("APP sd format start size=%llu fs=%s cluster=%lu bu=%lu align=%lu start=%lu sectors=%lu\n", (unsigned long long)s_bytes, pc_fmt_fs_name(s_plan.fs),
           (unsigned long)s_plan.cluster_bytes, (unsigned long)s_plan.bu_sectors, (unsigned long)s_plan.align, (unsigned long)s_plan.part_start, (unsigned long)s_plan.part_sectors);
    s_state = SDF_RUN;
    s_err = 0;
    s_t0_us = esp_timer_get_time();
    if (!sd_hw_format(&s_plan, s_serial)) sd_format_finish(ESP_ERR_NO_MEM, PC_FMT_STAGE_BEFORE);
    return true;
}

void sd_format_finish(int err, int stage)
{
    long ms = (long)((esp_timer_get_time() - s_t0_us) / 1000);
    s_stage = stage;
    s_out = pc_fmt_outcome(stage, err == ESP_OK);                      /* touched or not: only BEFORE is "untouched" (review I1) */
    g_sd_ok = sd_hw_reason() == NULL;                                  /* mounted now: formatted and mounted, or a card that mounted at the re-check (left as it was) */
    if (s_out != PC_FMT_OUT_DONE) {
        s_state = SDF_FAIL;
        s_err = err == ESP_OK ? ESP_FAIL : err;
        printf("APP sd format failed err=%s stage=%s out=%s ms=%ld\n", esp_err_to_name(s_err), pc_fmt_stage_name(stage), pc_fmt_outcome_name(s_out), ms);
        return;
    }
    pc_int_mkdir(PC_CFG_DIR);                                     /* the firmware's own folders */
    pc_int_mkdir(PC_INDEX_DIR);
    s_state = SDF_DONE;
    s_err = 0;
    printf("APP sd format done ms=%ld stage=%s mounted=%d\n", ms, pc_fmt_stage_name(stage), g_sd_ok);
    if (g_sd_ok) library_rescan();
}

int sd_format_state(void) { return s_state; }
int sd_format_err(void) { return s_err; }
int sd_format_outcome(void) { return s_out; }
const pc_fmt_plan_t *sd_format_plan(void) { return &s_plan; }

void sd_format_print(void)
{
    static const char *const N[] = { "idle", "run", "done", "fail" };
    const char *r = sd_format_reason();
    printf("APP sd format state=%s err=%s stage=%s out=%s offered=%d reason=%s mounted=%d usb=%d\n", N[s_state & 3], esp_err_to_name(s_err), pc_fmt_stage_name(s_stage),
           s_state >= SDF_DONE ? pc_fmt_outcome_name(s_out) : "-", sd_format_offered(), r ? r : "-", g_sd_ok, usb_mode_active());
}
