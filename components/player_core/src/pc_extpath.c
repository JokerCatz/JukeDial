// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extpath.h"
#include <math.h>
#include <string.h>
#include "pc/pc_fxapp.h"
#include "pc/pc_fxchain.h"

struct pc_extpath {
    pc_extstream_t *s;
    const pc_extuac_t *u;
    pc_fxchain_t *chain;
    int gate;                 /* ready blocks seen (audio task) */
    volatile uint32_t cut;    /* blocks the final clamp changed */
};

pc_extpath_t *pc_extpath_create(void *(*alloc)(size_t), pc_extstream_t *s, const pc_extuac_t *u)
{
    int app = pc_fxapp_find("limiter");
    const pc_fxapp_t *const *tab;
    pc_fxapps(&tab);
    if (app < 0 || !alloc) return NULL;
    pc_extpath_t *p = alloc(sizeof *p);
    if (!p) return NULL;
    memset(p, 0, sizeof *p);
    p->s = s;
    p->u = u;
    p->chain = pc_fxchain_create(alloc);
    if (!p->chain) return NULL;
    float v[PC_FXP_MAX] = { 0 };
    for (int i = 0; i < tab[app]->nparam; i++) v[i] = tab[app]->param[i].def;   /* the defaults: ceiling -1 dB, release 120 ms, look-ahead 5 ms */
    size_t n = pc_fxchain_block_size(app, PC_EXTPATH_LIM_RATE, v);
    void *blk = n ? alloc(n) : NULL;
    if (!blk) return NULL;
    pc_fxchain_block_init(blk, app, PC_EXTPATH_LIM_RATE, v, 1.0f);
    pc_fxmsg_t m = { .op = PC_FXOP_SWAP, .app = (uint8_t)app, .ptr = blk };
    if (!pc_fxchain_post(p->chain, &m)) return NULL;                    /* taken by the first block the audio task runs (also an idle one) */
    return p;
}
void pc_extpath_destroy(pc_extpath_t *p, void (*release)(void *))
{
    if (!p) return;
    pc_fxchain_destroy(p->chain, release);
    release(p);
}

bool pc_extpath_ready(const pc_extpath_t *p)
{
    pc_fxchain_stat_t st;
    pc_fxchain_stat(p->chain, &st);
    return st.blocks >= 1 && !pc_fxchain_busy(p->chain) && p->gate >= PC_EXTPATH_GATE_BLOCKS;
}

int pc_extpath_render(float *lr, int frames, void *arg)
{
    pc_extpath_t *p = arg;
    if (frames <= 0) return frames;
    pc_fxchain_stat_t st;
    pc_fxchain_stat(p->chain, &st);
    bool limiter_in = st.blocks >= 1 && !pc_fxchain_busy(p->chain);
    if (!limiter_in) p->gate = 0;
    if (!limiter_in || p->gate < PC_EXTPATH_GATE_BLOCKS) {
        if (limiter_in) p->gate++;     /* not (yet) limited: nothing of the stream; what arrives meanwhile is dropped here (the reader discards it), so the first sound is fresh, not 35 ms old */
        pc_extstream_discard(p->s);
        memset(lr, 0, (size_t)frames * 2 * sizeof *lr);
        return frames;
    }
    return pc_extstream_render(p->s, lr, frames, pc_extuac_gain(p->u, 0), pc_extuac_gain(p->u, 1));
}

void pc_extpath_post(float *lr, int frames, uint32_t rate, int src, void *arg)
{
    pc_extpath_t *p = arg;
    if (src < 0) { pc_fxchain_idle(p->chain, lr, frames, rate, 0); return; }
    pc_fxchain_process(p->chain, lr, frames, rate, pc_fx_rate_mode(rate));   /* only the rate bits: the limiter runs at every rate (pc_fx_app_allowed) */
    bool cut = false;
    for (int i = 0; i < 2 * frames; i++) {
        float x = lr[i];
        if (!(x <= PC_EXTPATH_CEIL)) { lr[i] = x != x ? 0.0f : PC_EXTPATH_CEIL; cut = true; }       /* NaN -> 0 */
        else if (x < -PC_EXTPATH_CEIL) { lr[i] = -PC_EXTPATH_CEIL; cut = true; }
    }
    if (cut) p->cut++;
}
uint32_t pc_extpath_limited(const pc_extpath_t *p) { return p->cut; }
