// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Stops endless skipping when every file fails (Review Focus 2). */
#define PC_FAILSTREAK_MAX 5
typedef struct { int fails; } pc_failstreak_t;
bool pc_failstreak_record(pc_failstreak_t *f, bool ok);   /* true: PC_FAILSTREAK_MAX failures in a row -> stop */
#ifdef __cplusplus
}
#endif
