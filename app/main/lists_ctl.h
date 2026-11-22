// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "cJSON.h"
/* "list_*" requests of the settings protocol: glyph-pack lists on /sdcard/lists/ (lists_ctl.c). LVGL task.
 * r already holds "id" and "ok": true; returns r (with ok false and "error" on failure). */
cJSON *lists_ctl(const char *op, cJSON *r, const cJSON *q);
