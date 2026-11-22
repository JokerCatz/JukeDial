// SPDX-License-Identifier: LGPL-2.1-or-later
/* app/main/jpeg_route.c without the S31's JPEG engine (esp_driver_jpeg): no engine, so a NATIVE picture goes to our decoder (the same fallback the board takes when the
 * engine refuses it). jpeg_route_own is the firmware's, copied line for line. */
#include "jpeg_route.h"
#include <stdlib.h>
#include "esp_heap_caps.h"

void jpeg_route_init(void) {}
bool jpeg_route_hw_ready(void) { return false; }
uint16_t *jpeg_route_hw(const uint8_t *jpg, uint32_t len, const pc_jpeg_info_t *in, int *w, int *h, const char **why)
{
    (void)jpg; (void)len; (void)in;
    *w = *h = 0;
    *why = "no_engine";
    return NULL;
}
static void *ps_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
static void ps_free(void *p) { heap_caps_free(p); }
static void (*s_poll)(void);
void jpeg_route_set_poll(void (*poll)(void)) { s_poll = poll; }
uint16_t *jpeg_route_own(const uint8_t *jpg, uint32_t len, int min_short, int *w, int *h, int *rc)
{
    pc_jpeg_info_t in;
    pc_jpeg_probe(jpg, len, &in);
    const pc_jpeg_mem_t mem = { ps_alloc, ps_free, s_poll };
    uint16_t *px = NULL;
    *rc = pc_jpeg_decode(jpg, len, pc_jpeg_shift_for(in.w, in.h, min_short), &mem, &px, w, h);
    return px;
}
