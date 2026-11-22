// SPDX-License-Identifier: LGPL-2.1-or-later
/* The audio half of the fx service: the post hook of audio_svc calls the chain with the current mode bits. Everything the chain needs on
 * this task (mailbox drain, fades, the mode rule, state blocks) is inside pc_fxchain_process; nothing is allocated, freed or printed here.
 * audio_svc hands blocks of pc_block_frames(rate) frames at the pipeline rate: up to 3840 at 384 kHz (hires T13), which the chain runs as eight
 * PC_FX_BLOCK (480) blocks; an app whose fits() refuses the rate is skipped (and asks for a rebuild), as at any rate it cannot take. */
#include "fx_audio.h"
#include "audio_svc.h"
#include "fx_state.h"
#include "bt_svc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_private/esp_clk.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* called for every block, also while nothing plays (src < 0, x is silent): then the chain runs only while pc_fxchain_busy and its output is discarded (pc_fxchain_idle), so the mailbox
 * is drained, fades finish and blocks go back to the LVGL side with playback stopped or paused, and an idle block costs one slot scan */
static void post(float *x, int frames, uint32_t rate, int src, void *u)
{
    pc_fxchain_t *c = u;
    /* the rate bits come from this block's own rate (no fx_mode_set here: it logs and hops to the LVGL task, H28); the LVGL tick mirrors the same bits for looks and the log */
    if (src < 0) { pc_fxchain_idle(c, x, frames, rate, fx_mode_bits()); return; }   /* nothing plays: no rate bits (the tick drops them too) */
    uint8_t mode = (uint8_t)(fx_mode_bits() | pc_fx_rate_mode(rate));   /* bookkeeping only: the output of such a block is zeroed */
    pc_fxchain_process(c, x, frames, rate, mode);
}

void fx_audio_start(pc_fxchain_t *c)
{
    audio_svc_set_post(post, c);
}

/* ---- fxbench (spec §11 M1): each app alone through the app contract (state_size / init / process), not through the live chain
 * (the chain's own header/mix/fade/slot-scan cost is not in the numbers). Cycles are counted around process() only; the yields (vTaskDelay)
 * and the noise generation are outside the timed region. ---- */
#define BENCH_SECS 2
#define BENCH_TAIL_SECS 2       /* the measured tail run */
#define BENCH_TAIL_MAX_S 12     /* the silence before it: until the output peak is below -700 dBFS, at most this long */
#define BENCH_TAIL_FLOOR 1e-35f /* -700 dBFS: below this a float is (almost) denormal (the range starts near 1e-38, -760 dBFS) */
#define BENCH_STACK (6 * 1024)   /* internal RAM; the apps' state is heap, nothing big on the stack */
#define BENCH_YIELD_BLOCKS 50    /* a vTaskDelay(1) every 50 blocks (~0.5 s of audio): IDLE1 (task watchdog) runs even while an app costs ~100 % */

/* SplitMix64-style generator (the finaliser of SplitMix64, input seed ^ k*golden), seed 0x5EED0001, uniform +-0.5. NOT byte-identical to
 * the vector inputs of tools/gen/fx_vectors.py (that one adds the golden step; this one XORs): the cost of the apps does not depend on it. */
static uint64_t sm_hash(uint64_t seed, uint64_t count)
{
    uint64_t z = seed ^ (count * 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* the value at 0.7 of the range: LOG geometric, CHOICE/BOOL by index, linear snapped to the step grid */
static float bench_p07(const pc_fxparam_t *p)
{
    float lo = p->lo, hi = p->hi;
    if (p->flags & PC_FXP_LOG) return lo * powf(hi / lo, 0.7f);
    if (p->unit == PC_FXU_CHOICE || p->unit == PC_FXU_BOOL) return lo + roundf(0.7f * (hi - lo));
    float v = lo + 0.7f * (hi - lo);
    if (p->step > 0) v = lo + roundf((v - lo) / p->step) * p->step;
    return v < lo ? lo : v > hi ? hi : v;
}

typedef struct { uint64_t cycles; uint32_t worst; float peak; } bench_res_t;   /* worst: slowest block except the first (cold cache); peak: output peak of the run (modes 1 and 2) */
enum { BM_NOISE, BM_IMPULSE /* one unit impulse in the first block, then silence */, BM_SILENCE };

/* one run of nblocks blocks of `blk` frames in mode BM_*; the output peak is recorded for the two silence modes */
static void bench_run(const pc_fxapp_t *a, void *st, float *buf, int blk, int nblocks, int mode, bench_res_t *r)
{
    uint64_t total = 0, k = 0;
    uint32_t w = 0;
    float peak = 0;
    for (int b = 0; b < nblocks; b++) {
        for (int i = 0; i < 2 * blk; i++) buf[i] = mode == BM_NOISE ? (float)(sm_hash(0x5EED0001ull, k++) >> 40) / 16777216.0f - 0.5f : (mode == BM_IMPULSE && b == 0 && i < 2 ? 1.0f : 0.0f);
        uint32_t t0 = esp_cpu_get_cycle_count();
        a->process(st, buf, blk);
        uint32_t d = esp_cpu_get_cycle_count() - t0;
        total += d;
        if (b > 0 && d > w) w = d;
        if (mode != BM_NOISE) for (int i = 0; i < 2 * blk; i++) { float m = fabsf(buf[i]); if (m > peak) peak = m; }
        if (b % BENCH_YIELD_BLOCKS == BENCH_YIELD_BLOCKS - 1) vTaskDelay(1);
    }
    r->cycles = total;
    r->worst = w;
    r->peak = peak;
}

static double bench_pct(uint64_t cycles, uint32_t hz, int secs) { return 100.0 * (double)cycles / ((double)secs * (double)hz); }

/* state block from `caps`, init, one noise run, free. false: the block did not fit */
static bool bench_state(const pc_fxapp_t *a, uint32_t rate, const float *v, uint32_t caps, float *buf, int blk, bench_res_t *r)
{
    void *st = heap_caps_aligned_alloc(16, a->state_size(rate, v), caps);
    if (!st) return false;
    a->init(st, rate, v);
    bench_run(a, st, buf, blk, (int)((uint32_t)BENCH_SECS * rate / (uint32_t)blk), BM_NOISE, r);
    heap_caps_free(st);
    vTaskDelay(1);   /* after every run: IDLE1 gets its turn */
    return true;
}

/* the denormal tail (REVERB family): one impulse, then silence in 0.5 s windows until the window's peak is below -700 dBFS (or exactly 0) or
 * BENCH_TAIL_MAX_S passed (*reach_s = -1: not reached); then BENCH_TAIL_SECS of silence are measured (cycles around process() only), r->peak = their peak */
static bool bench_tail(const pc_fxapp_t *a, uint32_t rate, const float *v, float *buf, int blk, bench_res_t *r, float *reach_s)
{
    void *st = heap_caps_aligned_alloc(16, a->state_size(rate, v), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!st) return false;
    a->init(st, rate, v);
    int win = (int)(rate / 2 / (uint32_t)blk);   /* 0.5 s: 50 blocks */
    float t = 0;
    *reach_s = -1;
    for (int w = 0; w < 2 * BENCH_TAIL_MAX_S; w++) {
        bench_res_t x;
        bench_run(a, st, buf, blk, win, w == 0 ? BM_IMPULSE : BM_SILENCE, &x);
        t += 0.5f;
        vTaskDelay(1);
        if (x.peak < BENCH_TAIL_FLOOR) { *reach_s = t; break; }
    }
    bench_run(a, st, buf, blk, (int)((uint32_t)BENCH_TAIL_SECS * rate / (uint32_t)blk), BM_SILENCE, r);
    heap_caps_free(st);
    vTaskDelay(1);
    return true;
}

/* why the bench must not run now (NULL = nothing streams): a decode or encode task on core 1 would be starved by a task above it */
static const char *bench_blocked(void)
{
    aud_status_t st;
    audio_svc_status(&st);
    if (st.state == AUD_PLAYING || st.state == AUD_PAUSED || st.state == AUD_NEAR || st.state == AUD_NEXT || st.state == AUD_FADED) return "playing";
    bt_mode_t m = bt_svc_mode();
    if (m == BT_MODE_BROADCAST || m == BT_MODE_LISTEN) return "cast";
    if (m == BT_MODE_HEADPHONES) return "bt";
    return NULL;
}

static void bench_field(char *b, size_t n, bool ok, double v) { if (ok) snprintf(b, n, "%.2f", v); else snprintf(b, n, "-"); }

static volatile bool s_bench_busy;   /* the bench's only static: one run at a time */

static void bench_task(void *arg)
{
    char *want = arg;   /* fx_bench's PSRAM copy of the name, "" = all */
    const pc_fxapp_t *const *tab;
    int n = pc_fxapps(&tab);
    uint32_t hz = (uint32_t)esp_clk_cpu_freq();
    size_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    /* the work buffer lives where the live one does (audio_svc.c: PSRAM); the int= run moves it and the state to internal RAM */
    _Static_assert(PC_FX_BLOCK == 480, "fxbench: the work buffers below hold PC_FX_BLOCK frames; pc_fx_bench_block caps every call at it");
    float *pbuf = heap_caps_malloc(2 * PC_FX_BLOCK * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    float *ibuf = heap_caps_malloc(2 * PC_FX_BLOCK * sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const char *why = NULL;
    for (int i = 0; i < n && !why; i++) {
        const pc_fxapp_t *a = tab[i];
        if (*want && strcmp(want, a->id)) continue;
        float def[PC_FXP_MAX], v07[PC_FXP_MAX], tl[PC_FXP_MAX];
        for (int p = 0; p < a->nparam; p++) { def[p] = a->param[p].def; v07[p] = bench_p07(&a->param[p]); tl[p] = def[p]; }
        if (a->fam == PC_FXFAM_REVERB) {   /* the tail at the shortest decay so the signal falls fast into the denormal range (freeverb: room at its minimum) */
            for (int p = 0; p < a->nparam; p++) if (!strncmp(a->param[p].id, "decay", 5) || (!strcmp(a->id, "freeverb") && !strcmp(a->param[p].id, "room"))) tl[p] = a->param[p].lo;
        }
        static const uint32_t RATES[] = { 48000, 44100, 96000, 192000, 384000 };
        for (int r = 0; r < (int)(sizeof RATES / sizeof RATES[0]) && !why; r++) {
            uint32_t rate = RATES[r];
            int blk = pc_fx_bench_block(rate);   /* 441 / 480 frames; above 48 kHz the same 480-frame calls the chain makes (the buffers hold PC_FX_BLOCK frames) */
            if (!pbuf) { printf("APP fxbench app=%s rate=%u alloc failed: work buffer (psram)\n", a->id, (unsigned)rate); continue; }
            bench_res_t rd = { 0 }, rp = { 0 }, ri = { 0 }, rt = { 0 };
            float reach = -1;
            bool okd, okp, oki = false, okt = false;
            if ((why = bench_blocked())) break;
            okd = bench_state(a, rate, def, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, pbuf, blk, &rd);
            if (!okd) { printf("APP fxbench app=%s rate=%u alloc failed: state def (psram)\n", a->id, (unsigned)rate); continue; }
            if ((why = bench_blocked())) break;
            okp = bench_state(a, rate, v07, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, pbuf, blk, &rp);
            if (!okp) printf("APP fxbench app=%s rate=%u alloc failed: state p07 (psram)\n", a->id, (unsigned)rate);
            if (!ibuf) printf("APP fxbench app=%s rate=%u alloc failed: ibuf\n", a->id, (unsigned)rate);
            else if (!(why = bench_blocked())) oki = bench_state(a, rate, def, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, ibuf, blk, &ri);   /* does not fit: int=- (velvet_room) */
            if (a->fam == PC_FXFAM_REVERB && !why && !(why = bench_blocked())) {
                okt = bench_tail(a, rate, tl, pbuf, blk, &rt, &reach);
                if (!okt) printf("APP fxbench app=%s rate=%u alloc failed: state tail (psram)\n", a->id, (unsigned)rate);
            }
            uint32_t w = rd.worst;
            if (okp && rp.worst > w) w = rp.worst;
            if (oki && ri.worst > w) w = ri.worst;
            char fd[16], fp[16], fi[16], ft[16], fl[16], fr[16];
            bench_field(fd, sizeof fd, okd, bench_pct(rd.cycles, hz, BENCH_SECS));
            bench_field(fp, sizeof fp, okp, bench_pct(rp.cycles, hz, BENCH_SECS));
            bench_field(fi, sizeof fi, oki, bench_pct(ri.cycles, hz, BENCH_SECS));
            bench_field(ft, sizeof ft, okt, bench_pct(rt.cycles, hz, BENCH_TAIL_SECS));
            bench_field(fl, sizeof fl, okt, rt.peak > 0 ? 20.0 * log10((double)rt.peak) : -999.0);
            if (!okt) snprintf(fr, sizeof fr, "-"); else if (reach < 0) snprintf(fr, sizeof fr, ">%d", BENCH_TAIL_MAX_S); else snprintf(fr, sizeof fr, "%.1f", (double)reach);
            printf("APP fxbench app=%s rate=%u def=%s p07=%s int=%s tail=%s tail_reach_s=%s tail_min_db=%s blk_max_us=%u mhz=%u blk_us=%u\n", a->id, (unsigned)rate, fd, fp, fi, ft, fr, fl,
                   (unsigned)((uint64_t)w * 1000000u / hz), (unsigned)(hz / 1000000u), (unsigned)((uint64_t)blk * 1000000u / rate));
        }
    }
    if (why) printf("APP fxbench aborted: %s\n", why);
    heap_caps_free(pbuf);
    heap_caps_free(ibuf);
    heap_caps_free(want);
    printf("APP fxbench heap free_before=%u free_after=%u psram_before=%u psram_after=%u\n", (unsigned)free_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)psram_before, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("APP fxbench done\n");
    s_bench_busy = false;
    vTaskDelete(NULL);
}

/* The instrument is a separate boot mode (console_app_cmd; no fx chain, no fxbench command), so it cannot be running in the player boot. */
void fx_bench(const char *id)
{
    char name[24];
    while (id && *id == ' ') id++;
    size_t len = id ? strlen(id) : 0;
    while (len && id[len - 1] == ' ') len--;
    if (len >= sizeof name) { printf("APP fxbench unknown app=%.23s\n", id); return; }
    memcpy(name, id ? id : "", len);
    name[len] = 0;
    const char *why = bench_blocked();
    if (why) { printf("APP fxbench refused: %s\n", why); return; }
    if (name[0] && pc_fxapp_find(name) < 0) { printf("APP fxbench unknown app=%s\n", name); return; }
    if (s_bench_busy) { printf("APP fxbench refused: running\n"); return; }
    char *arg = heap_caps_malloc(sizeof name, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);   /* not strdup: under 1 KB that would be internal RAM (final review M4) */
    if (!arg) return;
    memcpy(arg, name, sizeof name);
    s_bench_busy = true;
    /* core 1 like the audio task, one priority below it; with nothing streaming (bench_blocked) it competes with no stream */
    if (xTaskCreatePinnedToCore(bench_task, "fxbench", BENCH_STACK, arg, configMAX_PRIORITIES - 3, NULL, 1) != pdPASS) { heap_caps_free(arg); printf("APP fxbench refused: no task\n"); s_bench_busy = false; }
}
