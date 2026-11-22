// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_put.h"
#include <errno.h>
#include <stddef.h>

static const char *num(const char *s, uint32_t *out)       /* decimal digits up to UINT32_MAX, then one space */
{
    uint64_t v = 0;
    const char *p = s;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint64_t)(*p++ - '0');
        if (v > 0xFFFFFFFFu) return NULL;
    }
    if (p == s || *p != ' ') return NULL;
    *out = (uint32_t)v;
    return p + 1;
}

int pc_put_parse(const char *line, pc_put_chunk_t *c)
{
    if (!line || line[0] != '@') return -EINVAL;
    const char *p = num(line + 1, &c->off);
    if (!p || !(p = num(p, &c->n)) || !c->n || c->n > PC_PUT_CHUNK_MAX || !*p) return -EINVAL;
    c->b64 = p;
    return 0;
}

int pc_put_decide(uint32_t cur, uint32_t off, uint32_t n)
{
    uint64_t end = (uint64_t)off + n;
    if (!n || end > 0xFFFFFFFFu) return PC_PUT_GAP;
    if (off == cur) return PC_PUT_WRITE;
    if (end <= cur) return PC_PUT_HAVE;
    return PC_PUT_GAP;
}
