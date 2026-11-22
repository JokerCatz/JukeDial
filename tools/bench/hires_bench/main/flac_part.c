// SPDX-License-Identifier: LGPL-2.1-or-later
/* FLAC decode cost (hires_bench part "flac"). Same dr_flac as the player (components/player_core/third_party/dr_flac,
 * the defines of pc_backend_flac.c:4-6, the project's -O2 "perf" build), same reader shape (16 KB RBUF filled by memcpy,
 * on_read/on_seek/on_tell copied from pc_backend_flac.c:30-57), heap like the player (malloc > 1 KB -> PSRAM).
 * Fixture: host-encoded (gen_fixtures.py) in the "fix" partition, copied to PSRAM before any timing.
 * Timed region = one drflac_read_pcm_frames_f32(fs/100 frames) call (decode-only), or that call plus the 2:1
 * decimator over its output (path=dec2: the player's 31-tap Blackman FIR at dec 2, code of pc_backend_flac.c:59-73,
 * 126-151). path=today: the player's current backend decimation to <= 48 kHz (dec 4 for 192k, 8 for 384k).
 * Pass 1 warms caches and is reported only as max_cold; pass 2 (seek to 0) gives min/avg/max. */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "dr_flac.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_private/esp_clk.h"

uint32_t hb_cpu_mhz(void);

#define RBUF 16384
#define TAPS_PER 16
#define MAXTAPS (8 * TAPS_PER)

typedef struct {
    const uint8_t *src; uint32_t size;    /* the whole file in PSRAM (stands in for pc_reader_t) */
    uint32_t off;
    uint8_t *buf;
    uint32_t buf_off, buf_len;
} rd_t;

static size_t on_read(void *u, void *out, size_t n)
{
    rd_t *f = u;
    size_t done = 0;
    while (done < n) {
        if (f->off < f->buf_off || f->off >= f->buf_off + f->buf_len) {
            if (f->off >= f->size) break;
            uint32_t got = f->size - f->off < RBUF ? f->size - f->off : RBUF;
            memcpy(f->buf, f->src + f->off, got);         /* stands in for r.read() from TF */
            f->buf_off = f->off;
            f->buf_len = got;
        }
        uint32_t at = f->off - f->buf_off, k = f->buf_len - at;
        if (k > n - done) k = (uint32_t)(n - done);
        memcpy((uint8_t *)out + done, f->buf + at, k);
        done += k;
        f->off += k;
    }
    return done;
}
static drflac_bool32 on_seek(void *u, int offset, drflac_seek_origin origin)
{
    rd_t *f = u;
    int64_t to = origin == DRFLAC_SEEK_SET ? offset : origin == DRFLAC_SEEK_CUR ? (int64_t)f->off + offset : (int64_t)f->size + offset;
    if (to < 0 || to > f->size) return DRFLAC_FALSE;
    f->off = (uint32_t)to;
    return DRFLAC_TRUE;
}
static drflac_bool32 on_tell(void *u, drflac_int64 *pos) { *pos = ((rd_t *)u)->off; return DRFLAC_TRUE; }

typedef struct { float coef[MAXTAPS], hist[2][MAXTAPS]; int taps, hpos, phase, dec; } fir_t;
static void fir_init(fir_t *f, int dec)    /* pc_backend_flac.c fir_init(), outside any timed region */
{
    memset(f, 0, sizeof *f);
    f->dec = dec;
    f->taps = dec * TAPS_PER - 1;
    double fc = 0.45 / dec, sum = 0;
    int m = f->taps - 1;
    for (int i = 0; i < f->taps; i++) {
        double x = i - m / 2.0, s = x == 0 ? 2 * fc : sin(2 * M_PI * fc * x) / (M_PI * x);
        double w = 0.42 - 0.5 * cos(2 * M_PI * i / m) + 0.08 * cos(4 * M_PI * i / m);
        f->coef[i] = (float)(s * w);
        sum += s * w;
    }
    for (int i = 0; i < f->taps; i++) f->coef[i] = (float)(f->coef[i] / sum);
}
static inline float clip(float v) { return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v; }
/* the per-frame loop of pc_backend_flac.c flac_render() for dec > 1 (stereo) */
static int fir_run(fir_t *f, const float *in, int n, float *out)
{
    int done = 0;
    for (int k = 0; k < n; k++) {
        f->hist[0][f->hpos] = in[2 * k];
        f->hist[1][f->hpos] = in[2 * k + 1];
        f->hpos = (f->hpos + 1) % f->taps;
        if (++f->phase < f->dec) continue;
        f->phase = 0;
        float a = 0, b = 0;
        for (int i = 0, j = f->hpos; i < f->taps; i++, j = j + 1 == f->taps ? 0 : j + 1) {
            a += f->coef[i] * f->hist[0][j];
            b += f->coef[i] * f->hist[1][j];
        }
        out[2 * done] = clip(a);
        out[2 * done + 1] = clip(b);
        done++;
    }
    return done;
}

typedef struct { char name[24]; uint32_t fs, bits, level, bs, off, size; } ent_t;

static void run_file(const ent_t *e, const uint8_t *data)
{
    static const char *pname[3] = { "decode", "dec2", "today" };
    uint32_t mhz = esp_clk_cpu_freq() / 1000000;
    for (int path = 0; path < 3; path++) {
        int dec = path == 0 ? 1 : path == 1 ? 2 : 1;
        if (path == 2) { while (e->fs / dec > 48000 && e->fs % (dec * 2) == 0 && dec < 8) dec *= 2; }
        if (path > 0 && e->fs <= 96000) continue;           /* decimator rows only for 192k / 384k */
        rd_t r = { .src = data, .size = e->size };
        size_t i0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), p0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        r.buf = malloc(RBUF);
        size_t i1 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), p1 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        drflac *fl = drflac_open(on_read, on_seek, on_tell, &r, NULL);
        size_t i2 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), p2 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (!fl) { printf("HB flac %s open failed\n", e->name); free(r.buf); return; }
        int fr = (int)(fl->sampleRate / 100), ch = fl->channels;
        float *pcm = malloc((size_t)fr * ch * sizeof(float));
        float *dout = malloc((size_t)fr * ch * sizeof(float));
        fir_t *fir = malloc(sizeof *fir);
        size_t i3 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (!pcm || !dout || !fir || ch != 2) { printf("HB flac %s alloc/ch failed\n", e->name); goto done; }
        if (path == 0)
            printf("HB flacram name=%s fs=%" PRIu32 " bits=%u ch=%d maxblock=%u total_frames=%" PRIu64
                   " drflac_open: internal=%d psram=%d (sizeof(drflac)=%u) rbuf: internal=%d psram=%d pcm10ms+out+fir internal=%d\n",
                   e->name, (uint32_t)fl->sampleRate, fl->bitsPerSample, ch, fl->maxBlockSizeInPCMFrames,
                   (uint64_t)fl->totalPCMFrameCount, (int)(i1 - i2), (int)(p1 - p2), (unsigned)sizeof(drflac),
                   (int)(i0 - i1), (int)(p0 - p1), (int)(i2 - i3));
        uint32_t cmin = UINT32_MAX, cmax = 0, cold = 0;
        uint64_t csum = 0, frames = 0;
        uint32_t chk = 0;
        int nblk = 0;
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 1) { drflac_seek_to_pcm_frame(fl, 0); if (dec > 1) fir_init(fir, dec); }
            else if (dec > 1) fir_init(fir, dec);
            for (;;) {
                uint32_t t0 = esp_cpu_get_cycle_count();
                drflac_uint64 n = drflac_read_pcm_frames_f32(fl, fr, pcm);
                if (dec > 1 && n) fir_run(fir, pcm, (int)n, dout);
                uint32_t c = esp_cpu_get_cycle_count() - t0;
                if (!n) break;
                if (pass == 0) { if (c > cold) cold = c; continue; }
                if ((int)n < fr) { frames += n; break; }   /* the short tail block is not a 10 ms block */
                frames += n;
                nblk++;
                csum += c;
                if (c < cmin) cmin = c;
                if (c > cmax) cmax = c;
                chk = chk * 31u + (uint32_t)(int32_t)(pcm[fr] * 8388608.0f);
            }
        }
        double avg = (double)csum / nblk / mhz, mn = (double)cmin / mhz, mx = (double)cmax / mhz;
        printf("HB flac fs=%" PRIu32 " bits=%u lvl=%" PRIu32 " us_per_10ms=%.0f/%.0f/%.0f core_pct=%.2f | path=%s dec=%d bs=%" PRIu32
               " blocks=%d frames=%" PRIu64 " max_cold_us=%.0f peak_pct=%.1f file_kbps=%.0f chk=%08" PRIx32 "\n",
               e->fs, fl->bitsPerSample, e->level, mn, avg, mx, avg / 100.0, pname[path], dec, e->bs, nblk, frames,
               (double)cold / mhz, mx / 100.0, e->size * 8.0 / 4.0 / 1000.0, chk);
    done:
        free(fir);
        free(dout);
        free(pcm);
        drflac_close(fl);
        free(r.buf);
    }
}

typedef struct { SemaphoreHandle_t done; } fa_t;
static void flac_task(void *p)
{
    fa_t *a = p;
    printf("HB flac task core=%d cpu %" PRIu32 " MHz (measured)\n", (int)esp_cpu_get_core_id(), hb_cpu_mhz());
    const esp_partition_t *pt = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fix");
    uint8_t hdr[8];
    if (!pt || esp_partition_read(pt, 0, hdr, 8) != ESP_OK || memcmp(hdr, "HBFX", 4)) {
        printf("HB flac no fixture blob in partition 'fix'\n");
        goto out;
    }
    uint32_t n;
    memcpy(&n, hdr + 4, 4);
    for (uint32_t i = 0; i < n && i < 16; i++) {
        ent_t e;
        esp_partition_read(pt, 8 + i * sizeof(ent_t), &e, sizeof e);   /* 48 B entries (gen_fixtures.py '<24s6I') */
        e.name[23] = 0;
        uint8_t *d = heap_caps_malloc(e.size, MALLOC_CAP_SPIRAM);
        if (!d || esp_partition_read(pt, e.off, d, e.size) != ESP_OK) { printf("HB flac %s load failed\n", e.name); free(d); continue; }
        run_file(&e, d);
        free(d);
    }
out:
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

void flac_part(void)
{
    fa_t a = { .done = xSemaphoreCreateBinary() };
    xTaskCreatePinnedToCore(flac_task, "flac", 16384, &a, 10, NULL, 1);   /* core 1 = the player's audio task core */
    xSemaphoreTake(a.done, portMAX_DELAY);
}
