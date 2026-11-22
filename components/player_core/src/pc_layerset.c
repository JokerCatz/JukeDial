// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_layerset.h"
#include <string.h>

void pc_layerset_init(pc_layerset_t *s, int nlayers, int max)
{
    memset(s, 0, sizeof *s);
    s->n = nlayers < 0 ? 0 : nlayers > PC_SCENE_MAX_LAYERS ? PC_SCENE_MAX_LAYERS : nlayers;
    s->max = max;
    for (int i = 0; i < s->n && i < max; i++) s->on[i] = true;
}

int pc_layerset_count(const pc_layerset_t *s)
{
    int k = 0;
    for (int i = 0; i < s->n; i++) k += s->on[i];
    return k;
}

int pc_layerset_toggle(pc_layerset_t *s, int i)
{
    if (i < 0 || i >= s->n) return -1;
    if (s->on[i]) { s->on[i] = false; return PC_LS_OFF; }
    if (pc_layerset_count(s) >= s->max) return PC_LS_FULL;
    s->on[i] = true;
    return PC_LS_ON;
}

int pc_layerset_add(pc_layerset_t *s)
{
    if (s->n >= PC_SCENE_MAX_LAYERS) return -1;
    int i = s->n++;
    s->on[i] = false;
    s->on[i] = pc_layerset_count(s) < s->max;
    return i;
}

void pc_layerset_set(pc_layerset_t *s, int i, bool on)
{
    if (i >= 0 && i < s->n) s->on[i] = on;
}
