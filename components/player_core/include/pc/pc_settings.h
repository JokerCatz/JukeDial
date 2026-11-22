// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Device settings the web page may change (spec web 3): a whitelist with ranges. */
typedef struct { const char *key; int min, max, def; bool reboot; } pc_setting_t;
const pc_setting_t *pc_settings(int *n);
/* key=value text -> 0 and *out = the value, or -1 unknown key / not a number / out of range / not an allowed step */
int pc_setting_check(const char *key, const char *val, int *out);
#ifdef __cplusplus
}
#endif
