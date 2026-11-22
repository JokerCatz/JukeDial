// SPDX-License-Identifier: LGPL-2.1-or-later
/* LDAC encoder cost on the ESP32-S31 (feasibility, user 2026-11-10; extended 2026-11-13): encode 2 s of stereo audio
 * at each quality and rate, report the share of one core it takes. Real time needs well under 100 %.
 * The peer is a bare board (no TF): every signal is synthesised here.
 *   sine  = the 2026-11-10 signal (1 kHz, L 12000 peak, R half), generated inside the timed loop as before
 *           ("loop" column = the old number, sine generation included) and also timed around ldacBT_encode only;
 *   music = music-like, different per channel, generated per 128-sample block outside the timer:
 *           L: notes every 250 ms (C4 E4 G4 B4 D5 cycle), each 4 harmonics (1/k amplitude) x 2 voices detuned
 *              +-0.35 %, attack 5 ms / exp. decay tau 180 ms; peak about -8 dBFS;
 *           R: notes every 375 ms (A2 E3 A3 C4 cycle), 6 harmonics, same detune, decay tau 300 ms;
 *           both: low-passed white noise (one-pole, fc ~ 3 kHz, about -32 dBFS) + a transient every 500 ms
 *              (white-noise burst, 20 ms exp. decay, -10 dBFS; R offset by 125 ms);
 *   silence = digital zero.
 * "enc" = time inside ldacBT_encode / 2 s of audio; "max" = slowest single ldacBT_encode call (one call = 128 samples,
 * 2.9 ms at 44.1k, 1.3 ms at 96k); "crc" = FNV-1a of all output bytes (the LDAC_PROF build must print the same).
 * Each sweep runs once in a task pinned to core 0 and once pinned to core 1. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "ldacBT.h"

#ifdef LDAC_PROF
extern uint32_t ldac_prof_cyc[8];
static const char *prof_names[8] = { "set_pcm", "mdct", "frame_status", "norm_spectrum", "alloc_bits",
                                     "quant_spectrum", "quant_residual", "pack" };
#endif

static uint32_t rng = 1;
static uint32_t mhz_now = 320;   /* set from the timed loop before each sweep */
static inline float noise(void) { rng = rng * 1664525u + 1013904223u; return (int32_t)rng * (1.0f / 2147483648.0f); }

static const char *sig_name[3] = { "sine", "music", "silence" };

/* one sample of the music-like signal: n = sample index, ch = 0/1 */
static float lp[2];
static inline float music(int n, int sf, int ch)
{
    static const float fl[5] = { 261.63f, 329.63f, 392.0f, 493.88f, 587.33f };
    static const float fr[4] = { 110.0f, 164.81f, 220.0f, 261.63f };
    float t = (float)n / sf;
    float per = ch ? 0.375f : 0.25f, tau = ch ? 0.30f : 0.18f;
    int note = (int)(t / per);
    float tn = t - note * per;
    float f0 = ch ? fr[note % 4] : fl[note % 5];
    int nh = ch ? 6 : 4;
    float env = (tn < 0.005f ? tn / 0.005f : 1.0f) * expf(-tn / tau);
    float v = 0;
    for (int k = 1; k <= nh; k++) {
        float f = f0 * k;
        if (f > 0.45f * sf) break;
        v += (sinf(2 * (float)M_PI * f * 1.0035f * tn) + sinf(2 * (float)M_PI * f * 0.9965f * tn)) / k;
    }
    v *= 0.16f * env;
    lp[ch] += 0.33f * (noise() - lp[ch]);                 /* one-pole low-pass, ~3 kHz at 48k */
    v += 0.05f * lp[ch];
    float tt = fmodf(t + (ch ? 0.125f : 0.0f), 0.5f);    /* transient */
    if (tt < 0.1f) v += 0.32f * noise() * expf(-tt / 0.02f);
    return v;
}

static void bench(int sf, int eqmid, const char *label, int mix)
{
    HANDLE_LDAC_BT h = ldacBT_get_handle();
    size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (!h || ldacBT_init_handle_encode(h, 679, eqmid, LDACBT_CHANNEL_MODE_STEREO, LDACBT_SMPL_FMT_S16, sf)) {
        printf("LDAC %s %d Hz: init failed err=0x%x\n", label, sf, h ? ldacBT_get_error_code(h) : -1);
        if (h) ldacBT_free_handle(h);
        return;
    }
    size_t used = before - heap_caps_get_free_size(MALLOC_CAP_8BIT);
    static int16_t pcm[2 * LDACBT_ENC_LSU];
    static unsigned char out[1024];
    double ph = 0;
    int total = sf * 2, done = 0, bytes = 0, frames = 0;
    int64_t enc_us = 0, max_us = 0;
    uint32_t crc = 2166136261u;
    rng = 1; lp[0] = lp[1] = 0;
#ifdef LDAC_PROF
    for (int i = 0; i < 8; i++) ldac_prof_cyc[i] = 0;
#endif
    int64_t t0 = esp_timer_get_time();
    while (done < total) {
        if (!mix) {
            for (int i = 0; i < LDACBT_ENC_LSU; i++) { int16_t v = (int16_t)(12000 * sin(ph)); pcm[2 * i] = v; pcm[2 * i + 1] = (int16_t)(v / 2); ph += 2 * M_PI * 1000.0 / sf; }
        } else if (mix == 1) {
            for (int i = 0; i < LDACBT_ENC_LSU; i++) {
                for (int c = 0; c < 2; c++) {
                    float v = music(done + i, sf, c) * 32767.0f;
                    pcm[2 * i + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                }
            }
        } else {
            for (int i = 0; i < 2 * LDACBT_ENC_LSU; i++) pcm[i] = 0;
        }
        int used_b = 0, sz = 0, fn = 0;
        int64_t e0 = esp_timer_get_time();
        int r = ldacBT_encode(h, pcm, &used_b, out, &sz, &fn);
        int64_t e = esp_timer_get_time() - e0;
        if (r) { printf("LDAC encode error 0x%x\n", ldacBT_get_error_code(h)); break; }
        enc_us += e;
        if (e > max_us) max_us = e;
        for (int i = 0; i < sz; i++) { crc ^= out[i]; crc *= 16777619u; }
        done += used_b / 4;
        bytes += sz;
        frames += fn;
    }
    int64_t us = esp_timer_get_time() - t0;
    printf("LDAC core%d %-7s %-2s %6d Hz: loop %5.1f %%  enc %5.1f %% of a core, max %4lld us/call, %4d kbps, heap %u B, crc %08lx\n",
           (int)esp_cpu_get_core_id(), sig_name[mix], label, sf, mix ? 0.0 : us / 20000.0, enc_us / 20000.0,
           (long long)max_us, bytes * 8 / 2000, (unsigned)used, (unsigned long)crc);
#ifdef LDAC_PROF
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += ldac_prof_cyc[i];
    printf("  stages (%% of a core / %% of enc):");
    for (int i = 0; i < 8; i++)
        printf(" %s %.1f/%.0f", prof_names[i], ldac_prof_cyc[i] / (mhz_now * 2e6) * 100, sum ? 100.0 * ldac_prof_cyc[i] / sum : 0.0);
    printf("\n");
#endif
    ldacBT_free_handle(h);
}

static uint32_t cpu_mhz(void)   /* timed loop: cycle counter against esp_timer over 200 ms */
{
    int64_t t0 = esp_timer_get_time();
    uint32_t c0 = esp_cpu_get_cycle_count();
    while (esp_timer_get_time() - t0 < 200000) { }
    uint32_t c1 = esp_cpu_get_cycle_count();
    int64_t t1 = esp_timer_get_time();
    return (uint32_t)((uint64_t)(c1 - c0) / (uint64_t)(t1 - t0));
}

static SemaphoreHandle_t done_sem;

static void sweep_task(void *arg)
{
    static const int rates[3] = { 44100, 48000, 96000 };
    mhz_now = cpu_mhz();
    printf("core %d: measured CPU clock %lu MHz\n", (int)esp_cpu_get_core_id(), (unsigned long)mhz_now);
    for (int mix = 0; mix < 3; mix++) {
        for (int s = 0; s < 3; s++) {
            bench(rates[s], LDACBT_EQMID_HQ, "HQ", mix);
            bench(rates[s], LDACBT_EQMID_SQ, "SQ", mix);
            bench(rates[s], LDACBT_EQMID_MQ, "MQ", mix);
        }
    }
    xSemaphoreGive(done_sem);
    vTaskDelete(NULL);
}

void app_main(void)
{
    printf("LDAC bench, libldac version 0x%x, %s%s, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=%d\n", ldacBT_get_version(),
#ifdef _32BIT_FIXED_POINT
           "fixed point",
#else
           "floating point",
#endif
#ifdef LDAC_PROF
           " (LDAC_PROF stage timers)",
#else
           "",
#endif
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    done_sem = xSemaphoreCreateBinary();
    for (int core = 0; core < 2; core++) {
        xTaskCreatePinnedToCore(sweep_task, "ldac", 32768, NULL, 5, NULL, core);
        xSemaphoreTake(done_sem, portMAX_DELAY);
    }
    printf("LDAC bench done\n");
}
