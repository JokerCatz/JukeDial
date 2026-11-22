// SPDX-License-Identifier: LGPL-2.1-or-later
/* aptX / aptX HD cost on the ESP32-S31 (user 2026-11-10: support first, trade off only when measured).
 * Encode 2 s of a 24-bit stereo sine at 44.1 and 48 kHz, then decode it back; report the share of one core. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freeaptx.h"

#define CHUNK 1024   /* samples per channel per call */

static void bench(int hd, int sf)
{
    const char *name = hd ? "aptX HD" : "aptX";
    size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    struct aptx_context *e = aptx_init(hd), *d = aptx_init(hd);
    if (!e || !d) { printf("%s: init failed\n", name); return; }
    size_t used = before - heap_caps_get_free_size(MALLOC_CAP_8BIT);
    int total = sf * 2;
    unsigned char *pcm = heap_caps_malloc((size_t)total * 6, MALLOC_CAP_SPIRAM);
    unsigned char *enc = heap_caps_malloc((size_t)total * 2, MALLOC_CAP_SPIRAM);
    unsigned char *dec = heap_caps_malloc((size_t)total * 6 + 4096, MALLOC_CAP_SPIRAM);
    static unsigned char cin[CHUNK * 6], cout[CHUNK * 2];   /* internal RAM, like a real send path */
    if (!pcm || !enc || !dec) { printf("%s: no PSRAM\n", name); goto out; }
    for (int i = 0; i < total; i++)
        for (int c = 0; c < 2; c++) {
            int v = (int)(sin(2 * M_PI * (c ? 1000.0 : 440.0) * i / sf) * 0.5 * 8388607);
            unsigned char *p = pcm + i * 6 + c * 3; p[0] = v; p[1] = v >> 8; p[2] = v >> 16;
        }
    size_t elen = 0, w;
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < total; i += CHUNK) {
        int n = total - i < CHUNK ? total - i : CHUNK;
        memcpy(cin, pcm + (size_t)i * 6, (size_t)n * 6);
        aptx_encode(e, cin, (size_t)n * 6, cout, sizeof cout, &w);
        memcpy(enc + elen, cout, w); elen += w;
    }
    int64_t te = esp_timer_get_time() - t0;
    size_t dlen = 0; int synced = 0; size_t dropped = 0;
    t0 = esp_timer_get_time();
    aptx_decode_sync(d, enc, elen, dec, (size_t)total * 6 + 4096, &dlen, &synced, &dropped);
    int64_t td = esp_timer_get_time() - t0;
    double se = 0, sn = 0;
    for (int i = 2000; i < total - 2000 && (size_t)(i + 1) * 6 <= dlen; i++) {
        int a = (int)((unsigned)pcm[i * 6] | pcm[i * 6 + 1] << 8 | pcm[i * 6 + 2] << 16) << 8 >> 8;
        int b = (int)((unsigned)dec[i * 6] | dec[i * 6 + 1] << 8 | dec[i * 6 + 2] << 16) << 8 >> 8;
        se += (double)a * a; sn += (double)(a - b) * (a - b);
    }
    printf("%-7s %5d Hz: encode %5.1f %% of a core, decode %5.1f %%, %4u kbps, SNR %.1f dB, heap %u B, synced %d\n",
           name, sf, te / 20000.0, td / 20000.0, (unsigned)(elen * 8 / 2000), sn > 0 ? 10 * log10(se / sn) : 999.0,
           (unsigned)used, synced);
out:
    free(pcm); free(enc); free(dec);
    aptx_finish(e); aptx_finish(d);
}

void app_main(void)
{
    printf("aptX bench (libfreeaptx), CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=%d\n", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    static const int rates[2] = { 44100, 48000 };
    for (int s = 0; s < 2; s++) { bench(0, rates[s]); bench(1, rates[s]); }
    printf("aptX bench done\n");
}
