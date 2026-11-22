// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_crashnet.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_crc32.h"

#define MAGIC 0xC4A5E7B1u

uint32_t pc_cn_id(const char *path)
{
    uint32_t c = pc_crc32(0, path, (uint32_t)strlen(path));
    return c ? c : 1u;
}

static uint32_t chk(const pc_cn_rtc_t *r) { return MAGIC ^ r->cur ^ (r->next * 2654435761u); }
static void seal(pc_cn_rtc_t *r) { r->magic = MAGIC; r->chk = chk(r); }
static bool intact(const pc_cn_rtc_t *r) { return r->magic == MAGIC && r->chk == chk(r); }

void pc_cn_rtc_set_cur(pc_cn_rtc_t *r, uint32_t id) { r->cur = id; r->next = 0; seal(r); }
void pc_cn_rtc_set_next(pc_cn_rtc_t *r, uint32_t id)
{
    if (!intact(r)) { r->cur = 0; }
    r->next = id;
    seal(r);
}

int pc_cn_rtc_take(pc_cn_rtc_t *r, bool crashed, uint32_t out[2])
{
    int n = 0;
    if (crashed && intact(r)) {
        if (r->cur) out[n++] = r->cur;
        if (r->next && r->next != r->cur) out[n++] = r->next;
    }
    memset(r, 0, sizeof *r);
    seal(r);
    return n;
}

bool pc_cn_marks_has(const pc_cn_marks_t *m, uint32_t id)
{
    for (int i = 0; i < m->n; i++)
        if (m->id[i] == id) return true;
    return false;
}

bool pc_cn_marks_add(pc_cn_marks_t *m, uint32_t id)
{
    if (!id || pc_cn_marks_has(m, id)) return false;
    if (m->n == PC_CN_MAX) { memmove(m->id, m->id + 1, (PC_CN_MAX - 1) * sizeof m->id[0]); m->n--; }
    m->id[m->n++] = id;
    return true;
}

bool pc_cn_marks_del(pc_cn_marks_t *m, uint32_t id)
{
    for (int i = 0; i < m->n; i++)
        if (m->id[i] == id) { memmove(m->id + i, m->id + i + 1, (size_t)(m->n - i - 1) * sizeof m->id[0]); m->n--; return true; }
    return false;
}

int pc_cn_marks_save(const pc_cn_marks_t *m, const char *path)
{
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -errno;
    for (int i = 0; i < m->n; i++) fprintf(f, "%08x\n", (unsigned)m->id[i]);
    if (fclose(f)) { remove(tmp); return -EIO; }
    if (rename(tmp, path)) { remove(path); if (rename(tmp, path)) { remove(tmp); return -errno; } }   /* FatFs: rename onto an existing file fails */
    return 0;
}

int pc_cn_marks_load(pc_cn_marks_t *m, const char *path)
{
    m->n = 0;
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT ? 0 : -errno;
    char line[32];
    while (fgets(line, sizeof line, f)) {
        char *end;
        unsigned long v = strtoul(line, &end, 16);
        if (end != line && v) pc_cn_marks_add(m, (uint32_t)v);
    }
    fclose(f);
    return 0;
}
