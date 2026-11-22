// SPDX-License-Identifier: LGPL-2.1-or-later
#include "crashnet.h"
#include <stdio.h>
#include "esp_attr.h"
#include "cfg.h"
#include "pc/pc_crashnet.h"
#include "pc/pc_paths.h"

static RTC_NOINIT_ATTR pc_cn_rtc_t s_rtc;     /* RTC memory: 16 bytes, no internal RAM */
static EXT_RAM_BSS_ATTR struct { pc_cn_marks_t marks; uint32_t found[2]; int nfound; } S;   /* PSRAM: no internal RAM (the ceilings have no headroom) */
#define s_marks S.marks
#define s_found S.found
#define s_nfound S.nfound

void crashnet_early(bool crashed)
{
    s_nfound = pc_cn_rtc_take(&s_rtc, crashed, s_found);
    if (s_nfound) printf("APP crashnet crash at %d track(s) in flight\n", s_nfound);
}

static void save(void)
{
    if (!g_sd_ok) return;
    pc_int_mkdir(PC_CFG_DIR);
    int rc = pc_cn_marks_save(&s_marks, PC_CFG_CRASHED);
    if (rc) printf("APP crashnet save rc=%d\n", rc);
}

void crashnet_card(bool card_ok)
{
    if (card_ok) pc_cn_marks_load(&s_marks, PC_CFG_CRASHED);
    bool changed = false;
    for (int i = 0; i < s_nfound; i++) changed |= pc_cn_marks_add(&s_marks, s_found[i]);
    if (changed) save();
    printf("APP crashnet marks=%d new=%d\n", s_marks.n, s_nfound);
    s_nfound = 0;
}

void crashnet_playing(const char *path) { pc_cn_rtc_set_cur(&s_rtc, pc_cn_id(path)); }
void crashnet_queued(const char *path) { pc_cn_rtc_set_next(&s_rtc, path ? pc_cn_id(path) : 0); }
void crashnet_idle(void) { pc_cn_rtc_set_cur(&s_rtc, 0); }
bool crashnet_marked(const char *path) { return s_marks.n && pc_cn_marks_has(&s_marks, pc_cn_id(path)); }
int crashnet_count(void) { return s_marks.n; }

void crashnet_clear(const char *path)
{
    if (pc_cn_marks_del(&s_marks, pc_cn_id(path))) { printf("APP crashnet mark cleared by a deliberate play\n"); save(); }
}
