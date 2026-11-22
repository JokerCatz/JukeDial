// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_outstat.h"
/* BTAUDIO: the output chain of what plays, as the places that decide it recorded it. One PSRAM record (0 B internal), read by the settings pages' live
 * lines, the Bluetooth / wired info lines and the details page's 輸出 block through pc_os_* (pc_outstat.h): never a second copy of the text.
 * Writers: the audio task (wired plan, every source open / re-plan), the Bluetooth service (its live state: bt_svc_out_in), the speed and effects gates (read when asked).
 * Readers: the LVGL task. The wired record is a seqlock (one writer). */
void out_status_wired_set(const pc_os_wired_in_t *decision);   /* audio task: the plan just made (src_rate, cap, hard, hw_cap, plan, up_from, refused_hard; the rest is filled when read) */
void out_status_wired_clear(void);                              /* audio task: the source ended or was refused for good: nothing plays */
void out_status_get_wired(pc_outstat_t *os);                    /* the wired chain (kind WIRED; src_rate 0 while nothing plays) */
void out_status_get_bt(pc_outstat_t *os);                       /* the headphones link as the plan makes it (kind A2DP; conn false when no sink) */
void out_status_get(pc_outstat_t *os);                          /* what plays now: wired, Bluetooth headphones or Auracast, with source, speed and effects; kind NONE: nothing */
bool out_status_get_bt_next(pc_outstat_t *os);                  /* FIX4 I3: false unless a stored preference waits for the next connection; then the chain it will give */
void out_status_bt_prefs(pc_os_bt_in_t *in);                    /* the stored preferences into in (cfg bt_codec / bt_rate / bt_bits / bt_chan / ldac_kbps) */
bool out_status_bt_build_ldac(void);                            /* this build has the vendor codec stack */
void out_status_text_block(char *b, unsigned n);                /* the details page block in the screen's language ("" when nothing plays) */
void out_status_console(const char *lang);                  /* console `outstat [tw|cn|en]`: the live lines and the details block, one "APP outstat" line each */
