// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes album_svc (the photo slideshow builder on core 0). */
/* Behaviour: three ready "photos" (360x360 test frames drawn by sim_cover.c from "photo <idx>"); a load is answered in LVGL context (next step), the newest wins. */
/* Empty: the card scan, building, waiting reasons, stat, clear. */
#include <stddef.h>
#include <stdio.h>
#include "esp_heap_caps.h"
#include "album_svc.h"
#include "sim.h"
#include "ui.h"

#define PHOTOS 3

static struct { bool active; uint32_t req; int idx; album_cb cb; void *u; } s_pend;
static uint32_t s_req;

static void deliver(void *arg)
{
    (void)arg;
    if (!s_pend.active) return;
    s_pend.active = false;
    uint16_t *px = NULL;
    if (s_pend.idx >= 0 && s_pend.idx < PHOTOS && (px = heap_caps_malloc((size_t)ALBUM_PX * ALBUM_PX * sizeof *px, MALLOC_CAP_SPIRAM))) {
        char key[24];
        snprintf(key, sizeof key, "photo %d", s_pend.idx);
        sim_cover_draw(key, ALBUM_PX, px);
    }
    if (s_pend.cb) s_pend.cb(s_pend.req, px, s_pend.u);
}

void album_svc_start(bool soon) { (void)soon; }
void album_svc_wake(void) {}
int album_svc_ready(void) { return PHOTOS; }
int album_svc_total(void) { return PHOTOS; }
bool album_svc_building(void) { return false; }
const char *album_svc_waiting(void) { return NULL; }
uint32_t album_svc_load(int idx, album_cb cb, void *u)
{
    s_pend.active = true;
    s_pend.req = ++s_req;
    s_pend.idx = idx;
    s_pend.cb = cb;
    s_pend.u = u;
    ui_async(deliver, NULL);
    return s_pend.req;
}
void album_svc_stat(void) {}
void album_svc_clear(void) {}
