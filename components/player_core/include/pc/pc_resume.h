// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* What was playing when the device rebooted to switch mode (cfg key "resume", one shot).
 * mp3: a = folder (-1 = whole library), b = track; chip: name = system, a = file, b = sub-track; noise: a = scene; radio: a = station; color: a = noise colour. */
#define PC_RESUME_MAX 96
typedef enum { PC_RES_NONE, PC_RES_MP3, PC_RES_CHIP, PC_RES_NOISE, PC_RES_RADIO, PC_RES_COLOR } pc_res_kind_t;
typedef struct { pc_res_kind_t kind; int a, b; uint32_t pos; char name[64]; } pc_resume_t;   /* pos: mp3 byte offset (0 = start) */
int  pc_resume_format(const pc_resume_t *r, char *out, size_t cap);   /* length; 0 for NONE; -1 when it does not fit */
bool pc_resume_parse(const char *s, pc_resume_t *r);                  /* false: r->kind = NONE */
#ifdef __cplusplus
}
#endif
