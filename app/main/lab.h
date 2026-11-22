// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include "pc/pc_lab.h"
/* LAB: the run-time knobs of the test day (ranges and defaults: pc_lab.h, the one table). Values live in the card's config under "lab_<name>"; the
 * copy used by the code is read here once (cfg_load) and after every `lab` command, so a task other than the LVGL one may call lab_get() at any time. Nothing is
 * stored in internal RAM (PSRAM statics). A boot after a crash drops every lab_ key first (the setting that crashed it must not come back); a boot with any knob off its
 * default prints one line "APP lab active: key=value ...". No UI: the console's `lab` is the whole interface. */
void lab_load(void);                      /* end of cfg_load(): the crash reset, the values, the log line (idempotent) */
int  lab_get(int id);                     /* PC_LAB_*: the value in use (the default before lab_load) */
void lab_cmd(const char *arg);            /* console: "" lists, "reset", "<key>", "<key> <value>" (what follows "lab ") */
