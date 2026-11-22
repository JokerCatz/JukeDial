// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_clkmeas.h"
#include <string.h>

void pc_clkmeas_reset(pc_clkmeas_t *m, uint8_t desc, uint32_t now)
{
    memset(m, 0, sizeof *m);
    m->desc = desc;
    m->t0 = m->t1 = now;
}

static void learn(pc_clkmeas_t *m, const void *buf, uint32_t t)
{
    m->learned = 0;
    m->n = 0;
    m->bufs[m->learned++] = buf;
    if (m->learned == m->desc) { m->n = 1; m->t0 = m->t1 = t; m->last = 0; }   /* a one-descriptor ring */
}

void pc_clkmeas_event(pc_clkmeas_t *m, const void *buf, uint32_t t)
{
    if (!m->desc || m->desc > PC_CLKMEAS_MAX_DESC) return;
    if (m->learned < m->desc) {                          /* learning the ring order: the buffers in the order they finish */
        for (uint8_t i = 0; i < m->learned; i++)
            if (m->bufs[i] == buf) { learn(m, buf, t); return; }   /* a repeat before the ring is complete: an event was lost meanwhile, start again */
        m->bufs[m->learned++] = buf;
        if (m->learned == m->desc) { m->n = 1; m->t0 = m->t1 = t; m->last = (uint8_t)(m->desc - 1); }
        return;
    }
    uint8_t idx = 0xff;
    for (uint8_t i = 0; i < m->desc; i++) if (m->bufs[i] == buf) { idx = i; break; }
    if (idx == 0xff) { learn(m, buf, t); return; }       /* not in the ring: it is not the ring we learned */
    uint8_t d = (uint8_t)((idx + m->desc - m->last) % m->desc);
    m->n += d ? d : m->desc;                              /* the same descriptor again: a whole lap */
    m->last = idx;
    m->t1 = t;
}

uint32_t pc_clkmeas_rate(const pc_clkmeas_t *m, uint32_t n_from, uint32_t t_from, uint32_t frames, uint32_t min_n)
{
    uint32_t dt = m->t1 - t_from;
    if (!m->n || m->n < n_from || m->n - n_from < min_n || !dt || !frames) return 0;
    return (uint32_t)((uint64_t)(m->n - n_from) * frames * 1000000u / dt);
}

bool pc_clkmeas_close(uint32_t want, uint32_t got, uint32_t dt_us)
{
    if (!want || !got || !dt_us) return false;
    uint32_t d = got > want ? got - want : want - got;
    uint64_t allow = want / 100u + (uint64_t)want * 6000u / dt_us;
    return d <= allow;
}
