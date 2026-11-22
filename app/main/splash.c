// SPDX-License-Identifier: LGPL-2.1-or-later
/* The power-on picture: ONE JPEG from the card, drawn once on the system layer, "breathing" by the backlight ramp (pc/pc_splash.h). The picture is decoded by the
 * covers' route (jpeg_route.h: the JPEG engine for the common layouts, our PSRAM decoder for the rest), 1:1, no scaling; the decoded buffer is freed when the picture goes.
 * Runs in the boot front (boot_front.c) before any mode's own code, synchronously: splash_run returns when the one cycle is over and the picture is gone.
 * The ramp is ONE stepped implementation for both boards: the calling thread sets bsp_display_backlight_set(pc_splash_level(clock)) about 60 times a second (nothing holds
 * the display lock meanwhile). No LEDC fade service: its ISR and helpers sit in internal RAM, which is full (the trade-off notes, 開機畫面的背光呼吸). */
#include "splash.h"
#include <stdio.h>
#include <string.h>
#include "bsp/bsp_display.h"
#include "cfg.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_route.h"
#include "pc/pc_paths.h"
#include "pc/pc_splash.h"
#include "sdkconfig.h"

#define STEP_MS 16u          /* about 60 brightness steps a second: ample for a 10-bit PWM and for the FakePod's 0x51 command */


EXT_RAM_BSS_ATTR static struct {
    uint16_t *px;
    int w, h;
    lv_image_dsc_t dsc;
    lv_obj_t *bg;
    int64_t t0_us;
    uint8_t user;
    bool prepared;
} s;

static void none(const char *why) { printf("APP splash none (%s)\n", why); }

static uint8_t *read_file(const char *path, uint32_t *len, const char **why)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = NULL;
    long n = (fseek(f, 0, SEEK_END) == 0) ? ftell(f) : -1;
    if (n <= 0 || (unsigned long)n > PC_SPLASH_MAX_FILE) *why = n <= 0 ? "empty file" : "file too big";
    else if (fseek(f, 0, SEEK_SET) != 0 || !(b = heap_caps_malloc((size_t)n, MALLOC_CAP_SPIRAM))) *why = "memory";
    else if (fread(b, 1, (size_t)n, f) != (size_t)n) { heap_caps_free(b); b = NULL; *why = "read error"; }
    else *len = (uint32_t)n;
    fclose(f);
    return b;
}

bool splash_prepare(const char *mode)
{
    memset(&s, 0, sizeof s);
    if (cfg_int("splash", 1) == 0) return false;                       /* the off switch: nothing, not even a log line */
    if (!g_sd_ok) { none("no card"); return false; }
    if (cfg_brightness() <= 0) { none("brightness 0"); return false; }
    char path[64];
    const char *why = "no file";
    uint8_t *buf = NULL;
    uint32_t len = 0;
    for (int i = 0; !buf && pc_splash_path(mode, i, path, sizeof path); i++) {   /* the mode's own picture, then the general one */
        const char *w = NULL;
        buf = read_file(path, &len, &w);
        if (w) { why = w; break; }                                       /* the file is there but unusable: no second try with the other */
    }
    if (!buf) { none(why); return false; }
    jpeg_route_init();                                                  /* the engine, once (thumb_svc_start's call is then a no-op) */
    pc_jpeg_info_t in;
    int route = pc_jpeg_probe(buf, len, &in);
    uint16_t *px = NULL;
    int w = 0, h = 0;
    if (route != PC_JPEG_NATIVE && route != PC_JPEG_OWN) why = pc_jpeg_route_name(route);   /* progressive / unsupported / bad */
    else if ((uint64_t)in.w * (uint64_t)in.h * 2u > JPEG_HW_MAX_BYTES) why = "too big";
    else {
        why = "decode failed";
        if (route == PC_JPEG_NATIVE) {
            const char *hw_why = NULL;
            px = jpeg_route_hw(buf, len, &in, &w, &h, &hw_why);
        }
        if (!px) {                                                      /* OWN, or a NATIVE picture the engine refused: our decoder, 1:1 (shift 0) */
            int rc;
            px = jpeg_route_own(buf, len, 1 << 20, &w, &h, &rc);
            if (!px) why = rc == PC_JPEG_E_NOMEM ? "memory" : rc == PC_JPEG_E_TOO_BIG ? "too big" : "decode failed";
        }
    }
    heap_caps_free(buf);
    if (!px) { none(why); return false; }
    s.px = px; s.w = w; s.h = h;
    s.prepared = true;
    s.user = (uint8_t)cfg_brightness();
    printf("APP splash file %s\n", path);
    return true;
}

static void drop(void)
{
    if (s.px) heap_caps_free(s.px);
    s.px = NULL;
    s.prepared = false;
}

void splash_drop(void) { drop(); }
bool splash_shown(void) { return s.bg != NULL; }

void splash_run(lv_display_t *disp)
{
    if (!s.prepared) return;
    if (!bsp_display_lock(-1)) { drop(); return; }
    s.dsc = (lv_image_dsc_t){ .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = (uint32_t)s.w, .h = (uint32_t)s.h, .stride = (uint32_t)s.w * 2 },
                              .data_size = (uint32_t)s.w * (uint32_t)s.h * 2, .data = (const uint8_t *)s.px };
    s.bg = lv_obj_create(lv_layer_sys());                               /* above every page, outside the pages' leak census */
    lv_obj_remove_style_all(s.bg);
    lv_obj_set_size(s.bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s.bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s.bg, LV_OPA_COVER, 0);
    lv_obj_t *im = lv_image_create(s.bg);                               /* 1:1, centred; a bigger picture is cropped by the screen */
    lv_image_set_src(im, &s.dsc);
    lv_obj_center(im);
    bsp_display_backlight_set(0);                                       /* dark until the frame is out (already so on the board) */
    lv_refr_now(disp);
    bsp_display_unlock();
    printf("APP splash shown %dx%d\n", s.w, s.h);
    const int64_t t0 = esp_timer_get_time();
    int last = -1;
    for (;;) {                                                          /* the ramp: the clock says the level, a step ~60 times a second, only when it changed */
        uint32_t el = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        int v = pc_splash_level(el, s.user);
        if (v != last) { bsp_display_backlight_set((uint8_t)v); last = v; }
        if (el >= PC_SPLASH_TOTAL_MS) break;
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
    s.t0_us = t0;
    if (bsp_display_lock(-1)) { lv_obj_delete(s.bg); bsp_display_unlock(); }
    s.bg = NULL;
    drop();
    printf("APP splash done ms=%lu\n", (unsigned long)((esp_timer_get_time() - s.t0_us) / 1000));
}
