// SPDX-License-Identifier: LGPL-2.1-or-later
#include "jpeg_route.h"
#include <stdio.h>
#include <string.h>
#include "driver/jpeg_decode.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"

static jpeg_decoder_handle_t s_eng;

void jpeg_route_init(void)
{
    if (s_eng) return;
    size_t f0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    jpeg_decode_engine_cfg_t c = { .intr_priority = 0, .timeout_ms = 1000 };
    esp_err_t e = jpeg_new_decoder_engine(&c, &s_eng);
    if (e != ESP_OK) s_eng = NULL;
    printf("APP jpeghw init=%s int_cost=%ld int_free=%u int_big=%u\n", esp_err_to_name(e), (long)f0 - (long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}
bool jpeg_route_hw_ready(void) { return s_eng != NULL; }

uint16_t *jpeg_route_hw(const uint8_t *jpg, uint32_t len, const pc_jpeg_info_t *in, int *w, int *h, const char **why)
{
    *w = *h = 0;
    if (!s_eng) { *why = "no_engine"; return NULL; }
    bool gray = in->ncomp == 1;
    uint32_t pw32, ph32;                                     /* the engine pads to its MCU (luma factors; gray 1x1); 64-bit (fix round 3) */
    uint64_t need = pc_jpeg_hw_bytes(in, &pw32, &ph32);
    if (need > JPEG_HW_MAX_BYTES || (uint64_t)in->w * (uint64_t)in->h * 2u > JPEG_HW_MAX_BYTES) { *why = "too_big"; return NULL; }
    size_t pw = pw32, ph = ph32, bpp = gray ? 1 : 2;           /* all three fit: pw * ph * bpp <= 8 MB */
    jpeg_decode_memory_alloc_cfg_t mc = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
    size_t got = 0;
    uint8_t *buf = jpeg_alloc_decoder_mem(pw * ph * bpp, &mc, &got);
    if (!buf) { *why = "no_psram"; return NULL; }
    jpeg_decode_cfg_t dc = { .output_format = gray ? JPEG_DECODE_OUT_FORMAT_GRAY : JPEG_DECODE_OUT_FORMAT_RGB565,
                             .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR, .conv_std = JPEG_YUV_RGB_CONV_STD_BT601 };
    uint32_t outn = 0;
    if (jpeg_decoder_process(s_eng, &dc, jpg, len, buf, (uint32_t)got, &outn) != ESP_OK || outn != pw * ph * bpp) {
        heap_caps_free(buf);
        *why = "refused";
        return NULL;
    }
    uint16_t *px;
    if (gray) {                                              /* one byte per pixel: into RGB565 */
        px = heap_caps_malloc((size_t)in->w * in->h * 2, MALLOC_CAP_SPIRAM);
        if (!px) { heap_caps_free(buf); *why = "no_psram"; return NULL; }
        for (int y = 0; y < in->h; y++)
            for (int x = 0; x < in->w; x++) {
                uint8_t g = buf[(size_t)y * pw + x];
                px[(size_t)y * in->w + x] = (uint16_t)((g >> 3) << 11 | (g >> 2) << 5 | g >> 3);
            }
        heap_caps_free(buf);
    } else {                                                 /* the rows packed in place (each moves left or stays) */
        px = (uint16_t *)buf;
        if (pw != (size_t)in->w)
            for (int y = 1; y < in->h; y++) memmove(px + (size_t)y * in->w, px + (size_t)y * pw, (size_t)in->w * 2);
    }
    *w = in->w;
    *h = in->h;
    return px;
}

EXT_RAM_BSS_ATTR static void (*s_poll)(void);   /* a pointer, PSRAM: no new internal RAM */
void jpeg_route_set_poll(void (*poll)(void)) { s_poll = poll; }
static void *ps_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
static void ps_free(void *p) { heap_caps_free(p); }

uint16_t *jpeg_route_own(const uint8_t *jpg, uint32_t len, int min_short, int *w, int *h, int *rc)
{
    pc_jpeg_info_t in;
    pc_jpeg_probe(jpg, len, &in);
    const pc_jpeg_mem_t mem = { ps_alloc, ps_free, s_poll };
    uint16_t *px = NULL;
    *rc = pc_jpeg_decode(jpg, len, pc_jpeg_shift_for(in.w, in.h, min_short), &mem, &px, w, h);
    return px;
}
