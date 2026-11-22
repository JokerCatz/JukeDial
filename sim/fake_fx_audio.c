// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes fx_audio (the post hook of audio_svc on the audio task). */
/* Behaviour: the real chain (pc_fxchain_process) runs from a 10 ms lv_timer on 480 frames of silence at 48000 Hz with the current mode bits, so
 * the mailbox is drained, blocks are installed and handed back exactly as on the board. Like the board's hook it runs only while a source
 * plays (audio_svc_status says AUD_PLAYING) or pc_fxchain_busy; every message it takes prints
 * "APP fx apply op=<set|swap|off|mix> app=<id> param=<n> v=<v>". */
/* Empty: nothing of the audio path (the source is silence, the output is dropped). */
#include <stdio.h>
#include <string.h>
#include "audio_svc.h"
#include "fx_audio.h"
#include "fx_state.h"
#include "lvgl.h"

static pc_fxchain_t *s_chain;

static void trace(const pc_fxmsg_t *m, void *u)
{
    (void)u;
    static const char *const OP[] = { "?", "set", "swap", "off", "mix" };
    const pc_fxapp_t *const *a;
    int n = pc_fxapps(&a);
    printf("APP fx apply op=%s app=%s param=%d v=%g\n", m->op < 5 ? OP[m->op] : "?", m->app < n ? a[m->app]->id : "?", m->param, (double)m->v);
}

static void run(lv_timer_t *t)
{
    (void)t;
    aud_status_t st;
    audio_svc_status(&st);
    static float silence[2 * PC_FX_BLOCK];
    memset(silence, 0, sizeof silence);
    if (st.state != AUD_PLAYING) pc_fxchain_idle(s_chain, silence, PC_FX_BLOCK, 48000, fx_mode_bits());   /* the board's rule (fx_audio.c): idle blocks are bookkeeping, output zeroed */
    else pc_fxchain_process(s_chain, silence, PC_FX_BLOCK, 48000, fx_mode_bits());
}

void fx_audio_start(pc_fxchain_t *c)
{
    s_chain = c;
    pc_fxchain_set_trace(c, trace, NULL);
    lv_timer_create(run, 10, NULL);
}
