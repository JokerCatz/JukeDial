// SPDX-License-Identifier: LGPL-2.1-or-later
/* LC3 encode cost on the ESP32-S31 (2026-11-13): the player's Auracast encoder path (esp_audio_codec LC3, one encoder
 * per channel, 16-bit mono in, 10 ms frames, nbyte per channel = 120 for 48_4 / 155 for 48_6 / 60 for 24_2), run on
 * core 1 and then core 0 (the player's comment says LC3 needs core 1 for PIE, but its log prints `cast task core=0`). 2 s per case; signals synthesised on the board:
 * sine (1 kHz L / 440 Hz R, -6 dBFS), music (same music-like generator as tools/bench/ldac_bench), silence.
 * Prints average and worst us per channel per frame and % of one core for both channels. */
#include <math.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include "esp_audio_enc.h"
#include "esp_audio_enc_reg.h"
#include "esp_lc3_enc.h"

static uint32_t rng = 1;
static inline float noise(void) { rng = rng * 1664525u + 1013904223u; return (int32_t)rng * (1.0f / 2147483648.0f); }
static float lp[2];
static float music(int n, int sf, int ch)   /* copy of tools/bench/ldac_bench/main/main.c music() */
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
    lp[ch] += 0.33f * (noise() - lp[ch]);
    v += 0.05f * lp[ch];
    float tt = fmodf(t + (ch ? 0.125f : 0.0f), 0.5f);
    if (tt < 0.1f) v += 0.32f * noise() * expf(-tt / 0.02f);
    return v;
}

static SemaphoreHandle_t done;
static const char *sig_name[3] = { "sine", "music", "silence" };

static void bench(int sf, int nbyte, int sig)
{
    esp_audio_enc_handle_t enc[2] = { 0 };
    for (int c = 0; c < 2; c++) {
        esp_lc3_enc_config_t lc = ESP_LC3_ENC_CONFIG_DEFAULT();
        lc.sample_rate = (uint32_t)sf;
        lc.channel = 1;
        lc.frame_dms = 100;
        lc.nbyte = (uint16_t)nbyte;
        esp_audio_enc_config_t ec = { .type = ESP_AUDIO_TYPE_LC3, .cfg = &lc, .cfg_sz = sizeof lc };
        if (esp_audio_enc_open(&ec, &enc[c]) != ESP_AUDIO_ERR_OK) { printf("LC3 open failed %d/%d\n", sf, nbyte); return; }
    }
    static int16_t ch[2][480];
    static uint8_t out[512];
    int fr = sf / 100, frames = 200, bytes = 0, errs = 0;
    int64_t sum = 0, mx = 0;
    rng = 1; lp[0] = lp[1] = 0;
    for (int f = 0; f < frames; f++) {
        for (int i = 0; i < fr; i++) {
            int n = f * fr + i;
            for (int c = 0; c < 2; c++) {
                float v = sig == 0 ? 0.5f * sinf(2 * (float)M_PI * (c ? 440.0f : 1000.0f) * n / sf) : sig == 1 ? music(n, sf, c) : 0.0f;
                v *= 32767.0f;
                ch[c][i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
        }
        for (int c = 0; c < 2; c++) {
            esp_audio_enc_in_frame_t in = { .buffer = (uint8_t *)ch[c], .len = (uint32_t)(fr * 2) };
            esp_audio_enc_out_frame_t o = { .buffer = out, .len = sizeof out };
            int64_t t0 = esp_timer_get_time();
            esp_audio_err_t er = esp_audio_enc_process(enc[c], &in, &o);
            int64_t e = esp_timer_get_time() - t0;
            if (er != ESP_AUDIO_ERR_OK) errs++;
            bytes += (int)o.encoded_bytes;
            sum += e;
            if (e > mx) mx = e;
        }
    }
    printf("LC3 core%d %-7s %5d Hz %3d B/ch (%3d kbps/ch): avg %5lld us/ch/frame, max %5lld us, stereo %5.1f %% of a core, out %d B/frame/ch, err %d\n",
           (int)esp_cpu_get_core_id(), sig_name[sig], sf, nbyte, nbyte * 8 * 100 / 1000, (long long)(sum / (frames * 2)), (long long)mx,
           sum / (frames * 100.0), bytes / (frames * 2), errs);
    for (int c = 0; c < 2; c++) esp_audio_enc_close(enc[c]);
}

static uint32_t cpu_mhz(void)
{
    int64_t t0 = esp_timer_get_time();
    uint32_t c0 = esp_cpu_get_cycle_count();
    while (esp_timer_get_time() - t0 < 200000) { }
    uint32_t c1 = esp_cpu_get_cycle_count();
    return (uint32_t)((uint64_t)(c1 - c0) / (uint64_t)(esp_timer_get_time() - t0));
}

static void task(void *arg)
{
    printf("LC3 bench (esp_audio_codec %s), core %d, measured CPU clock %lu MHz, CONFIG %d MHz\n", "2.6.2",
           (int)esp_cpu_get_core_id(), (unsigned long)cpu_mhz(), CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    static bool reg;
    if (!reg) { esp_lc3_enc_register(); reg = true; }
    for (int sig = 0; sig < 3; sig++) {
        bench(48000, 120, sig);
        bench(48000, 155, sig);
        bench(24000, 60, sig);
    }
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}

void app_main(void)
{
    done = xSemaphoreCreateBinary();
    for (int core = 1; core >= 0; core--) {   /* core 1 first; core 0 too (the player's cast task reports core=0) */
        xTaskCreatePinnedToCore(task, "lc3", 16384, NULL, 5, NULL, core);
        xSemaphoreTake(done, portMAX_DELAY);
    }
    printf("LC3 bench done\n");
}
