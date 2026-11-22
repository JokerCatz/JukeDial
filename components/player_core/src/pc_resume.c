// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_resume.h"
#include <stdio.h>
#include <string.h>

int pc_resume_format(const pc_resume_t *r, char *out, size_t cap)
{
    int n;
    if (cap) out[0] = 0;
    switch (r->kind) {
    case PC_RES_MP3: n = r->pos ? snprintf(out, cap, "mp3 %d %d %lu", r->a, r->b, (unsigned long)r->pos) : snprintf(out, cap, "mp3 %d %d", r->a, r->b); break;
    case PC_RES_CHIP: n = snprintf(out, cap, "chip %d %d %s", r->a, r->b, r->name); break;   /* name last: may hold spaces */
    case PC_RES_NOISE: n = snprintf(out, cap, "noise %d", r->a); break;
    case PC_RES_RADIO: n = snprintf(out, cap, "radio %d", r->a); break;
    case PC_RES_COLOR: n = snprintf(out, cap, "color %d", r->a); break;
    default: return 0;
    }
    if (n < 0 || (size_t)n >= cap) { if (cap) out[0] = 0; return -1; }
    return n;
}

bool pc_resume_parse(const char *s, pc_resume_t *r)
{
    memset(r, 0, sizeof *r);
    int a, b, used = 0;
    unsigned long pos;
    if (sscanf(s, "mp3 %d %d %lu%n", &a, &b, &pos, &used) == 3 && !s[used] && a >= -1 && b >= 0) {
        *r = (pc_resume_t){ .kind = PC_RES_MP3, .a = a, .b = b, .pos = (uint32_t)pos };
        return true;
    }
    if (sscanf(s, "mp3 %d %d%n", &a, &b, &used) == 2 && !s[used] && a >= -1 && b >= 0) {
        *r = (pc_resume_t){ .kind = PC_RES_MP3, .a = a, .b = b };
        return true;
    }
    if (sscanf(s, "chip %d %d %n", &a, &b, &used) == 2 && used && s[used] && a >= 0 && b >= 0) {
        *r = (pc_resume_t){ .kind = PC_RES_CHIP, .a = a, .b = b };
        snprintf(r->name, sizeof r->name, "%s", s + used);
        return true;
    }
    if (sscanf(s, "noise %d%n", &a, &used) == 1 && !s[used] && a >= 0) {
        *r = (pc_resume_t){ .kind = PC_RES_NOISE, .a = a };
        return true;
    }
    if (sscanf(s, "color %d%n", &a, &used) == 1 && !s[used] && a >= 0) {
        *r = (pc_resume_t){ .kind = PC_RES_COLOR, .a = a };
        return true;
    }
    if (sscanf(s, "radio %d%n", &a, &used) == 1 && !s[used] && a >= 0) {
        *r = (pc_resume_t){ .kind = PC_RES_RADIO, .a = a };
        return true;
    }
    return false;
}
