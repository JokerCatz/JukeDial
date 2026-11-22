// SPDX-License-Identifier: LGPL-2.1-or-later
#include "library_svc.h"
#include "pc/pc_paths.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "cfg.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mp3_ctl.h"
#include "thumb_svc.h"
#include "ui.h"

pc_library_t g_lib;
void (*library_on_scanned)(void);
static volatile bool s_scanning;
static volatile int s_done;
static pc_library_t s_new;

/* the scan's name lists and a shuffle pool's weights are big blocks: PSRAM, never one small block per name in internal RAM */
static void *big_realloc(void *p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
static void init_mem(void) { pc_bigmem_set(big_realloc, heap_caps_free); }

static unsigned s_gen;
unsigned library_gen(void) { return s_gen; }
bool library_scanning(void) { return s_scanning; }
int library_scan_done(void) { return s_done; }

void library_svc_load(void)
{
    init_mem();
    int64_t t0 = esp_timer_get_time();
    int rc = g_sd_ok ? pc_library_load(&g_lib, LIB_INDEX) : -ENOENT;
    s_gen++;
    printf("APP library loaded rc=%d n=%d folders=%d ms=%lld\n", rc, g_lib.n, g_lib.nfolders, (esp_timer_get_time() - t0) / 1000);
    if (!rc) thumb_svc_library_changed();         /* the cover cache follows the index (a failed load prunes nothing) */
    struct stat st;
    if (rc && g_sd_ok && !stat(LIB_EXPORT, &st)) { printf("APP library rescan reason=index rc=%d\n", rc); library_rescan(); }
}

static void progress(int done, void *u) { (void)u; s_done = done; vTaskDelay(1); }

static int s_rc;
static void swap_cb(void *arg)
{
    (void)arg;
    if (!s_rc) {                                  /* a failed scan keeps the library we had */
        mp3_ctl_library_changed(&s_new);         /* hires T8e: playback goes on, its indices follow into the new library */
        pc_library_free(&g_lib);
        g_lib = s_new;
        s_gen++;
        memset(&s_new, 0, sizeof s_new);
        thumb_svc_library_changed();             /* the cover pack follows the new library in the background (built while idle) */
    }
    s_scanning = false;
    if (library_on_scanned) library_on_scanned();
}

static void scan_task(void *arg)
{
    (void)arg;
    int64_t t0 = esp_timer_get_time();
    int rc = s_rc = pc_library_scan(LIB_EXPORT, &s_new, progress, NULL);
    pc_int_mkdir(PC_INDEX_DIR);
    int sv = rc ? rc : pc_library_save(&s_new, LIB_INDEX);
    printf("APP scan n=%d folders=%d rc=%d save=%d ms=%lld\n", s_new.n, s_new.nfolders, rc, sv, (esp_timer_get_time() - t0) / 1000);
    if (s_new.skipped_big) printf("APP lib big files skipped=%u\n", (unsigned)s_new.skipped_big);
    ui_async(swap_cb, NULL);
    vTaskDelete(NULL);
}

bool library_rescan(void)
{
    if (s_scanning || !g_sd_ok) return false;
    init_mem();
    s_scanning = true;
    s_done = 0;
    if (xTaskCreatePinnedToCore(scan_task, "scan", 8192, NULL, 2, NULL, 0) != pdPASS) { s_scanning = false; return false; }
    return true;
}
