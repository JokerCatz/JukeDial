// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Crash safety net (all formats): the track being decoded is noted in RTC memory (a small struct that survives a panic / watchdog reset),
 * and a boot whose reset reason is a crash turns it into a persisted mark: the track is not played again by itself (resume, next, shuffle) and the lists say
 * so; a deliberate play clears the mark. Pure: the caller owns the RTC struct (RTC_NOINIT_ATTR on the board), the file path, and decides "crashed".
 * An id is the CRC-32 of the track's path (never 0): 4 bytes instead of a path in RTC memory. RTC cost on the board: sizeof(pc_cn_rtc_t) = 16 bytes. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PC_CN_MAX 24                                  /* marks kept (oldest dropped first) */
typedef struct { uint32_t magic, cur, next, chk; } pc_cn_rtc_t;   /* cur: playing; next: opened for the gapless join (its open runs while cur plays) */
typedef struct { uint32_t id[PC_CN_MAX]; int n; } pc_cn_marks_t;

uint32_t pc_cn_id(const char *path);                  /* the id of a path */
void pc_cn_rtc_set_cur(pc_cn_rtc_t *r, uint32_t id);  /* 0 = nothing playing (also clears next) */
void pc_cn_rtc_set_next(pc_cn_rtc_t *r, uint32_t id); /* 0 = no queued track */
/* The boot: the ids that were in flight when the last run ended (0, 1 or 2: cur, then next) when crashed and the struct is intact; always clears it. */
int  pc_cn_rtc_take(pc_cn_rtc_t *r, bool crashed, uint32_t out[2]);

bool pc_cn_marks_has(const pc_cn_marks_t *m, uint32_t id);
bool pc_cn_marks_add(pc_cn_marks_t *m, uint32_t id);  /* true when the set changed */
bool pc_cn_marks_del(pc_cn_marks_t *m, uint32_t id);  /* true when the set changed */
int  pc_cn_marks_save(const pc_cn_marks_t *m, const char *path);   /* 0, or -errno; text, one 8-digit hex id per line */
int  pc_cn_marks_load(pc_cn_marks_t *m, const char *path);         /* 0 (also: no file = empty), or -errno */
#ifdef __cplusplus
}
#endif
