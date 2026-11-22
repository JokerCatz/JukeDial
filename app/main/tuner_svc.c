// SPDX-License-Identifier: LGPL-2.1-or-later
/* The tuner's listening service (tuner_svc.h). */
#include "tuner_svc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_mic.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "pc/pc_tuner.h"

#define RATE     16000
#define RING     2048
#define HOP      1024                    /* new samples per analysis */
#define HOLD_US  400000                  /* the last pitch stays this long after the sound stops */

typedef struct {
    int16_t ring[RING];                  /* the newest RING samples, oldest first after a shift */
    float scratch[PC_PITCH_SCRATCH];
    int fill;                            /* valid samples in ring */
    int fresh;                           /* samples since the last analysis */
    float last_hz[3];                    /* the last analyses (0 = none) */
    float shown_hz;
    int64_t voiced_us;                   /* when a pitch was last confirmed */
    tuner_reading_t r;
} tuner_t;
static tuner_t *s_t;
static int s_a4d;                        /* the A4 reference as an offset from 440 (zero-initialised: PSRAM .bss, not internal .data) */
#define s_a4 (PC_A4_DEFAULT + s_a4d)
static uint32_t s_reads, s_analyses, s_samples, s_last_us;

void tuner_set_a4(int hz) { s_a4d = (hz < PC_A4_MIN ? PC_A4_MIN : hz > PC_A4_MAX ? PC_A4_MAX : hz) - PC_A4_DEFAULT; }
int tuner_a4(void) { return s_a4; }
bool tuner_listening(void) { return s_t != NULL; }

bool tuner_start(void)
{
    if (s_t) return true;
    const size_t li = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), bi = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    s_t = heap_caps_calloc(1, sizeof *s_t, MALLOC_CAP_SPIRAM);
    if (!s_t) { printf("APP tuner no memory\n"); return false; }
    if (bsp_mic_start(RATE) != ESP_OK) { heap_caps_free(s_t); s_t = NULL; printf("APP tuner no mic\n"); return false; }
    s_reads = s_analyses = s_samples = 0;
    printf("APP tuner mic start internal_free_before=%u internal_free_after=%u largest_before=%u largest_after=%u\n", (unsigned)li,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)bi, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return true;
}

void tuner_stop(void)
{
    if (!s_t) return;
    bsp_mic_stop();
    heap_caps_free(s_t);
    s_t = NULL;
    printf("APP tuner mic stop internal_free=%u largest=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static float median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) b = c;
    return a > b ? a : b;
}
static bool close_semitone(float a, float b) { return a > 0 && b > 0 && fabsf(12.0f * log2f(a / b)) < 1.0f; }

static void analyse(void)
{
    tuner_t *t = s_t;
    pc_pitch_t p;
    const int64_t t0 = esp_timer_get_time();
    const bool ok = pc_pitch_detect(t->ring, t->fill, RATE, t->scratch, &p);
    s_last_us = (uint32_t)(esp_timer_get_time() - t0);
    s_analyses++;
    t->last_hz[0] = t->last_hz[1]; t->last_hz[1] = t->last_hz[2]; t->last_hz[2] = ok ? p.hz : 0.0f;
    t->r.rms = p.rms;
    if (close_semitone(t->last_hz[1], t->last_hz[2])) {
        t->shown_hz = t->last_hz[0] > 0 && close_semitone(t->last_hz[0], t->last_hz[2]) ? median3(t->last_hz[0], t->last_hz[1], t->last_hz[2]) : t->last_hz[2];
        t->voiced_us = esp_timer_get_time();
    }
    t->r.voiced = t->shown_hz > 0 && esp_timer_get_time() - t->voiced_us < HOLD_US;
    if (!t->r.voiced) t->shown_hz = 0;
    t->r.hz = t->shown_hz;
    if (t->r.voiced) t->r.midi = pc_note_from_hz(t->shown_hz, (float)s_a4, &t->r.cents);
    else t->r.cents = 0;
    t->r.seq++;
}

bool tuner_poll(tuner_reading_t *r)
{
    tuner_t *t = s_t;
    if (!t) return false;
    bool fresh = false;
    for (int guard = 0; guard < 8; guard++) {                        /* what the DMA queue holds now: at most 6 x 240 samples */
        size_t got = 0;
        int16_t blk[240];
        bsp_mic_read(blk, 240, &got, 0);
        s_reads++;
        if (!got) break;
        s_samples += (uint32_t)got;
        if (t->fill + (int)got > RING) {                              /* the oldest go */
            const int drop = t->fill + (int)got - RING;
            memmove(t->ring, t->ring + drop, (size_t)(t->fill - drop) * sizeof *t->ring);
            t->fill -= drop;
        }
        memcpy(t->ring + t->fill, blk, got * sizeof *blk);
        t->fill += (int)got;
        t->fresh += (int)got;
        if (got < 240) break;
    }
    if (t->fill >= PC_PITCH_NEED && t->fresh >= HOP) { t->fresh = 0; analyse(); fresh = true; }
    if (r) *r = t->r;
    return fresh;
}

void tuner_print(void)
{
    const tuner_t *t = s_t;
    char nm[8] = "-";
    if (t && t->r.voiced) pc_note_name(t->r.midi, nm, sizeof nm);
    printf("APP tuner on=%d a4=%d analyses=%u reads=%u samples=%u last_us=%u voiced=%d hz=%.2f note=%s cents=%.1f rms=%.0f\n", t != NULL, s_a4, (unsigned)s_analyses, (unsigned)s_reads,
           (unsigned)s_samples, (unsigned)s_last_us, t ? t->r.voiced : 0, t ? t->r.hz : 0.0f, nm, t ? t->r.cents : 0.0f, t ? t->r.rms : 0.0f);
}
