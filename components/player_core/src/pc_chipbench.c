// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_chipbench.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc/pc_rate.h"

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void pc_chipbench_run(const pc_backend_t *be, const void *data, uint32_t size, int sub, int secs,
                      const pc_chipbench_env_t *env, pc_chipbench_t *o)
{
    memset(o, 0, sizeof *o);
    o->alloc_open = o->alloc_play = -1;
    if (!be || !data || !size || secs < 1 || secs > 3600 || !env || !env->now_us) { o->rc = -3; return; }
    pc_reader_t r;
    pc_reader_mem(&r, data, size);
    if (env->ext) pc_reader_set_ext(&r, env->ext);
    uint32_t a0 = env->alloc_small ? env->alloc_small() : 0;
    void *h = NULL;
    if (be->open(&h, &r, 0) || !h) { o->rc = -1; return; }
    if (sub > 0 && (!be->start_track || be->start_track(h, sub))) { be->close(h); o->rc = -2; return; }
    uint32_t a1 = env->alloc_small ? env->alloc_small() : 0;
    uint32_t rate = be->rate(h);
    int bf = pc_block_frames(rate);
    int total = (int)((uint64_t)secs * rate / (uint64_t)bf);       /* blocks in `secs` seconds */
    float *pcm = malloc(sizeof(float) * 2 * (size_t)bf);
    uint32_t *us = malloc(sizeof(uint32_t) * (size_t)total);
    if (!pcm || !us) { free(pcm); free(us); be->close(h); o->rc = -4; return; }
    o->rate = rate;
    o->block_frames = bf;
    uint32_t budget = (uint32_t)((uint64_t)PC_CHIPBENCH_BUDGET_US * (uint64_t)bf * 100 / rate);   /* 10 ms at rate/100 frames; scales for odd rates */
    uint64_t sum = 0;
    uint32_t fnv = 2166136261u;
    int run = 0;
    int n = 0;
    for (; n < total; n++) {
        uint64_t t0 = env->now_us();
        int got = be->render(h, pcm, bf);
        uint64_t t1 = env->now_us();
        if (got < 0) { o->render_err = 1; break; }
        if (got == 0) { o->ended = 1; break; }
        uint32_t d = (uint32_t)(t1 - t0);
        us[n] = d;
        sum += d;
        if (d > budget) { o->over_budget++; if (++run > o->max_run_over) o->max_run_over = run; } else run = 0;
        int nz = 0;
        for (int i = 0; i < 2 * got; i++) {
            float f = pcm[i] * 32768.0f;
            int s = (int)(f < 0 ? f - 0.5f : f + 0.5f);
            if (s > 32767) s = 32767;
            if (s < -32768) s = -32768;
            nz |= s;
            fnv = (fnv ^ (uint8_t)s) * 16777619u;
            fnv = (fnv ^ (uint8_t)(s >> 8)) * 16777619u;
        }
        if (!nz) o->silent_blocks++;
        if (env->block_hook) env->block_hook(env->hook_u);
    }
    uint32_t a2 = env->alloc_small ? env->alloc_small() : 0;
    be->close(h);
    o->blocks = n;
    o->checksum = fnv;
    if (env->alloc_small) { o->alloc_open = (int32_t)(a1 - a0); o->alloc_play = (int32_t)(a2 - a1); }
    if (n > 0) {
        o->avg_us = (uint32_t)(sum / (uint64_t)n);
        qsort(us, (size_t)n, sizeof *us, cmp_u32);
        o->p50_us = us[n / 2];
        o->p99_us = us[(int)((int64_t)n * 99 / 100) >= n ? n - 1 : (int)((int64_t)n * 99 / 100)];
        o->max_us = us[n - 1];
        /* real time of the rendered audio = n blocks of bf / rate seconds */
        uint64_t audio_us = (uint64_t)n * (uint64_t)bf * 1000000u / rate;
        o->rt_pct10 = audio_us ? (uint32_t)(sum * 1000u / audio_us) : 0;
    }
    free(pcm);
    free(us);
}

int pc_chipbench_fmt(const pc_chipbench_t *r, char *buf, int n)
{
    return snprintf(buf, (size_t)n, "rc=%d rate=%u blocks=%d avg_us=%u p50_us=%u p99_us=%u max_us=%u rt_pct=%u.%u over10=%d maxrun=%d silent=%d ended=%d alloc_open=%d alloc_play=%d sum=%08x",
                    r->rc, (unsigned)r->rate, r->blocks, (unsigned)r->avg_us, (unsigned)r->p50_us, (unsigned)r->p99_us, (unsigned)r->max_us,
                    (unsigned)(r->rt_pct10 / 10), (unsigned)(r->rt_pct10 % 10), r->over_budget, r->max_run_over, r->silent_blocks, r->ended,
                    (int)r->alloc_open, (int)r->alloc_play, (unsigned)r->checksum);
}
