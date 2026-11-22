// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_lowmem.h"

static int target(size_t f, size_t l)
{
    if (f < PC_LOWMEM_BACKGND_FREE) return PC_LOWMEM_NO_BACKGND;
    if (f < PC_LOWMEM_STILL_FREE) return PC_LOWMEM_STILL;
    if (f < PC_LOWMEM_COVER_FREE || l < PC_LOWMEM_COVER_BLOCK) return PC_LOWMEM_NO_COVER;
    return PC_LOWMEM_OK;
}

/* enough room to leave `level` for the one above it */
static bool clear_of(int level, size_t f, size_t l)
{
    if (level == PC_LOWMEM_NO_BACKGND) return f >= PC_LOWMEM_BACKGND_FREE + PC_LOWMEM_HYST;
    if (level == PC_LOWMEM_STILL) return f >= PC_LOWMEM_STILL_FREE + PC_LOWMEM_HYST;
    return f >= PC_LOWMEM_COVER_FREE + PC_LOWMEM_HYST && l >= PC_LOWMEM_COVER_BLOCK + PC_LOWMEM_HYST;
}

int pc_lowmem_update(pc_lowmem_t *m, size_t f, size_t l, uint32_t now)
{
    int t = target(f, l);
    if (t > m->level) { m->level = t; m->calm = false; return m->level; }
    if (m->level == PC_LOWMEM_OK) return m->level;
    if (!clear_of(m->level, f, l)) { m->calm = false; return m->level; }
    if (!m->calm) { m->calm = true; m->calm_since = now; return m->level; }
    if ((uint32_t)(now - m->calm_since) >= PC_LOWMEM_CALM_MS) { m->level--; m->calm_since = now; }   /* stays calm: the next level counts from here */
    return m->level;
}

bool pc_lowmem_big_ok(size_t f, size_t l) { return f >= PC_LOWMEM_COVER_FREE && l >= PC_LOWMEM_COVER_BLOCK; }
bool pc_lowmem_big_esp_ok(size_t f, size_t l) { return pc_lowmem_big_ok(f, l) && f >= PC_LOWMEM_COVER_FREE + PC_LOWMEM_BIG_ESP_PEAK; }
