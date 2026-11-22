// SPDX-License-Identifier: LGPL-2.1-or-later
#include "console.h"
#include "crashnet.h"
#include "out_status.h"
#include "speed_svc.h"
#include "cpu_clock.h"
#include "lab.h"
#include "lowmem.h"
#include "stats_svc.h"
#include "gamesave_svc.h"
#include "drink_svc.h"
#include <math.h>
#include <stdio.h>
#include "esp_log.h"
#include "wifi_svc.h"
#include "sysmode.h"
#include "radio_svc.h"
#include "ctl.h"
#include "color_noise.h"
#include "usb_mode.h"
#include "power_svc.h"
#include "clock_svc.h"
#include <time.h>
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "bsp/bsp_display.h"
#include "bsp/bsp_geom.h"
#include "imu_svc.h"
#include "bt_meta.h"
#include "cfg.h"
#include "vol_out.h"
#include "esp_attr.h"
#ifndef SIM
#include "driver/usb_serial_jtag.h"
#if CONFIG_ESP_CONSOLE_UART
#include "driver/uart.h"
#endif
#include "driver/usb_serial_jtag_vfs.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifndef SIM
#include "freertos/idf_additions.h"
#endif
#include "lvgl.h"
#include "memlog.h"
#include "mp3_ctl.h"
#include "library_svc.h"
#include "screens.h"
#include "ui_ring.h"
#include "audio_svc.h"
#include "bsp/bsp_audio.h"
#include "bsp/bsp_rtc.h"
#include "pc/pc_dacmax.h"
#include "pc/pc_rate.h"
#include "pc/pc_vmute.h"
#include "pc/pc_voice.h"
#include "fx_audio.h"
#include "chip_ctl.h"
#include "backend_gme.h"
#ifndef SIM
#include "chipbench_ctl.h"
#endif
#include "noise_engine.h"
#include "bt_svc.h"
#include "test_tone.h"
#include "rate_test.h"
#include "card_test.h"
#include "draw/snapshot/lv_snapshot.h"
#include "esp_timer.h"
#include "bsp/bsp_haptic.h"
#include "thumb_svc.h"
#include "album_svc.h"
#include "ui/album_view.h"
#include "ui/ui_theme.h"
#include "ui/i18n.h"
#include "ui/now_menu.h"
#include "ui/now_fp.h"
#include "ui/ui_ninegrid.h"
#include "ui/ui_axis.h"
#include "ui/ui_kit.h"
#include "ui/ui_bubbles.h"
#include "ui/ui_exit.h"
#include "ui/ui_frame.h"
#include "ui/ui_qs.h"
#include "ui/ui_tour.h"
#include "ui/ui_arena.h"
#include "ui/ui_cards.h"
#include "ui/ui_dial.h"
#include "ui/ui_beat.h"
#include "ui/ui_board.h"
#include "ui/ui_sectors.h"
#include "ui/games.h"
#include "ui/ui_code_pad.h"
#include "ui/ui_safe.h"
#include "ui/ui_popring.h"
#include "fx_state.h"
#include "sleep_timer.h"
#include "player_ctl.h"
#include "instrument.h"
#include "cast_mode.h"

bool (*console_app_cmd)(const char *c);
#ifndef SIM
#include "psa/crypto.h"
#include "crashlog.h"
#include "bt_remote.h"
#endif
#include "pc/pc_fxapp.h"
#include "pc/pc_put.h"
#include "pc/pc_io.h"
#include "ui/glyph_text.h"
#include "ui/ui.h"
#include "freertos/semphr.h"
#include "pc/pc_upload.h"
#include "ui/font.h"
#include "psram_alloc.h"
#include "sfx.h"

/* test aid (2026-11-09): the screen as RGB565 over the console, for checking UI changes off-site (tools/simtools/shot.py) */
static void shot(void)
{
    lv_draw_buf_t *b = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!b) { printf("SHOT failed\n"); return; }
    static const char T64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int w = b->header.w, h = b->header.h, stride = b->header.stride;
    char line[(BSP_LCD_H_RES * 2 + 2) / 3 * 4 + 1];   /* one base64 line per row: w * 2 bytes, 4 chars per 3 bytes (360 px: 960 + NUL; 460 px: 1228 + NUL) */
    if (((w * 2 + 2) / 3) * 4 + 1 > (int)sizeof line) { printf("SHOT failed\n"); lv_draw_buf_destroy(b); return; }   /* a snapshot wider than the panel: never overrun */
    printf("SHOT begin %d %d\n", w, h);
    for (int y = 0; y < h; y++) {
        const uint8_t *p = b->data + y * stride;
        int n = w * 2, k = 0;
        for (int i = 0; i < n; i += 3) {
            uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
            line[k++] = T64[v >> 18 & 63]; line[k++] = T64[v >> 12 & 63];
            line[k++] = i + 1 < n ? T64[v >> 6 & 63] : '=';
            line[k++] = i + 2 < n ? T64[v & 63] : '=';
        }
        line[k] = 0;
        printf("SHOTR %d %s\n", y, line);
    }
    printf("SHOT end\n");
    lv_draw_buf_destroy(b);
}

#ifndef SIM   /* hardware-only test aids: FreeRTOS stats, A2DP encoder bench, heap walk and tracing */
/* test aid: which tasks used the CPU over 2 s (runtime deltas) */
static void tasks_top(void)
{
    static TaskStatus_t a[80], b[80];
    configRUN_TIME_COUNTER_TYPE ta, tb;
    UBaseType_t na = uxTaskGetSystemState(a, 80, &ta);
    vTaskDelay(pdMS_TO_TICKS(2000));
    UBaseType_t nb = uxTaskGetSystemState(b, 80, &tb);
    for (UBaseType_t i = 0; i < nb; i++) {
        uint32_t prev = 0;
        for (UBaseType_t j = 0; j < na; j++) if (a[j].xHandle == b[i].xHandle) prev = a[j].ulRunTimeCounter;
        uint32_t d = b[i].ulRunTimeCounter - prev;
        if (d > 20000) printf("APP task %-16s core=%d %3lu%%\n", b[i].pcTaskName, (int)xTaskGetCoreID(b[i].xHandle), (unsigned long)(d / 20000));
    }
    printf("APP tasks n=%u\n", (unsigned)nb);
}

/* test aid (2026-11-09): cost of the A2DP encoders (headphones mode) without headphones: 2 s of a sweep each */
#include "esp_audio_enc.h"
#include "esp_aac_enc.h"
#include "esp_sbc_enc.h"
static char s_wget_url[160];
static int s_wget_secs;
static void wget_task(void *arg)
{
    (void)arg;
    wifi_svc_bench(s_wget_url, s_wget_secs);
    vTaskDelete(NULL);
}

static void enc_bench_task(void *arg)
{
    (void)arg;
    for (int kind = 0; kind < 2; kind++) {
        esp_audio_enc_handle_t h = NULL;
        if (kind == 0) {
            esp_aac_enc_config_t c = ESP_AAC_ENC_CONFIG_DEFAULT();
            c.sample_rate = 44100; c.channel = 2; c.bits_per_sample = 16; c.bitrate = 256000; c.adts_used = false;
            esp_audio_enc_config_t ec = { .type = ESP_AUDIO_TYPE_AAC, .cfg = &c, .cfg_sz = sizeof c };
            esp_aac_enc_register();
            esp_audio_enc_open(&ec, &h);
        } else {
            esp_sbc_enc_config_t c = ESP_SBC_STD_ENC_CONFIG_DEFAULT();
            c.sample_rate = 44100; c.ch_mode = ESP_SBC_CH_MODE_JOINT_STEREO; c.allocation_method = ESP_SBC_ALLOC_LOUDNESS;
            c.bitpool = 53; c.block_length = 16; c.sub_bands_num = 8;
            esp_audio_enc_config_t ec = { .type = ESP_AUDIO_TYPE_SBC, .cfg = &c, .cfg_sz = sizeof c };
            esp_sbc_enc_register();
            esp_audio_enc_open(&ec, &h);
        }
        int in_sz = 0, out_sz = 0;
        if (!h || esp_audio_enc_get_frame_size(h, &in_sz, &out_sz) != ESP_AUDIO_ERR_OK) { printf("APP encbench open failed\n"); continue; }
        uint8_t *in = heap_caps_malloc((size_t)in_sz, MALLOC_CAP_SPIRAM), *out = heap_caps_malloc((size_t)out_sz, MALLOC_CAP_SPIRAM);
        int16_t *p = (int16_t *)in;
        int fs = in_sz / 4, frames = 0;
        int64_t used = 0;
        for (int f = 0; frames * fs < 44100 * 2; f++, frames++) {                /* 2 s of audio */
            for (int i = 0; i < fs; i++) { float t = (float)(f * fs + i) / 44100.0f; p[2 * i] = p[2 * i + 1] = (int16_t)(8000 * sinf(6.283f * (200 + 2000 * t) * t)); }
            esp_audio_enc_in_frame_t fi = { .buffer = in, .len = (uint32_t)in_sz };
            esp_audio_enc_out_frame_t fo = { .buffer = out, .len = (uint32_t)out_sz };
            int64_t t0 = esp_timer_get_time();
            esp_audio_enc_process(h, &fi, &fo);
            used += esp_timer_get_time() - t0;
        }
        printf("APP encbench %s frames=%d us_total=%lld cpu_pct=%.1f core_after=%d\n", kind ? "SBC" : "AAC", frames, used,
               100.0 * (double)used / 2e6, (int)xPortGetCoreID());
        esp_audio_enc_close(h);
        heap_caps_free(in);
        heap_caps_free(out);
    }
    vTaskDelete(NULL);
}


/* test aid (2026-11-11, leak cycles): internal heap blocks by size, and the blocks that are new since "heapsnap"
 * (address, size, first bytes) - finds what a repeated action keeps without a heap-tracing build */
#define SNAP_MAX 6000
typedef struct { void *p; uint32_t n; } snap_t;
static snap_t *s_snap, *s_cur;
static int s_nsnap, s_ncur;
static uint32_t s_bins[12], s_binb[12], s_used, s_nused;
static bool walk_cb(walker_heap_into_t h, walker_block_info_t b, void *u)
{
    (void)h;
    if (!b.used) return true;
    int k = 0;
    while (k < 11 && b.size > (16u << k)) k++;
    s_bins[k]++; s_binb[k] += (uint32_t)b.size; s_used += (uint32_t)b.size; s_nused++;
    if (u && s_ncur < SNAP_MAX) { s_cur[s_ncur].p = b.ptr; s_cur[s_ncur++].n = (uint32_t)b.size; }
    return true;
}
static int snap_cmp(const void *a, const void *b) { uintptr_t x = (uintptr_t)((const snap_t *)a)->p, y = (uintptr_t)((const snap_t *)b)->p; return x < y ? -1 : x > y; }
static void heap_walk(int mode)    /* 0 histogram, 1 snapshot, 2 diff against the snapshot */
{
    if (!s_snap) { s_snap = heap_caps_calloc(SNAP_MAX, sizeof *s_snap, MALLOC_CAP_SPIRAM); s_cur = heap_caps_calloc(SNAP_MAX, sizeof *s_cur, MALLOC_CAP_SPIRAM); }
    if (!s_snap || !s_cur) { printf("HEAP no psram\n"); return; }
    memset(s_bins, 0, sizeof s_bins); memset(s_binb, 0, sizeof s_binb); s_used = s_nused = 0; s_ncur = 0;
    heap_caps_walk(MALLOC_CAP_INTERNAL, walk_cb, (void *)1);
    printf("HEAP used=%lu blocks=%lu free=%u", (unsigned long)s_used, (unsigned long)s_nused, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    for (int k = 0; k < 12; k++) printf(" <=%u:%lu/%lu", k < 11 ? 16u << k : 99999u, (unsigned long)s_bins[k], (unsigned long)s_binb[k]);
    printf("\n");
    qsort(s_cur, (size_t)s_ncur, sizeof *s_cur, snap_cmp);
    if (mode == 1) { memcpy(s_snap, s_cur, (size_t)s_ncur * sizeof *s_cur); s_nsnap = s_ncur; printf("HEAP snap n=%d\n", s_nsnap); return; }
    if (mode != 2) return;
    int added = 0, gone = 0; long addb = 0, goneb = 0;
    for (int i = 0, j = 0; i < s_ncur || j < s_nsnap;) {
        int c = i >= s_ncur ? 1 : j >= s_nsnap ? -1 : s_cur[i].p == s_snap[j].p ? (s_cur[i].n == s_snap[j].n ? 0 : 2) : (uintptr_t)s_cur[i].p < (uintptr_t)s_snap[j].p ? -1 : 1;
        if (c == 0) { i++; j++; continue; }
        if (c == 1) { gone++; goneb += s_snap[j].n; j++; continue; }
        if (c == 2) { gone++; goneb += s_snap[j].n; j++; }
        const uint8_t *q = s_cur[i].p;
        if (added < 60) {
            printf("HEAP new %p %lu:", q, (unsigned long)s_cur[i].n);
            for (int k = 0; k < 24 && k < (int)s_cur[i].n; k++) printf(" %02x", q[k]);
            printf(" |");
            for (int k = 0; k < 24 && k < (int)s_cur[i].n; k++) putchar(q[k] >= 32 && q[k] < 127 ? q[k] : '.');
            printf("|\n");
        }
        added++; addb += s_cur[i].n; i++;
    }
    printf("HEAP diff new=%d/%ld gone=%d/%ld\n", added, addb, gone, goneb);
}

#if CONFIG_HEAP_TRACING_STANDALONE        /* heap-tracing build only (tools/simtools/leak_cycles.py, app/build-trace) */
#include "esp_heap_trace.h"
static heap_trace_record_t *s_trace;
static void trace_cmd(const char *a)
{
    if (!s_trace) {
        s_trace = heap_caps_calloc(2000, sizeof *s_trace, MALLOC_CAP_SPIRAM);
        if (!s_trace || heap_trace_init_standalone(s_trace, 2000) != ESP_OK) { printf("HEAP trace init failed\n"); return; }
    }
    if (!strcmp(a, "start")) printf("HEAP trace start rc=%d\n", heap_trace_start(HEAP_TRACE_LEAKS));
    else if (!strcmp(a, "stop")) printf("HEAP trace stop rc=%d\n", heap_trace_stop());
    else if (!strcmp(a, "dump")) heap_trace_dump_caps(MALLOC_CAP_INTERNAL);
}
#endif

/* tfread <path> [chunk]: the card's sequential read rate through the reader the backends get (unbuffered FILE, pc_reader_file),
 * in chunks of <chunk> bytes (default 16384; the WAV backend reads 4096). Called from the console task loop (not LVGL: a long read must
 * not freeze the UI); the chunk buffer is PSRAM, `path` is edited in place (the console task's line buffer), no float printf. */
static void tfread(char *path)
{
    uint32_t chunk = 16384;
    char *sp = strrchr(path, ' ');
    if (sp && sp[1] && strspn(sp + 1, "0123456789") == strlen(sp + 1)) { chunk = (uint32_t)strtoul(sp + 1, NULL, 10); *sp = 0; }
    if (!path[0] || chunk < 1 || chunk > (1u << 20)) { printf("APP tfread usage: tfread <path> [chunk 1..1048576]\n"); return; }
    FILE *f = fopen(path, "rb");
    if (f) setvbuf(f, NULL, _IONBF, 0);              /* as mp3_ctl opens it */
    pc_reader_t r;
    uint8_t *buf = heap_caps_malloc(chunk, MALLOC_CAP_SPIRAM);
    if (!buf || pc_reader_file(&r, f)) { printf("APP tfread failed: %s\n", buf ? "open" : "no memory"); if (f) fclose(f); free(buf); return; }
    uint64_t t0 = (uint64_t)esp_timer_get_time();
    uint32_t off = 0;
    int got;
    while (off < r.size && (got = r.read(r.u, off, buf, chunk)) > 0) off += (uint32_t)got;
    uint64_t us = (uint64_t)esp_timer_get_time() - t0;
    unsigned m100 = us ? (unsigned)((uint64_t)off * 800 / us) : 0;   /* Mbit/s x 100, integer math: no float printf on the 4 KB console stack */
    printf("APP tfread bytes=%lu ms=%lu mbps=%u.%02u\n", (unsigned long)off, (unsigned long)(us / 1000), m100 / 100, m100 % 100);
    fclose(f);
    free(buf);
}

#endif   /* !SIM */

/* "cardtest raw|file" (aliases "read" / "write"): starts the test; with the card page on top its timer runs it, else this loop does (the result lines print when it ends).
 * "cardtest tap raw|file" clicks the page's row, "cardtest show" prints the last results */
static void cardtest_cmd(const char *a)
{
    int kind = !strcmp(a, "raw") || !strcmp(a, "read") ? CT_RAW : !strcmp(a, "file") || !strcmp(a, "write") ? CT_FILE : -1;
    if (!strncmp(a, "tap ", 4)) { scr_cardtest_cmd(a + 4); return; }
    if (!strcmp(a, "page")) { scr_cardtest_cmd("page"); return; }
    if (!strcmp(a, "tmp")) { struct stat st; if (stat(PC_CARDTEST_TMP, &st)) printf("APP cardtest tmp none\n"); else printf("APP cardtest tmp size=%ld\n", (long)st.st_size); return; }   /* the temp file now */
    if (!strcmp(a, "show")) { for (int k = 0; k < CT_KINDS; k++) card_test_print(k); return; }
    if (kind < 0) { printf("APP cardtest usage: cardtest raw|file|tap raw|tap file|page|tmp|show\n"); return; }
    if (!card_test_start(kind)) { printf("APP cardtest %s refused fail=%d\n", kind == CT_RAW ? "raw" : "file", card_test_result(kind)->fail); return; }
    printf("APP cardtest %s started\n", kind == CT_RAW ? "raw" : "file");
    if (ui_top() == &scr_cardtest) return;
    while (card_test_running()) { card_test_step(); vTaskDelay(1); }
}

/* test aid (2026-11-11): whole-screen redraw time (render + flush), n times */
static void refr_bench(int n)
{
    int64_t sum = 0, worst = 0;
    for (int i = 0; i < n; i++) {
        lv_obj_invalidate(lv_screen_active());
        int64_t t0 = esp_timer_get_time();
        lv_refr_now(NULL);
        int64_t d = esp_timer_get_time() - t0;
        sum += d;
        if (d > worst) worst = d;
    }
    printf("APP refrbench n=%d avg_us=%lld max_us=%lld\n", n, sum / n, worst);
}

#ifndef SIM   /* serial file transfer */
/* test aid (2026-11-11): "get <path> [offset len]" prints a file (or a range) as "GET <offset> <n> <base64>"
 * lines (768 bytes each; another task's log line can cut one, the host then asks again for that range) and the
 * SHA-256 of the bytes read; "sha <path>" only the SHA-256 (tools/board/tf_get.py). Console task, not LVGL. */
static void get_file(char *path, bool dump)
{
    static const char T64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    long off = 0, want = -1;
    char *sp = strchr(path, ' ');
    if (sp) { *sp = 0; sscanf(sp + 1, "%ld %ld", &off, &want); }
    FILE *f = fopen(path, "rb");
    if (!f) { printf("GET fail\n"); return; }
    if (off > 0) fseek(f, off, SEEK_SET);
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&op, PSA_ALG_SHA_256);
    static uint8_t b[768];
    static char line[1100];
    size_t n, total = 0;
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        size_t ask = sizeof b;
        if (want >= 0 && (size_t)want - total < ask) ask = (size_t)want - total;
        if (!ask || (n = fread(b, 1, ask, f)) == 0) break;
        psa_hash_update(&op, b, n);
        total += n;
        if (!dump) continue;
        int k = 0;
        for (size_t i = 0; i < n; i += 3) {
            uint32_t v = (uint32_t)b[i] << 16 | (i + 1 < n ? (uint32_t)b[i + 1] << 8 : 0) | (i + 2 < n ? b[i + 2] : 0);
            line[k++] = T64[v >> 18 & 63]; line[k++] = T64[v >> 12 & 63];
            line[k++] = i + 1 < n ? T64[v >> 6 & 63] : '=';
            line[k++] = i + 2 < n ? T64[v & 63] : '=';
        }
        line[k] = 0;
        printf("GET %ld %u %s\n", off + (long)(total - n), (unsigned)n, line);
    }
    fclose(f);
    uint8_t h[32];
    size_t hl = 0;
    psa_hash_finish(&op, h, sizeof h, &hl);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    printf("GET end size=%u sha256=%s ms=%lld\n", (unsigned)total, hex, (esp_timer_get_time() - t0) / 1000);
}

#endif   /* !SIM */

/* Deliberate leak for checking the ui_pop tripwire (console uileak, then back: one timer, one object left behind) */
static lv_timer_t *s_leak_timer;
static lv_obj_t *s_leak_obj;
static void leak_tick(lv_timer_t *t) { (void)t; }
static void leaktest_create(lv_obj_t *p, void *arg)
{
    (void)p; (void)arg;
    s_leak_timer = lv_timer_create(leak_tick, 1000, NULL);
    s_leak_obj = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_leak_obj);
}
static const ui_screen_t scr_leaktest = { .name = "leaktest", .create = leaktest_create };
/* UI unify T3: the popring test page (uipush poptest). A tap on the page opens a popring: cell 0 armed (two taps), 1 press, 2 a bounded axis 0..4,
 * 3 plain. Prints "APP poptest open t=<ms>" and, for every run, "APP poptest ran <cell> v=<axis value> t=<ms> tap=<ui_axis_from_tap>". Shared pieces only. */
enum { PT_ARM, PT_PRESS, PT_AXIS, PT_PLAIN, PT_N };
static int s_pt_v;
static lv_obj_t *s_pt_root;
static const char *pt_label(int i)
{
    static char b[24];
    if (i == PT_AXIS) { snprintf(b, sizeof b, "%s %d", T(S_VALUE) /* 值 */, s_pt_v); return b; }
    return i == PT_ARM ? T(S_ACTION) /* 動作 */ : i == PT_PRESS ? T(S_PRESS) /* 按下 */ : T(S_PLAIN) /* 一般 */;
}
static const char *pt_armed(int i) { (void)i; return T(S_TAP_AGAIN_RUN) /* 再點一次：執行 */; }
static void pt_ran(int i) { printf("APP poptest ran %d v=%d t=%u tap=%d\n", i, s_pt_v, (unsigned)lv_tick_get(), ui_axis_from_tap()); }
static void pt_pick(int i) { pt_ran(i); }
static bool pt_step(int axis, int d)
{
    (void)axis;
    s_pt_v = ui_axis_step(s_pt_v, 0, 4, 1, d, ui_axis_from_tap());
    pt_ran(PT_AXIS);
    if (ui_axis_from_tap()) ui_popring_relabel();   /* a knob turn does not relabel: the popring itself keeps its 4 s going */
    return true;
}
static const ui_popring_t s_pt_def = { .n = PT_N, .label = pt_label, .pick = pt_pick, .armed = pt_armed, .arm_mask = 1u << PT_ARM,
                                       .press_mask = 1u << PT_PRESS, .axis_mask = 1u << PT_AXIS, .step = pt_step, .help = &HELP_SETTINGS };   /* T6: the last cell is 說明 */
static const ui_popring_t s_pt_def2 = { .n = 2, .label = pt_label, .pick = pt_pick };   /* a second ring on the same page: no axis, the default centre (Close) */
static char s_pt_centre[200];                           /* test aid: `poptest centre TEXT` opens a ring with this centre text (wrap tests of the ring centre) */
static const char *pt_centre(void) { return s_pt_centre; }
static const ui_popring_t s_pt_def3 = { .n = 2, .label = pt_label, .pick = pt_pick, .centre = pt_centre };
static void pt_centre_cmd(const char *t) { snprintf(s_pt_centre, sizeof s_pt_centre, "%s", t); if (s_pt_root && ui_top_root() == s_pt_root) ui_popring_open(s_pt_root, &s_pt_def3, 0); }
static const ui_popring_t s_pt_def4 = { .n = 14, .label = pt_label, .pick = pt_pick, .help = &HELP_SETTINGS };   /* T6 fix: a full def (14 cells, the capacity) must still get its help cell */
static const ui_popring_t s_pt_def5 = { .n = 14, .label = pt_label, .pick = pt_pick, .status = &HELP_STATUS, .help = &HELP_SETTINGS };   /* hires T7: a full def with both extra cells: 12 own cells are kept */
static void pt_open5(void) { if (s_pt_root && ui_top_root() == s_pt_root) ui_popring_open(s_pt_root, &s_pt_def5, 0); }
static void pt_open4(void) { if (s_pt_root && ui_top_root() == s_pt_root) ui_popring_open(s_pt_root, &s_pt_def4, 0); }
static void pt_open2(void) { if (s_pt_root && ui_top_root() == s_pt_root) ui_popring_open(s_pt_root, &s_pt_def2, 0); }
static void pt_open(void) { printf("APP poptest open t=%u\n", (unsigned)lv_tick_get()); ui_popring_open(s_pt_root, &s_pt_def, 0); }
static void pt_create(lv_obj_t *p, void *arg) { (void)arg; s_pt_root = p; s_pt_v = 0; ui_page_tap(p, pt_open); }
static const ui_screen_t scr_poptest = { .name = "poptest", .create = pt_create, .on_knob = ui_axis_knob };

/* UI unify T4: the page-kit test pages (uipush kit .. kit5), shared pieces only; every control prints "APP kit <piece> <i>" (the page's own tap: "APP kit page 0").
 * kit: HEAD axis button (3 values), CENTER big button, LOWER progress bar. kit2: UPPER choice row of 4, LOWER pill. kit3: 2 x 2 choice grid. kit4: hub.
 * kit5: titled list of three marked rows (the second is current) + a note, FOOT pill, dots. The buttons of one page are 8 px or more apart (tools/check/ui_boxes_check.py). */
static int s_kt_v;
static void kt_hit(const char *piece, int i) { printf("APP kit %s %d\n", piece, i); }
static void kt_big(lv_event_t *e) { (void)e; kt_hit("big", 0); }
static void kt_pill(lv_event_t *e) { (void)e; kt_hit("pill", 0); }
static void kt_choice(lv_event_t *e) { kt_hit("choice", (int)(intptr_t)lv_event_get_user_data(e)); }
static void kt_grid(lv_event_t *e) { kt_hit("grid", (int)(intptr_t)lv_event_get_user_data(e)); }
static void kt_row(lv_event_t *e) { kt_hit("row", (int)(intptr_t)lv_event_get_user_data(e)); }
static void kt_hub(void) { kt_hit("hub", 0); }
static void kt_page(void) { kt_hit("page", 0); }
static const char *kt_text(int axis) { static char b[16]; (void)axis; snprintf(b, sizeof b, "val %d", s_kt_v); return b; }
static bool kt_step(int axis, int d) { (void)axis; s_kt_v = ((s_kt_v + d) % 3 + 3) % 3; kt_hit("axis", s_kt_v); return true; }
static void kit_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    s_kt_v = 0;
    lv_label_set_text(ui_text(p, UI_SLOT_TITLE, NULL), "kit");
    ui_axis_button(p, UI_SLOT_HEAD, 0, kt_step, kt_text);
    lv_obj_t *l;
    ui_big_button(p, UI_SLOT_CENTER, kt_big, NULL, &l);
    lv_label_set_text(l, "Go");
    lv_bar_set_value(ui_progress_bar(p, UI_SLOT_LOWER), 400, LV_ANIM_OFF);
    ui_page_tap(p, kt_page);
    ui_back_button(p);
}
static void kit2_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    lv_obj_t *c[4];
    ui_choice_row(p, UI_SLOT_UPPER, 4, kt_choice, c);
    for (int i = 0; i < 4; i++) ui_choice_set(c[i], (const char *[]){ "A", "B", "C", "D" }[i], i);
    ui_pill(p, UI_SLOT_LOWER, "Pill", kt_pill, NULL);
    ui_page_tap(p, kt_page);
    ui_back_button(p);
}
static void kit3_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    lv_obj_t *c[4];
    ui_choice_grid(p, 2, 2, kt_grid, c);
    for (int i = 0; i < 4; i++) ui_choice_set(c[i], (const char *[]){ "A", "B", "C", "D" }[i], i == 1 ? UI_LOOK_SEL : UI_LOOK_NORMAL);
    ui_page_tap(p, kt_page);
    ui_back_button(p);
}
static void kit4_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    ui_hub(p, 116, kt_hub);
    ui_page_tap(p, kt_page);
    ui_back_button(p);
}
static void kit5_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    lv_obj_t *l = ui_list_titled(p, "kit5");
    for (int i = 0; i < 3; i++) ui_list_row_mark(l, (const char *[]){ "One", "Two", "Three" }[i], i == 1, kt_row, (void *)(intptr_t)i);
    ui_list_note(l, "note");
    ui_pill(p, UI_SLOT_FOOT, "Foot", kt_pill, NULL);
    ui_dots_set(ui_dots(p, UI_SLOT_HEAD, 5), 5, 2, UI_SEL);
    ui_back_button(p);
}
/* kit6 (UI unify batch A fix): the kit's edge contracts. A choice row asked for 9 cells keeps every cell UI_TAP_MIN wide; a 3 x 3 grid (made 2 x 2) leaves the unset tail of cells[] NULL;
 * the title of a plain list (a pill before it, no title label) is NULL. Prints "APP kit6 row=<cells made> tail_null=<0|1> title=<0|1>". */
static void kit6_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    lv_obj_t *r[9], *g[9];
    for (int i = 0; i < 9; i++) r[i] = g[i] = (lv_obj_t *)(intptr_t)1;
    ui_choice_row(p, UI_SLOT_UPPER, 9, kt_choice, r);
    ui_choice_grid(p, 3, 3, kt_grid, g);
    int made = 0, tail = 1;
    for (int i = 0; i < 9; i++) made += r[i] != (lv_obj_t *)(intptr_t)1 && r[i];
    for (int i = 4; i < 9; i++) tail &= g[i] == NULL;
    ui_pill(p, UI_SLOT_FOOT, "Foot", kt_pill, NULL);
    printf("APP kit6 row=%d tail_null=%d title=%d\n", made, tail, ui_list_title(ui_list(p)) != NULL);
}
/* kit7 (fx apps T5): one list row per look through ui_list_row_look (normal, sel, dim, sys, on, row text = the look's name); prints "APP kit look <n> bg=<hex> fg=<hex>" per row. */
static void kit7_create(lv_obj_t *p, void *arg)
{
    (void)arg;
    static const char *const names[] = { "normal", "sel", "dim", "sys", "on" };
    static const int looks[] = { UI_LOOK_NORMAL, UI_LOOK_SEL, UI_LOOK_DIM, UI_LOOK_SYS, UI_LOOK_ON };
    lv_obj_t *l = ui_list_titled(p, "kit7");
    for (int i = 0; i < 5; i++) {
        lv_obj_t *r = ui_list_row(l, names[i], UI_PANEL2, kt_row, (void *)(intptr_t)i);
        ui_list_row_look(r, looks[i]);
        printf("APP kit look %d bg=%06x fg=%06x\n", i, (unsigned)lv_color_to_u32(lv_obj_get_style_bg_color(r, 0)) & 0xffffff,
               (unsigned)lv_color_to_u32(lv_obj_get_style_text_color(lv_obj_get_child(r, 0), 0)) & 0xffffff);
    }
    ui_back_button(p);
}
static bool kit_knob(int d) { return ui_axis_knob(d); }
static const ui_screen_t scr_kit6 = { .name = "kit6", .create = kit6_create };
static const ui_screen_t scr_kit = { .name = "kit", .create = kit_create, .on_knob = kit_knob };
static const ui_screen_t scr_kit2 = { .name = "kit2", .create = kit2_create };
static const ui_screen_t scr_kit3 = { .name = "kit3", .create = kit3_create };
static const ui_screen_t scr_kit4 = { .name = "kit4", .create = kit4_create };
static const ui_screen_t scr_kit5 = { .name = "kit5", .create = kit5_create };
static const ui_screen_t scr_kit7 = { .name = "kit7", .create = kit7_create };

static void leak_clean(void)
{
    if (s_leak_timer) lv_timer_delete(s_leak_timer);
    if (s_leak_obj) lv_obj_delete(s_leak_obj);
    s_leak_timer = NULL; s_leak_obj = NULL;
}

static void uistack_print(void)   /* console uistack: the screen stack, bottom to top (every mode) */
{
    printf("APP uistack");
    for (int i = 0; ui_layer(i); i++) printf(" %s", ui_layer(i)->name);
    printf("%s\n", ui_layer(0) ? "" : " -");
}

/* cfg <key> (read one back) / cfgdel <key> (drop it) / cfgset <key> <value> (put a value back): tests, and the tools that move settings and
 * put them back (board batch A F6, tools/simtools/cfg_keep.py); both apps */
static bool cfg_cmd(char *c)
{
    char k[64], *sp;
    if (cast_mode_active() && (!strncmp(c, "cfgdel ", 7) || !strncmp(c, "cfgset ", 7))) { printf("APP refused: the broadcast radio is play-only\n"); return true; }   /* as CTL: no settings written in this mode */
    if (sscanf(c, "cfgdel %63s", k) == 1) { cfg_del(k); printf("APP cfg del %s\n", k); }
    else if (!strncmp(c, "cfgset ", 7) && (sp = strchr(c + 7, ' ')) != NULL && sp > c + 7) {
        *sp = 0;
        if (sp - (c + 7) > 31 || strchr(c + 7, '=')) { printf("APP cfg refused key=%s (longer than 31 bytes or holds '=')\n", c + 7); return true; }   /* pc_kv: key[32], split at the first '=' */
        cfg_set_str(c + 7, sp + 1);
        if (!strcmp(c + 7, "lang")) i18n_load();       /* the language byte (a raw write: no rebuild, `lang` is the command that switches) */
        printf("APP cfg %s=%s\n", c + 7, cfg_str(c + 7, ""));
    }
    else if (sscanf(c, "cfg %63s", k) == 1) printf("APP cfg %s=%s\n", k, cfg_str(k, ""));
    else return false;
    return true;
}

static void run(void *arg)    /* LVGL context */
{
    static char buf64[64];
    char *c = arg;
    int n, n2, n3, n4, n5, n6;
    stats_svc_test_mode();          /* a test is driving the board: its playback must not count */
    printf("APP cmd %s\n", c);
    if (console_app_cmd) {      /* the instrument app has its own console hook; besides its commands: the layout dump (every mode) and the board-only test aids */
        if (console_app_cmd(c)) ;
        else if (!strcmp(c, "uiboxes")) ui_boxes();
        else if (!strcmp(c, "uiboxes clip")) ui_boxes_clip();
        else if (!strcmp(c, "uievents")) ui_events();
        else if (!strcmp(c, "uitop")) printf("APP uitop %s\n", ui_top() ? ui_top()->name : "-");
        else if (!strcmp(c, "uistack")) uistack_print();
        else if (!strcmp(c, "back")) ui_pop();
        else if (!strcmp(c, "uihome")) ui_home();
        else if (!strcmp(c, "ring")) ui_ring_print(ui_top_ring());
        else if (!strcmp(c, "popring dim auto")) ui_popring_dim_force(-1);
        else if (sscanf(c, "popring dim %d", &n) == 1) ui_popring_dim_force(n);
        else if (sscanf(c, "popring %d", &n) == 1) ui_popring_console(n);
        else if (!strcmp(c, "mem")) memlog("cmd");
        else if (!strcmp(c, "mem settle")) { bsp_display_release_dma(); memlog("cmd"); }   /* test aid only (a console line, no UI path): frees the display's last DMA bounce buffers first, so readings compare; no allocation */
        else if (cfg_cmd(c)) ;
#ifndef SIM
        else if (!strcmp(c, "shot")) shot();
        else if (!strcmp(c, "tasks")) tasks_top();
        else if (!strcmp(c, "heapwalk")) heap_walk(0);
        else if (!strcmp(c, "heapsnap")) heap_walk(1);
        else if (!strcmp(c, "heapdiff")) heap_walk(2);
        else if (!strncmp(c, "lowmem", 6)) lowmem_force(c[6] == ' ' ? atoi(c + 7) : -1);   /* test: force a level */
        else if (!strcmp(c, "crashlog")) crashlog_print_tail();
        else if (!strncmp(c, "crashtest", 9) && (!c[9] || c[9] == ' ')) crashlog_test(c[9] ? c + 10 : "");   /* needs /sdcard/.crashtest */
        else if (!strcmp(c, "safeshow")) printf("APP safeshow refused: player only\n");   /* the instrument's own hook (console_app_cmd) said no, so this branch is where its commands end */
#endif
        else printf("APP cmd unknown\n");
        free(c);
        return;
    }
    if (!strcmp(c, "scan")) ui_push(&scr_scan, NULL);
    else if (!strcmp(c, "mp3 all")) { mp3_ctl_play_folder(-1); now_open(); }
    else if (sscanf(c, "mp3 %d", &n) == 1) { if (n >= -1 && n < g_lib.nfolders) { mp3_ctl_play_folder(n); now_open(); } else printf("APP cmd mp3: folder %d out of range (-1..%d)\n", n, g_lib.nfolders - 1); }
    else if (sscanf(c, "libls %d", &n) == 1 && n >= 0 && n < g_lib.nfolders) {   /* tracks of folder n: index, title, path */
        const pc_folder_t *fo = &g_lib.folders[n];
        printf("APP libls folder=%d name=%s count=%ld sub=%ld parent=%d\n", n, pc_str(&g_lib, fo->name), (long)fo->count, (long)fo->sub_count, (int)fo->parent);
        for (int i = fo->first; i < fo->first + fo->count; i++)
            printf("APP libls %d %s | %s\n", i, pc_str(&g_lib, g_lib.tracks[i].title), pc_str(&g_lib, g_lib.tracks[i].path));
    }
    else if (!strcmp(c, "libls")) { for (int i = 0; i < g_lib.nfolders; i++) printf("APP libls folder %d %s\n", i, pc_str(&g_lib, g_lib.folders[i].name)); }
    else if (sscanf(c, "trackat %d %d", &n, &n2) == 2 && n >= 0 && n < g_lib.n && n2 >= 0 && n2 < 1000) {   /* trackat N P: from P/1000 of the file (tests a track end) */
        struct stat st;
        uint32_t at = stat(pc_str(&g_lib, g_lib.tracks[n].path), &st) ? 0 : (uint32_t)((uint64_t)st.st_size * (uint32_t)n2 / 1000);
        mp3_ctl_play_folder_track_at(pc_track_folder(&g_lib, n), n, at);
        now_open();
    }
    else if (!strcmp(c, "crashnet crash")) { crashnet_early(true); crashnet_card(g_sd_ok); printf("APP crashnet simulated crash reset: marks=%d\n", crashnet_count()); }   /* the boot after a panic, as the app_main calls do (the sim test; the board's real crash test needs the owner) */
    else if (sscanf(c, "crashnet marked %d", &n) == 1 && n >= 0 && n < g_lib.n) printf("APP crashnet track %d marked=%d\n", n, (int)crashnet_marked(pc_str(&g_lib, g_lib.tracks[n].path)));
    else if (!strcmp(c, "crashnet")) printf("APP crashnet marks=%d\n", crashnet_count());
    else if (sscanf(c, "track %d", &n) == 1 && n >= 0 && n < g_lib.n) { mp3_ctl_play_folder_track(pc_track_folder(&g_lib, n), n); now_open(); }
    else if (!strcmp(c, "next")) { const now_ops_t *o = player_ctl_now(); if (o) o->next(); }
    else if (!strcmp(c, "prev")) { const now_ops_t *o = player_ctl_now(); if (o && o->prev) o->prev(); else printf("APP prev n/a\n"); }
    else if (!strcmp(c, "voices")) {                    /* the per-voice level snapshot of the chip / tracker file that plays */
        pc_voices_t v;
        int nv = audio_svc_voices(&v);
        printf("APP voices n=%d", nv);
        for (int i = 0; i < nv; i++) printf(" %s=%d", v.name[i], v.level[i]);
        printf("\n");
    }
    else if (!strcmp(c, "voicex")) {                    /* the voice snapshot v2: per voice name level note(cents) strikes pan flags */
        pc_voices_t v;
        int nv = audio_svc_voices(&v);
        printf("APP voicex n=%d\n", nv);
        for (int i = 0; i < nv; i++) {
            char nn[4];
            pc_voice_note_name((v.flags[i] & PC_VF_PITCH) ? v.cents[i] : PC_VOICE_NOPITCH, nn);
            printf("APP voicex %d %s lv=%d note=%s cents=%d strikes=%d pan=%d flags=%d\n", i, v.name[i], v.level[i], nn, v.cents[i], v.strikes[i], v.pan[i], v.flags[i]);
        }
    }
    else if (!strncmp(c, "vmute", 5) && (!c[5] || c[5] == ' ')) {   /* vmute [<i> [on|mute|solo|cycle] | clear]: the per-voice mute / solo (the UI's state, locked: the console task may call the setters; the audio task applies the mask before the next block) */
        pc_voices_t v;
        const int nv = audio_svc_voices(&v);
        char w[16] = "";
        if (c[5] && sscanf(c + 6, "%d %15s", &n, w) >= 1 && n >= 0 && n < nv) {
            pc_vmute_sync();
            if (!strcmp(w, "on")) pc_vmute_set(n, PC_VM_ON, nv);
            else if (!strcmp(w, "mute")) pc_vmute_set(n, PC_VM_MUTED, nv);
            else if (!strcmp(w, "solo")) pc_vmute_set(n, PC_VM_SOLO, nv);
            else pc_vmute_cycle(n, nv);
        } else if (!strcmp(c + 5, " clear")) { pc_vmute_sync(); pc_vmute_clear(nv); }
        else if (c[5]) printf("APP cmd vmute: <i> [on|mute|solo|cycle] | clear\n");
        printf("APP vmute n=%d mask=0x%lx solo=%d states=", nv, (unsigned long)pc_vmute_mask(), pc_vmute_solo_voice());
        for (int i = 0; i < nv; i++) printf("%c", "-ms"[pc_vmute_get(i)]);
        printf("\n");
    }
    else if (!strncmp(c, "kssfm", 5) && (!c[5] || c[5] == ' ')) {   /* kssfm on|off: KSS MSX-MUSIC FM for the next KSS file (default on, not persisted; off = the old PSG/SCC-only decode) */
        if (!strcmp(c + 5, " on")) { backend_gme_kss_fm(1); printf("APP kssfm on (next KSS file)\n"); }
        else if (!strcmp(c + 5, " off")) { backend_gme_kss_fm(0); printf("APP kssfm off (next KSS file)\n"); }
        else printf("APP cmd kssfm: on|off\n");
    }
    else if (sscanf(c, "viz sel %d", &n) == 1) { scr_viz_pick_voice(n); scr_viz_stat(); }   /* the voice cursor of the voice rows / piano roll */
    else if (!strncmp(c, "viz zoom", 8) && (!c[8] || c[8] == ' ')) {   /* viz zoom [next|orig|zoom]: the square board's picture size (stored, applied if the page is up) */
        const char *a = c[8] ? c + 9 : "";
        int z = !strcmp(a, "zoom") ? 1 : !strcmp(a, "orig") ? 0 : !strcmp(a, "next") ? !ui_viz_zoom() : -1;
        if (z >= 0) scr_viz_set_zoom(z);
        else if (*a) printf("APP cmd viz zoom: next|orig|zoom\n");
        printf("APP viz zoom=%s\n", ui_viz_zoom() ? "zoom" : "orig");
    }
    else if (!strcmp(c, "stop")) { const now_ops_t *o = player_ctl_now(); if (o) o->stop(); }
    else if (!strcmp(c, "chip mode loop")) chip_set_mode(PC_SEQ_LOOP);
    else if (!strcmp(c, "chip mode random")) chip_set_mode(PC_SEQ_RANDOM);
    else if (sscanf(c, "chipopen %63s", buf64) == 1) printf("APP cmd chipopen rc=%d\n", chip_open_system(buf64));   /* browse only */
    else if (!strncmp(c, "chip ", 5)) {        /* chip <system> <file> <sub> */
        char sys[64]; int f = 0, sub = 0;
        if (sscanf(c + 5, "%63s %d %d", sys, &f, &sub) >= 1 && chip_open_system(sys) > 0) {
            if (f < 0 || f >= chip_file_count()) printf("APP cmd chip: file %d out of range (0..%d)\n", f, chip_file_count() - 1);
            else { chip_play(f, sub); now_open(); }
        } else printf("APP cmd chip failed\n");
    }
    else if (!strcmp(c, "chips")) { static char n[16][64]; int k = chip_list_systems(n, 16); for (int i = 0; i < k; i++) printf("APP chip system %s\n", n[i]); }
    else if (!strcmp(c, "scenes")) { int k = noise_load(); for (int i = 0; i < k; i++) printf("APP scene %d layers=%d %s\n", i, noise_scene(i)->nlayers, noise_scene(i)->id); }
    else if (sscanf(c, "layer %d", &n) == 1) printf("APP cmd layer rc=%d\n", noise_engine_toggle_layer(noise_current(), n));
    else if (!strcmp(c, "cats")) { static int ix[800]; noise_load(); for (int i = 0; i < noise_cat_count(); i++) printf("APP cat %d %s n=%d\n", i, noise_cat_name(i), noise_cat_sounds(i, ix, 800)); }
    else if (sscanf(c, "addsound %d %d", &n, &n2) == 2) printf("APP cmd addsound rc=%d\n", noise_engine_add_sound(n, n2));
    else if (sscanf(c, "delscene %d", &n) == 1) printf("APP cmd delscene rc=%d\n", noise_delete_custom(n));
    else if (sscanf(c, "savescene %d", &n) == 1) printf("APP cmd savescene rc=%d\n", noise_save_custom(n));
    else if (sscanf(c, "noise %d", &n) == 1) printf("APP cmd noise rc=%d\n", n >= 0 && noise_load() > n ? noise_engine_start(n) : -1);
    else if (sscanf(c, "sleep %d", &n) == 1) sleep_timer_set(n);
    else if (fx_console(c)) { }                                      /* fx state|stat|on|set|mode|import|export|off, fxs (fx_state.c) */
    else if (!strcmp(c, "viz barw") || !strcmp(c, "viz barw next") || !strcmp(c, "viz floor") || !strcmp(c, "viz floor next")) {   /* viz barw|floor [next]: the spectrum page's bar width / sensitivity settings */
        bool w = c[4] == 'b';
        if (strstr(c, " next")) { if (w) ui_viz_bar_w_next(); else ui_viz_floor_next(); }
        if (w) printf("APP viz barw=%d\n", ui_viz_bar_w()); else printf("APP viz floor=%d\n", (int)ui_viz_floor_db());
    }
    else if (!strncmp(c, "viz map", 7) && (!c[7] || c[7] == ' ')) {   /* viz map [next|height|tip|band|level]: the colour mapping of bars and ring (stored, applied if the page is up) */
        const char *a = c[7] ? c + 8 : "";
        int m = -1;
        for (int i = 0; i < PC_VD_MAPS; i++) if (!strcmp(a, pc_vd_map_name((pc_vd_map_t)i))) m = i;
        if (!strcmp(a, "next")) m = (ui_viz_map() + 1) % PC_VD_MAPS;
        if (m >= 0) scr_viz_set_map((pc_vd_map_t)m);
        else if (*a) printf("APP cmd viz map: next|height|tip|band|level\n");
        printf("APP viz map=%s\n", pc_vd_map_name(ui_viz_map()));
    }
    else if (!strncmp(c, "viz peaks", 9) && (!c[9] || c[9] == ' ')) {   /* viz peaks [next|on|off]: the falling peak marks of bars, ring and voices (stored, applied if the page is up) */
        const char *a = c[9] ? c + 10 : "";
        int on = !strcmp(a, "on") ? 1 : !strcmp(a, "off") ? 0 : !strcmp(a, "next") ? !ui_viz_peaks() : -1;
        if (on >= 0) scr_viz_set_peaks(on);
        else if (*a) printf("APP cmd viz peaks: next|on|off\n");
        printf("APP viz peaks=%d\n", ui_viz_peaks());
    }
    else if (!strncmp(c, "viz order", 9) && (!c[9] || c[9] == ' ')) {   /* viz order [next|seq|zigzag|spread]: the ring kind's band order (stored, applied if the page is up) */
        const char *a = c[9] ? c + 10 : "";
        int o = -1;
        for (int i = 0; i < PC_VD_ORDS; i++) if (!strcmp(a, pc_vd_order_name((pc_vd_order_t)i))) o = i;
        if (!strcmp(a, "next")) o = (ui_viz_order() + 1) % PC_VD_ORDS;
        if (o >= 0) scr_viz_set_order((pc_vd_order_t)o);
        else if (*a) printf("APP cmd viz order: next|seq|zigzag|spread\n");
        printf("APP viz order=%s\n", pc_vd_order_name(ui_viz_order()));
    }
    else if (!strncmp(c, "viz gap", 7) && (!c[7] || c[7] == ' ')) {   /* viz gap [next|0|2|4]: the ring wedges' gap in px (stored, the ring map is rebuilt if the page is up) */
        const char *a = c[7] ? c + 8 : "";
        int g = !strcmp(a, "0") ? 0 : !strcmp(a, "2") ? 2 : !strcmp(a, "4") ? 4 : !strcmp(a, "next") ? (ui_viz_ring_gap() == 2 ? 0 : ui_viz_ring_gap() == 0 ? 4 : 2) : -1;
        if (g >= 0) scr_viz_set_ring_gap(g);
        else if (*a) printf("APP cmd viz gap: next|0|2|4\n");
        printf("APP viz gap=%d\n", ui_viz_ring_gap());
    }
    else if (!strncmp(c, "viz size", 8) && (!c[8] || c[8] == ' ')) {   /* viz size [next|box|full]: the waterfall's picture size (stored, applied if the page is up) */
        const char *a = c[8] ? c + 9 : "";
        int f = !strcmp(a, "full") ? 1 : !strcmp(a, "box") ? 0 : !strcmp(a, "next") ? !ui_viz_fall_full() : -1;
        if (f >= 0) scr_viz_set_fall_full(f);
        else if (*a) printf("APP cmd viz size: next|box|full\n");
        printf("APP viz size=%s\n", ui_viz_fall_full() ? "full" : "box");
    }
    else if (!strcmp(c, "viz stat")) scr_viz_stat();
    else if (!strcmp(c, "viz menu")) { scr_viz_open(); scr_viz_ring(-2); scr_viz_stat(); }   /* the style ring (and, while it is open, the knob steps the current axis) */
    else if (sscanf(c, "viz pick %d", &n) == 1) { scr_viz_ring(n); scr_viz_stat(); }          /* a tap: 0..n-1 an axis cell (kind, variant, colour, map); -1 centre; -3 back */
    else if (!strcmp(c, "rtc")) bsp_rtc_dump();                                                   /* the FakePod RTC: registers 0x00..0x0A raw and decoded; the round board says absent */
    else if (!strcmp(c, "rtc set")) printf("APP cmd rtc set rc=%d\n", clock_rtc_write_now() ? 0 : -1);   /* write the system time (UTC) to the RTC and read it back */
    else if (!strcmp(c, "dac")) bsp_audio_dump();                                                /* F2: the FakePod CS43131 state and headphone detect; the round board says none */
    else if (!strcmp(c, "imu zero")) { printf("APP imu zero %s\n", imu_svc_zero() ? "asked (state in `imu`: zero=)" : "absent"); }   /* the neutral pose from the next still moment */
    else if (!strcmp(c, "imu cal")) imu_svc_cal();                                                /* LAB: the magnitude and the lab_imu_scale_pm that would make it 1.000 g */
    else if (!strcmp(c, "imu")) imu_svc_dump();                                                  /* F1b: the tilt source's raw and filtered values */
    else if (sscanf(c, "imu map %d %d %d", &n, &n2, &n3) == 3) { imu_svc_map(n != 0, n2, n3); printf("APP imu map swap=%d sx=%d sy=%d\n", n != 0, n2 < 0 ? -1 : 1, n3 < 0 ? -1 : 1); }   /* bring-up: the sensor's axes against the screen's */
    else if (sscanf(c, "knob %d", &n) == 1) ui_knob_inject(n);                                 /* one turn of the knob, the physical path */
    else if (!strncmp(c, "viz var ", 8)) {              /* viz var <n|next>: the current kind's variation */
        pc_vd_kind_t k = ui_viz_kind();
        int v = !strcmp(c + 8, "next") ? ui_viz_var(k) + 1 : atoi(c + 8);
        scr_viz_open();
        scr_viz_set_var(v);
        scr_viz_stat();
    } else if (!strncmp(c, "viz ", 4)) {                 /* viz <bars|ring|scope|fall|phase|voices|next> */
        int k = -1;
        for (int i = 0; i < PC_VD_KINDS; i++) if (!strcmp(c + 4, pc_vd_kind_name((pc_vd_kind_t)i))) k = i;
        if (!strcmp(c + 4, "next")) k = (ui_viz_kind() + 1) % PC_VD_KINDS;
        if (k < 0) printf("APP cmd viz: bars|ring|scope|fall|phase|voices|rows|roll|lyrics|next|var <n|next>|stat\n");
        else { scr_viz_open(); scr_viz_set_kind((pc_vd_kind_t)k); scr_viz_stat(); }
    }
    else if (!strcmp(c, "ramp next")) { ui_viz_ramp_next(); ui_rebuild_all(); printf("APP ramp=%s\n", pc_ramp_name(ui_viz_ramp())); }
    else if (!strcmp(c, "theme next")) ui_theme_next();
    else if (!strncmp(c, "lang", 4) && (!c[4] || c[4] == ' ')) {   /* lang [tw|cn|en]: set (every screen is built again), or just print */
        int l = c[4] ? pc_lang_parse(c + 5) : (int)i18n_lang();
        if (l < 0) printf("APP cmd lang: tw|cn|en\n");
        else { if (c[4]) i18n_set_lang((pc_lang_t)l); printf("APP lang=%s\n", pc_lang_code(i18n_lang())); }
    }
    else if (sscanf(c, "rot %d %d", &n, &n2) == 2) { extern void scr_now_set_rotation(int, int); scr_now_set_rotation(n, n2); }
    else if (!strcmp(c, "popring dim auto")) ui_popring_dim_force(-1);
    else if (sscanf(c, "popring dim %d", &n) == 1) ui_popring_dim_force(n);   /* debug: the dim layer of every ring forced on (1) or off (0) */
    else if (sscanf(c, "popring %d", &n) == 1) ui_popring_console(n);   /* the tap-revealed ring: see ui_popring.h */
    else if (sscanf(c, "axisstep %d %d %d %d %d %d", &n, &n2, &n3, &n4, &n5, &n6) == 6) printf("APP axisstep %d\n", ui_axis_step(n, n2, n3, n4, n5, n6 != 0));   /* v lo hi step detents tap */
    else if (!strcmp(c, "poptest2")) pt_open2();   /* the test page opens its second ring (UI unify T3 fix round) */
    else if (!strcmp(c, "poptest5")) pt_open5();   /* the same with a 現況 cell too (hires T7) */
    else if (!strcmp(c, "poptest4")) pt_open4();   /* the test page opens a full def of 14 cells (T6 fix: it must still get its help cell) */
    else if (!strncmp(c, "ninegrid", 8) && (!c[8] || c[8] == ' ')) ui_ninegrid_console(c[8] ? c + 9 : "");   /* the nine-grid (ui_ninegrid.c): state, or "tap <0..8>" */
    else if (!strcmp(c, "nowfp")) nowfp_print();   /* the square now-playing page's state line (now_fp.c) */
    else if (!strcmp(c, "nowmenu page")) now_menu_console(-4);   /* the playing ring's "next page" */
    else if (sscanf(c, "nowmenu %d", &n) == 1) now_menu_console(n);
    else if (sscanf(c, "nowhide %d", &n) == 1) { extern void scr_now_debug_hide(int); scr_now_debug_hide(n); }
    else if (!strcmp(c, "thumbbench")) thumb_svc_bench();
    else if (!strcmp(c, "thumbcache stat")) thumb_svc_cache_stat();     /* hires T8c: the card's cover cache */
    else if (!strcmp(c, "thumbcache build")) thumb_svc_cache_build();
    else if (sscanf(c, "speedstep %d", &n) == 1) { speed_svc_step(n); printf("APP speed pct=%d\n", speed_svc_pct()); }   /* BTAUDIO tests: the playing speed by detents (the playing ring's cell does the same) */
    else if (!strcmp(c, "btaudio") || !strncmp(c, "btaudio ", 8)) btaudio_cmd(c[7] ? c + 8 : "");   /* BTAUDIO: the Bluetooth audio page (scr_btaudio.c) */
    else if (!strcmp(c, "timeset") || !strncmp(c, "timeset ", 8)) timeset_cmd(c[7] ? c + 8 : "");         /* the Set time page (scr_timeset.c) */
    else if (!strncmp(c, "clock set ", 10)) {                                                            /* clock set YYYY-MM-DD HH:MM: the LOCAL time (cfg tz), as the Set time page's Apply */
        int y, mo, d, h, mi;
        if (sscanf(c + 10, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5) printf("APP cmd clock set rc=%d\n", clock_set_manual(y, mo, d, h, mi) ? 0 : -1);
        else printf("APP cmd clock set usage: clock set YYYY-MM-DD HH:MM\n");
    }
    else if (!strcmp(c, "wired") || !strncmp(c, "wired ", 6)) wired_cmd(c[5] ? c + 6 : "");         /* BTAUDIO: the Wired output page (scr_wiredout.c) */
    else if (!strcmp(c, "outstat") || !strncmp(c, "outstat ", 8)) out_status_console(c[7] ? c + 8 : "");   /* BTAUDIO: the output chain, the live lines and the details block; optional tw|cn|en */
    else if (!strcmp(c, "covers") || !strncmp(c, "covers ", 7)) covers_cmd(c[6] ? c + 7 : "");   /* hires T8e: the 封面快取 page (scr_covers.c) */
    else if (!strcmp(c, "mp3cur")) mp3_ctl_print();      /* hires T8e: the player's indices (a rescan keeps them) */
    else if (sscanf(c, "coversmooth %d", &n) == 1) cfg_set_int("cover_smooth", n);
    else if (sscanf(c, "wifi %d", &n) == 1) printf("APP cmd wifi rc=%d\n", wifi_svc_set(n != 0));
    else if (!strcmp(c, "wifi st")) printf("APP wifi on=%d up=%d ip=%s rssi=%d hold=%d\n", wifi_svc_on(), wifi_svc_connected(), wifi_svc_ip(), wifi_svc_rssi(), wifi_svc_hold_mask());
#ifndef SIM
    else if (sscanf(c, "hapticlen %d", &n) == 1) {     /* how long a DRV2605 effect really plays: GO (reg 0x0C) stays 1 */
        bsp_haptic_play((uint8_t)n);
        int64_t t0 = esp_timer_get_time();
        uint8_t go = 1;
        while (go && esp_timer_get_time() - t0 < 500000) { bsp_haptic_read_reg(0x0C, &go); go &= 1; }
        printf("APP haptic effect=%d ms=%.1f\n", n, (esp_timer_get_time() - t0) / 1000.0);
    }
#endif
    else if (!strncmp(c, "lab", 3) && (!c[3] || c[3] == ' ')) lab_cmd(c + 3);   /* LAB (lab.h): the test-day run-time knobs: "lab" lists, "lab <key> <value>" sets (stored), "lab reset" */
    else if (!strncmp(c, "cpuclk", 6) && (!c[6] || c[6] == ' ')) cpu_clock_cmd(c + 6);   /* the experimental CPU clock: "cpuclk" shows, "cpuclk 320|240|160" sets (saved) */
    else if (sscanf(c, "seti %63s %d", buf64, &n) == 2) { cfg_set_int(buf64, n); printf("APP cfg %s=%d\n", buf64, n); if (!strncmp(buf64, "rg", 2)) mp3_ctl_rg_changed(); else if (!strcmp(buf64, "dac_max_rate")) audio_svc_set_cap(pc_dacmax_norm(n, BSP_DAC_RATE_DEFAULT)); }   /* the ceiling applies at the next track; garbage normalises (hires T6) */
    else if (cfg_cmd(c)) ;
    else if (sscanf(c, "unset %63s", buf64) == 1) { int r = pc_kv_del(g_cfg, &g_cfg_n, buf64); if (r) cfg_save_soon(); printf("APP cfg unset %s removed=%d\n", buf64, r); }   /* a test key back to its default */
#ifndef SIM
    else if (!strcmp(c, "btscan")) bt_svc_scan();
    else if (!strcmp(c, "btdevs")) { bt_dev_t d[16]; int k = bt_svc_devices(d, 16); for (int i = 0; i < k; i++) printf("APP bt dev %d %02x%02x%02x%02x%02x%02x %s\n", i, d[i].bda[0], d[i].bda[1], d[i].bda[2], d[i].bda[3], d[i].bda[4], d[i].bda[5], d[i].name); }
    else if (sscanf(c, "btconnect %d", &n) == 1) { bt_dev_t d[16]; if (n >= 0 && n < bt_svc_devices(d, 16)) bt_svc_connect(d[n].bda); else printf("APP cmd btconnect: no device %d\n", n); }
    else if (!strncmp(c, "wget ", 5)) {               /* wget <seconds> <http url>: throughput test on a worker task */
        if (sscanf(c + 5, "%d %159s", &s_wget_secs, s_wget_url) == 2) xTaskCreatePinnedToCore(wget_task, "wget", 6144, NULL, 3, NULL, 0);
    }
    else if (!strncmp(c, "loglvl ", 7)) {            /* loglvl <tag|*> <0..5>: runtime log level (DEBUG needs a build with a higher maximum) */
        char tag[24]; int lv;
        if (sscanf(c + 7, "%23s %d", tag, &lv) == 2) { esp_log_level_set(tag, (esp_log_level_t)lv);
#if CONFIG_BT_ENABLED && CONFIG_BT_LOG_AVDT_TRACE_LEVEL >= 5                  /* debug builds only: AVDTP keeps its own level */
            if (!strcmp(tag, "BT_AVDT")) { extern unsigned char AVDT_SetTraceLevel(unsigned char); AVDT_SetTraceLevel(5); }
#endif
            printf("APP cmd loglvl %s=%d\n", tag, lv); }
    }
    else if (!strcmp(c, "usbmode")) usb_mode_request();
    else if (!strncmp(c, "ext ", 4)) usb_mode_console(c + 4);
    else if (!strcmp(c, "power")) power_svc_dump();   /* power state, last raw, raw histograms */
    else if (!strcmp(c, "power reset")) { power_svc_hist_reset(); printf("APP power hist reset\n"); }
    else if (sscanf(c, "bcastbypass %d", &n) == 1) { bt_svc_test_bypass(n != 0); printf("APP cmd bcastbypass=%d\n", n != 0); }   /* TEST ONLY (BR report): Wi-Fi + radio while broadcasting in the full player */
    else if (!strcmp(c, "castmode")) cast_mode_request();   /* the broadcast radio (reboot); the radio page's 廣播電台 row asks first */
    else if (!strcmp(c, "inst")) instrument_request();   /* the instrument app (reboot) */
#endif
    else if (sscanf(c, "alarm %d %d", &n, &n2) == 2) {   /* test: alarm at HH MM, every day, on */
        pc_alarm_t *a = clock_alarm(); a->on = true; a->min = n * 60 + n2; a->days = PC_DAYS_EVERY; clock_alarm_saved();
        printf("APP cmd alarm %02d:%02d valid=%d\n", n, n2, clock_time_valid());
    }
    else if (!strcmp(c, "alarm st")) {                  /* state, incl. a pending snooze (local time of the next ring) */
        const pc_alarm_t *a = clock_alarm();
        char sn[8] = "-";
        if (a->snooze_until) snprintf(sn, sizeof sn, "%02d:%02d", (int)(((a->snooze_until / 60 + clock_tz_min()) % 1440 + 1440) % 1440 / 60), (int)(((a->snooze_until / 60 + clock_tz_min()) % 1440 + 1440) % 60));
        printf("APP alarm on=%d at=%02d:%02d days=%u ringing=%d snooze=%s\n", a->on, a->min / 60, a->min % 60, (unsigned)a->days, clock_ringing(), sn);
    }
    else if (!strcmp(c, "alarm off")) { clock_alarm()->on = false; clock_alarm_saved(); clock_ring_stop(); }
    else if (!strcmp(c, "clock")) { extern bool clock_screen_open(void); if (!clock_screen_open()) ui_push(&scr_clock, NULL); }   /* one clock screen: its state is static */
    else if (!strncmp(c, "clockface ", 10)) { extern void clock_face_cmd(const char *); clock_face_cmd(c + 10); }   /* <name|next> */
    else if (!strcmp(c, "uistat")) ui_fps_stat();
    else if (!strcmp(c, "album")) ui_push(&scr_album, NULL);
    else if (!strcmp(c, "albumscan")) album_svc_wake();
    else if (!strcmp(c, "albumstat")) { album_svc_stat(); album_view_stat(); }
    else if (!strcmp(c, "albumnext")) album_view_next();
    else if (!strcmp(c, "albumclear")) album_svc_clear();
    else if (sscanf(c, "albummenu %d", &n) == 1) { extern void scr_album_menu(int); scr_album_menu(n); }
    else if (!strncmp(c, "timers", 6) && (c[6] == 0 || c[6] == ' ')) timers_cmd(c[6] ? c + 7 : c + 6);   /* the stopwatch and the countdown (HUD task) */
    else if (!strcmp(c, "pomo")) clock_pomo_start();
    else if (!strcmp(c, "pomo stop")) clock_pomo_stop();
    else if (!strcmp(c, "pomo stat")) { extern void clock_pomo_stat(void); clock_pomo_stat(); }
    else if (!strncmp(c, "show ", 5)) {                  /* screenshots: show help | alarm | pomo */
        extern bool clock_show(const char *what);
        if (!strcmp(c + 5, "help")) ui_help_open(&HELP_SETTINGS); else if (!clock_show(c + 5)) printf("APP show unknown %s\n", c + 5);
    }
    else if (!strcmp(c, "timenow")) { time_t t = time(NULL); printf("APP time utc=%lld valid=%d\n", (long long)t, clock_time_valid()); }
    else if (sscanf(c, "color %d", &n) == 1) { printf("APP cmd color rc=%d\n", color_noise_play(n)); now_open(); }
    else if (!strcmp(c, "radio stat")) { uint32_t b, r, u; radio_stats(&b, &r, &u); printf("APP radio stat bytes=%lu reconn=%lu underruns=%lu cur=%d\n", (unsigned long)b, (unsigned long)r, (unsigned long)u, radio_current()); }   /* soak aid */
    else if (!strcmp(c, "radio stop")) radio_stop();
    else if (!strncmp(c, "radiourl ", 9)) { int i = radio_add_temp("test", c + 9); printf("APP cmd radiourl rc=%d\n", i < 0 ? i : radio_play(i)); now_open(); }
    else if (sscanf(c, "radio %d", &n) == 1) { if (!radio_count()) radio_load(); printf("APP cmd radio rc=%d\n", radio_play(n)); now_open(); }
#ifndef SIM
    else if (sscanf(c, "sysmode %d", &n) == 1) { cfg_set_int("bt_mode", n); sysmode_reboot(); }   /* the UI's reboot switch */
    else if (sscanf(c, "btmode %d", &n) == 1) printf("APP cmd btmode rc=%d\n", bt_svc_set_mode((bt_mode_t)n));
    else if (sscanf(c, "tone %d", &n) == 1) printf("APP cmd tone rc=%d\n", test_tone_play(n));   /* codec checks: sine at -6 dBFS */
    else if (!strcmp(c, "pair yes") || !strcmp(c, "pair no")) { bool y = c[5] == 'y'; printf("APP cmd pair %s open=%d\n", y ? "yes" : "no", bt_svc_pair_pending(NULL)); bt_svc_pair_reply(y); }   /* answer the pairing prompt (tests) */
    else if (!strcmp(c, "btforget")) printf("APP cmd btforget n=%d\n", bt_svc_forget_all());   /* unpair every device */
    else if (!strncmp(c, "btkey ", 6)) {                /* btkey <play|pause|stop|next|prev>: what a headset key press runs (AVRCP target) */
        static const char *const kn[] = { "play", "stop", "pause", "next", "prev" };
        static const uint8_t kc[] = { 0x44, 0x45, 0x46, 0x4B, 0x4C };   /* AVRCP passthrough codes */
        int k = -1;
        for (int i = 0; i < 5; i++) if (!strcmp(c + 6, kn[i])) k = i;
        if (k < 0) printf("APP cmd btkey ?\n"); else bt_remote_key(kc[k], true);
    }
    else if (sscanf(c, "btabs %d", &n) == 1 && n >= 0 && n <= 127) bt_remote_heard((uint8_t)n);   /* what a headset VOLUME_CHANGE runs */
    else if (!strcmp(c, "btrc")) bt_remote_print();
    else if (!strcmp(c, "codecs")) printf("APP codecs=%s\n", ctl_codecs());   /* which build: ldac,aptxhd,aptx,aac,sbc (default) or aac,sbc (stock) */
    else if (sscanf(c, "ldac %d", &n) == 1) printf("APP cmd ldac rc=%d\n", bt_svc_ldac_kbps(n));   /* LDAC top rate (ABR below it) */
    else if (!strcmp(c, "encbench")) xTaskCreatePinnedToCore(enc_bench_task, "encbench", 8192, NULL, 2, NULL, 0);   /* starts on core 0 */
    else if (!strncmp(c, "fxbench", 7) && (!c[7] || c[7] == ' ')) fx_bench(c[7] ? c + 8 : "");   /* CPU cost of each sound-effect app (spec §11 M1); the instrument boot has no fx chain */
    else if (!strcmp(c, "tasks")) tasks_top();
    else if (!strcmp(c, "stack")) printf("APP stack lvgl=%u\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));   /* run() is on the LVGL task; the mark is the least free stack since boot, in bytes (IDF: StackType_t is uint8_t) */
    else if (!strcmp(c, "crashlog")) crashlog_print_tail();   /* last lines of /sdcard/_player/logs/crash.txt */
    else if (!strncmp(c, "crashtest", 9) && (!c[9] || c[9] == ' ')) crashlog_test(c[9] ? c + 10 : "");   /* needs /sdcard/.crashtest */
    else if (!strcmp(c, "safeshow")) {   /* player only; shows what safe mode shows: a fresh empty screen (the pages and their taps are not on it), then the three lines; leave with {"op":"reboot"} */
        printf("APP safeshow\n"); ui_popring_close(); ui_safe_show_fresh(lv_display_get_default(), "console test", 3);   /* the instrument app never gets here: its branch of run() answers "refused" */
    }
#endif
    else if (!strcmp(c, "shot")) shot();
    else if (!strcmp(c, "style next")) ui_menu_style_next();
    else if (!strcmp(c, "back")) ui_pop();
    else if (!strcmp(c, "list")) ui_open_once(&scr_tracks, (void *)(intptr_t)-1);
    else if (!strcmp(c, "setui")) ui_push(&scr_settings, NULL);
    else if (!strcmp(c, "ratetest start")) { if (ui_top() == &scr_ratetest) rate_test_start(); else { rate_test_want(); ui_push(&scr_ratetest, NULL); } }   /* the page starts the test (hires T11) */
    else if (!strcmp(c, "ratetest tap")) rate_test_tap();
    else if (!strcmp(c, "ratetest stop")) rate_test_stop();
    else if (!strcmp(c, "ratetest show")) rate_test_show();
    else if (!strcmp(c, "settings rows")) scr_settings_rows();
    else if (sscanf(c, "settings group %63s", buf64) == 1) scr_settings_group(buf64);
    else if (sscanf(c, "axishalf %d", &n) == 1) ui_list_row_axis_force(n);   /* the half a pointer-less click on a selected axis row counts as: -1 left, else right */
    else if (sscanf(c, "settings key %63s", buf64) == 1) scr_settings_tap_key(buf64);
    else if (!strncmp(c, "help ", 5)) ui_help_console(c + 5);
    else if (!strncmp(c, "fxui ", 5)) scr_fx_console(c + 5);
    else if (sscanf(c, "fxpage %63s", buf64) == 1) { int a = pc_fxapp_find(buf64); if (a < 0) printf("APP fxpage unknown %s\n", buf64); else ui_push(&scr_fxapp, (void *)(intptr_t)a); }
    else if (sscanf(c, "settings tap %d", &n) == 1) scr_settings_tap(n);
    else if (sscanf(c, "alarm tap %d", &n) == 1) clock_alarm_tap(n);
    else if (sscanf(c, "bt tap %d", &n) == 1 && scr_bt_tap_ok(n)) scr_bt_tap(n);   /* -1 the centre, the quality cell, 現況, the last cell 說明; any other index is not a test of the ring (it reboots or forgets) */
    else if (!strcmp(c, "ring")) ui_ring_print(ui_top_ring());   /* the top page's ring: page, selection, centre text */
    else if (!strcmp(c, "uihome")) ui_home();
    else if (!strcmp(c, "favui")) ui_push(&scr_favs, NULL);
    else if (sscanf(c, "coldui %63s", buf64) == 1) {   /* test aid: build + draw a screen at once (first visit after boot = cold glyphs) */
        int64_t t0 = esp_timer_get_time();
        if (!strcmp(buf64, "list")) ui_open_once(&scr_tracks, (void *)(intptr_t)-1);
        else if (!strcmp(buf64, "set")) ui_push(&scr_settings, NULL);
        else ui_push(&scr_favs, NULL);
        lv_refr_now(NULL);
        printf("APP coldui %s us=%lld\n", buf64, esp_timer_get_time() - t0);
    }
#ifndef SIM
    else if (!strcmp(c, "chipbench") || !strncmp(c, "chipbench ", 10)) chipbench_cmd(c[9] ? c + 10 : "");   /* CHIPEXP phase 0: decode a chip/tracker file for N s, time every 10 ms block (chipbench_ctl.c) */
#endif
    else if (!strcmp(c, "chipui")) ui_push(&scr_chip, NULL);
    else if (!strcmp(c, "noiseui")) ui_push(&scr_noise, NULL);
    else if (!strcmp(c, "mp3ui")) ui_push(&scr_mp3, NULL);
    else if (!strcmp(c, "btui")) ui_push(&scr_bt, NULL);
    else if (sscanf(c, "sceneui %d", &n) == 1) { extern const ui_screen_t scr_scene; ui_push(&scr_scene, (void *)(intptr_t)n); }
    else if (sscanf(c, "vol %d", &n) == 1) { app_set_volume(n); audio_svc_set_volume(app_volume()); vol_out_print(); }   /* BTVOL: the current output's volume */
    else if (!strcmp(c, "vol")) vol_out_print();
#ifndef SIM
    else if (!strcmp(c, "heapwalk")) heap_walk(0);
    else if (!strcmp(c, "heapsnap")) heap_walk(1);
    else if (!strcmp(c, "heapdiff")) heap_walk(2);
#if CONFIG_HEAP_TRACING_STANDALONE
    else if (!strncmp(c, "heaptrace ", 10)) trace_cmd(c + 10);
#endif
#endif
#ifdef SIM
    else if (!strncmp(c, "uimsg ", 6)) {    /* simulator tests: a message page with free text (ui_push_message keeps the pointer) */
        static char s_uimsg[96];
        snprintf(s_uimsg, sizeof s_uimsg, "%s", c + 6);
        ui_push_message(s_uimsg);
    }
    else if (!strncmp(c, "uiwarn ", 7)) {    /* simulator tests: a warning (the square panel's strip stays until tapped) */
        static char s_uiwarn[96];
        snprintf(s_uiwarn, sizeof s_uiwarn, "%s", c + 7);
        ui_push_warning(s_uiwarn);
    }
    else if (!strcmp(c, "uistrip")) printf("APP strip shown=%d\n", ui_strip_shown());
#endif
    else if (sscanf(c, "uipush %63s", buf64) == 1) {   /* leak cycles: open a screen the console has no command for */
        extern const ui_screen_t scr_colors;
        if (!strcmp(buf64, "menu")) ui_push(&scr_menu, NULL);
        else if (!strcmp(buf64, "langpick")) ui_push(&scr_langpick, NULL);   /* i18n B: the first-boot picker, on demand (pictures) */
        else if (!strcmp(buf64, "settings")) ui_push(&scr_settings, NULL);
        else if (!strcmp(buf64, "favs")) ui_push(&scr_favs, NULL);
        else if (!strcmp(buf64, "fx")) ui_push(&scr_fx, NULL);
        else if (!strcmp(buf64, "radio")) { if (!radio_count()) radio_load(); ui_push(&scr_radio, NULL); }
        else if (!strcmp(buf64, "colors")) ui_push(&scr_colors, NULL);
        else if (!strcmp(buf64, "breath")) ui_push(&scr_breath, NULL);   /* ideas G7: the breathing coach (a row of the noise list) */
        else if (!strcmp(buf64, "help")) ui_help_open(&HELP_SETTINGS);
        else if (!strcmp(buf64, "message")) ui_push_message(T(S_CANNOT_PLAY) /* 無法播放 */);
        else if (!strcmp(buf64, "fortune")) ui_push(&scr_fortune, NULL);
        else if (!strcmp(buf64, "pair")) ui_push(&scr_pair, NULL);
        else if (!strcmp(buf64, "poptest")) ui_push(&scr_poptest, NULL);
        else if (!strcmp(buf64, "kit")) ui_push(&scr_kit, NULL);
        else if (!strcmp(buf64, "kit2")) ui_push(&scr_kit2, NULL);
        else if (!strcmp(buf64, "kit3")) ui_push(&scr_kit3, NULL);
        else if (!strcmp(buf64, "kit4")) ui_push(&scr_kit4, NULL);
        else if (!strcmp(buf64, "kit5")) ui_push(&scr_kit5, NULL);
        else if (!strcmp(buf64, "kit6")) ui_push(&scr_kit6, NULL);
        else if (!strcmp(buf64, "kit7")) ui_push(&scr_kit7, NULL);
        else if (!strcmp(buf64, "charge")) ui_push(&scr_charge, NULL);   /* the FakePod charge screen (sim: state keys batt= charging= lowbat=) */
        else if (!strcmp(buf64, "meta")) { if (g_lib.n) ui_push(&scr_meta, (void *)(intptr_t)0); }
        else if (!strcmp(buf64, "now")) now_open();     /* the playing page as the menu opens it (the phone's stream has no console command that opens it) */
        else if (!strncmp(buf64, "tracks", 6)) { int fo = atoi(buf64 + 6); ui_open_once(&scr_tracks, (void *)(intptr_t)(fo >= 0 && fo < g_lib.nfolders ? fo : -1)); }
        else printf("APP uipush unknown\n");
    }
    else if (!strcmp(c, "btmeta")) bt_meta_print();   /* AVRCP 1.6: the phone's track and its cover art as the speaker mode has them */
    else if (!strcmp(c, "uileak")) ui_push(&scr_leaktest, NULL);
    else if (!strcmp(c, "uileak clean")) leak_clean();
    else if (!strcmp(c, "uitop")) printf("APP uitop %s\n", ui_top() ? ui_top()->name : "-");
    else if (!strcmp(c, "uiboxes")) ui_boxes();
    else if (!strcmp(c, "uiboxes clip")) ui_boxes_clip();
    else if (!strcmp(c, "uievents")) ui_events();
    else if (!strcmp(c, "gtfiles")) printf("APP gt files %d\n", gt_pack_open_count());
    else if (sscanf(c, "uipopto %63s", buf64) == 1) {  /* ui_pop_to the highest layer named so (tests) */
        const ui_screen_t *s = NULL;
        for (int i = 0; ui_layer(i); i++) if (!strcmp(ui_layer(i)->name, buf64)) s = ui_layer(i);
        printf("APP uipopto %s rc=%d\n", buf64, s && ui_pop_to(s));
    }
    else if (!strcmp(c, "uistack")) uistack_print();
    else if (!strcmp(c, "screen off")) ui_sleep();      /* through the UI: the spectrum stops drawing too */
    else if (sscanf(c, "cat %63s", buf64) == 1) {        /* small text files, e.g. /sdcard/_player/logs/soak.txt */
        FILE *f = fopen(buf64, "r");
        char l[160];
        while (f && fgets(l, sizeof l, f)) printf("CAT %s", l);
        if (f) fclose(f);
        printf("CAT end rc=%d\n", f ? 0 : -1);
    }
    else if (!strncmp(c, "poptest centre ", 15)) pt_centre_cmd(c + 15);
    else if (!strcmp(c, "screen on")) ui_wake();
    else if (!strcmp(c, "audio info")) {               /* the audio task's plan and output (aud_info_t), for the board run */
        aud_info_t a;
        audio_svc_info(&a);
        printf("APP audio info src=%lu pipe=%lu out=%lu bclk=%lu hw_cap=%lu cap=%lu why=%s halvings=%u bits=%u sink=%d underruns_src=%lu start_underruns_src=%lu wr_timeouts=%lu\n",
               (unsigned long)a.src_rate, (unsigned long)a.pipe_rate, (unsigned long)a.out_rate, (unsigned long)a.bclk, (unsigned long)a.hw_cap,
               (unsigned long)a.cap, pc_pipe_why_name(a.pipe_why), (unsigned)a.halvings, (unsigned)a.out_bits, a.sink ? 1 : 0,
               (unsigned long)a.underruns_src, (unsigned long)a.start_underruns_src, (unsigned long)a.wr_timeouts);
    }
    else if (!strcmp(c, "status")) {                   /* hires T7: the 現況 page's lines, one per APP status line (the page's text, status_body) */
        char *b = psram_malloc(2048), *l = b;
        if (b) {
            ui_status_body(b, 2048, i18n_zh(), 0);
            while (l && *l) {
                char *e = strstr(l, "\n\n");
                if (e) *e = 0;
                printf("APP status %s\n", l);
                l = e ? e + 2 : NULL;
            }
            psram_free(b);
        }
        printf("APP status end\n");
    }
    else if (!strcmp(c, "mem")) memlog("cmd");
    else if (!strcmp(c, "mem settle")) { bsp_display_release_dma(); memlog("cmd"); }   /* as above (the player's console) */
    else if (!strcmp(c, "memboot")) memlog_boot_request();   /* MEMB: reboot once with every init step logged + the full dump */
    else if (!strcmp(c, "memdump")) memlog_dump();
    else if (!strncmp(c, "lowmem", 6)) lowmem_force(c[6] == ' ' ? atoi(c + 7) : -1);   /* lowmem N: force a level (test); lowmem: measure */
    else if (!strcmp(c, "fortune") || !strncmp(c, "fortune ", 8)) fortune_cmd(c[7] ? c + 8 : "");   /* screenshots: see scr_fortune.c */
    else if (!strcmp(c, "tools") || !strncmp(c, "tools ", 6)) tools_cmd(c[5] ? c + 6 : "");         /* 小工具: see scr_tools.c */
    else if (!strcmp(c, "bubbles") || !strncmp(c, "bubbles ", 8)) ui_bubbles_cmd(c[7] ? c + 8 : "");   /* 捏泡泡紙: see ui_bubbles.h */
    else if (!strcmp(c, "tour")) ui_push(&scr_tour, NULL);                              /* GS: the gesture tutorial, see ui_tour.h */
    else if (!strcmp(c, "tour dump")) tour_dump();
    else if (!strcmp(c, "qs") || !strncmp(c, "qs ", 3)) ui_qs_cmd(c[2] ? c + 3 : "");   /* GS: the quick-settings page, see ui_qs.h */
    else if (!strncmp(c, "flick ", 6)) { const char *w = c + 6; ui_flick_inject(!strcmp(w, "up") ? UI_G_UP : !strcmp(w, "down") ? UI_G_DOWN : !strcmp(w, "left") ? UI_G_LEFT : !strcmp(w, "right") ? UI_G_RIGHT : UI_G_HOLD); }   /* GS: the verdict of a flick without a touch (up|down|left|right|hold) */
    else if (!strcmp(c, "frame") || !strncmp(c, "frame ", 6)) ui_frame_cmd(c[5] ? c + 6 : "");   /* the content-first page frame (F1a2): see ui_frame.h */
    else if (!strcmp(c, "exit") || !strncmp(c, "exit ", 5)) ui_exit_cmd(c[4] ? c + 5 : "");   /* the shared exit (G4b): see ui_exit.h */
    else if (!strcmp(c, "sfx")) sfx_print();                                                       /* toy sound effects: counters and the newest ids (sfx.h) */
    else if (!strncmp(c, "sfx play ", 9)) { sfx_play(c + 9); sfx_print(); }                        /* post one bank entry by name, as a toy does */
    else if (!strcmp(c, "sfx bank")) sfx_print_bank();                                              /* the parsed sound bank: counts and one line an entry */
    else if (!strcmp(c, "gamesave") || !strncmp(c, "gamesave ", 9)) gamesave_cmd(c[8] ? c + 9 : "");   /* games.txt: status | dump | get | set | flush | clear (gamesave_svc.h) */
    else if (!strcmp(c, "drink") || !strncmp(c, "drink ", 6)) drink_cmd(c[5] ? c + 6 : "");   /* 喝水提醒: see scr_drink.c / drink_svc.h */
    else if (!strcmp(c, "habit") || !strncmp(c, "habit ", 6)) habit_cmd(c[5] ? c + 6 : "");   /* 習慣打卡 page: see scr_habits.c */
    else if (!strcmp(c, "counter") || !strncmp(c, "counter ", 8)) counter_cmd(c[7] ? c + 8 : "");   /* 多組計數器 page: see scr_counter.c */
    else if (!strcmp(c, "tuner") || !strncmp(c, "tuner ", 6)) tuner_cmd(c[5] ? c + 6 : "");   /* the 調音器 pages: see scr_tuner.c */
    else if (!strcmp(c, "sfxbank") || !strncmp(c, "sfxbank ", 8)) sfxbank_cmd(c[7] ? c + 8 : "");   /* the 音效庫 page: rows | tap <name|row> (scr_sfxbank.c) */
    else if (!strcmp(c, "board") || !strncmp(c, "board ", 6)) ui_board_cmd(c[5] ? c + 6 : "");   /* the shared board of the games: see ui_board.h */
    else if (!strcmp(c, "sectors") || !strncmp(c, "sectors ", 8)) ui_sectors_cmd(c[7] ? c + 8 : "");   /* the shared sectors of the games (G1): see ui_sectors.h */
    else if (!strcmp(c, "games") || !strncmp(c, "games ", 6)) games_cmd(c[5] ? c + 6 : "");     /* 遊戲: the hub and its games, see games.h */
    else if (!strcmp(c, "cards") || !strncmp(c, "cards ", 6)) ui_cards_cmd(c[5] ? c + 6 : "");   /* the shared piles of cards / tiles of the card games: see ui_cards.h */
    else if (!strcmp(c, "dial") || !strncmp(c, "dial ", 5)) ui_dial_cmd(c[4] ? c + 5 : "");   /* the shared dial of the gauge toys and 保險箱開鎖: see ui_dial.h */
    else if (!strcmp(c, "beat") || !strncmp(c, "beat ", 5)) ui_beat_cmd(c[4] ? c + 5 : "");   /* the shared beat feed (pc_beat over the spectrum): see ui_beat.h */
    else if (!strcmp(c, "maze") || !strncmp(c, "maze ", 5)) knobtoy_maze_cmd(c[4] ? c + 5 : "");   /* G6b: the knob toys' marble maze: grav <dx> <dy> (games.h) */
    else if (!strcmp(c, "arena") || !strncmp(c, "arena ", 6)) ui_arena_cmd(c[5] ? c + 6 : "");   /* the shared play field of the action games: see ui_arena.h */
    else if (!strncmp(c, "cardtest", 8) && (!c[8] || c[8] == ' ')) cardtest_cmd(c[8] ? c + 9 : "");   /* 記憶卡診斷: raw | file (aliases read | write) | tap raw|file | show (card_test.c) */
    else if (!strcmp(c, "codepad") || !strncmp(c, "codepad ", 8)) ui_code_pad_cmd(c[7] ? c + 8 : "");   /* cardfmt: the code pad, see ui_code_pad.h */
    else if (!strcmp(c, "cardfmt") || !strncmp(c, "cardfmt ", 8)) scr_tfcard_cmd(c[7] ? c + 8 : "");     /* cardfmt: the card page and the format flow (scr_card.c) */
    else if (!strcmp(c, "vocab") || !strncmp(c, "vocab ", 6)) vocab_cmd(c[5] ? c + 6 : "");         /* 單字: see scr_vocab.c */
    else if (!strcmp(c, "fontinfo")) font_info();
    else if (!strcmp(c, "fontmiss")) font_miss_print();
    else if (!strcmp(c, "fontmiss clear")) { font_miss_clear(); font_miss_print(); }
    else if (sscanf(c, "fontbench %d", &n) == 1 && n > 20 && n < 100) font_bench(n);
    else if (sscanf(c, "refrbench %d", &n) == 1 && n > 0 && n <= 100) refr_bench(n);
    else printf("APP cmd unknown\n");
    free(c);
}

/* Lines run on the LVGL task in the order they arrived (board batch A F2: one lv_async_call per line ran them newest-first, a new
 * LVGL timer goes to the head of its list). One FIFO under the display lock (a recursive mutex; the LVGL task holds it while timers
 * run), drained by one async call that runs every line queued, also those queued while it runs. A line is two small heap blocks
 * (the node, the strdup'd text the command frees); a line that cannot be queued is refused with a console line, never dropped
 * silently. Before the UI task runs, lines wait in the FIFO. */
typedef struct line_node { struct line_node *next; char *text; } line_node_t;
#define LINES_MAX 64                       /* waiting lines; more is a runaway sender */
static line_node_t *s_lines, **s_lines_end = &s_lines;
static int s_lines_n;
static bool s_lines_armed;                 /* a drain is scheduled */

static void lines_drain(void *arg)
{
    (void)arg;
    for (;;) {
        bsp_display_lock(-1);
        line_node_t *l = s_lines;
        if (l) {
            s_lines = l->next;
            if (!s_lines) s_lines_end = &s_lines;
            s_lines_n--;
        } else s_lines_armed = false;      /* the next line schedules a new drain */
        bsp_display_unlock();
        if (!l) return;
        char *t = l->text;
        free(l);
        if (t[0] == '{') ctl_run(t);       /* '{': the settings page (not echoed: may hold a password) */
        else run(t);                       /* both free the text */
    }
}

/* One command line, dispatched exactly like a line from the serial port: '{' goes to the settings protocol,
 * anything else to run(); both on the LVGL task, in arrival order (the desktop simulator calls this directly). */
void console_exec(const char *line)
{
    if (!line || !line[0]) return;
    line_node_t *l = malloc(sizeof *l);
    char *dup = l ? strdup(line) : NULL;
    bsp_display_lock(-1);
    bool full = s_lines_n >= LINES_MAX;
    if (dup && !full) {
        l->next = NULL;
        l->text = dup;
        *s_lines_end = l;
        s_lines_end = &l->next;
        s_lines_n++;
        if (!s_lines_armed) s_lines_armed = lv_async_call(lines_drain, NULL) == LV_RESULT_OK;   /* failed: the line waits, the next one tries again */
        if (!s_lines_armed) printf("APP cmd queue: no drain scheduled, %d lines waiting\n", s_lines_n);
    }
    bsp_display_unlock();
    if (dup && !full) return;
    printf("APP cmd refused: %s\n", full ? "too many lines waiting" : "no memory");   /* the text is not echoed: a '{' line may hold a password */
    free(dup);
    free(l);
}

#ifndef SIM   /* the serial half: file transfer, the console task, the USB-Serial-JTAG driver */
/* "put <path>" then base64 lines, then "end": writes a file on the TF card (runs on this task, not LVGL) */
static int b64val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}
static size_t b64dec(const char *in, uint8_t *out, size_t cap)
{
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (; *in; in++) {
        int v = b64val((unsigned char)*in);
        if (v < 0) continue;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; if (n == cap) return cap + 1; out[n++] = (uint8_t)(acc >> bits); }   /* too long: never past out */
    }
    return n;
}
static void mkdirs(const char *path)
{
    char p[160];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 8; *s; s++)                 /* after "/sdcard/" */
        if (*s == '/') { *s = 0; mkdir(p, 0777); *s = '/'; }
}


/* serial check (an internal note, tools/board/ser_stress.py): "ping N" -> "APP pong N" from this
 * task; "serstat" counts console replies that printf could not hand to the USB-Serial-JTAG driver (EIO = the port
 * looked unplugged; usj_link.c fixed the false ones) */
static uint32_t s_pr_fail, s_pr_errno;
static void serstat(bool reset)
{
    printf("APP serstat printf_fail=%lu errno=%lu\n", (unsigned long)s_pr_fail, (unsigned long)s_pr_errno);
    if (reset) s_pr_fail = s_pr_errno = 0;
}

/* "pput": resumable upload (tools/board/tf_put.py; log an internal note). Writes <path>.new,
 * every chunk carries its offset and every reply the size of <path>.new, so a resend after a lost reply is harmless
 * (pc_put_decide) and the host always knows where to go on:
 *   pput [-f] /sdcard/<path>     open <path>.new (kept from an earlier try unless -f)  -> APP pput ready size=N
 *   @<off> <n> <base64>          n <= 2048 raw bytes                                   -> APP pput ok|gap|bad|err size=N
 *   pput ?                                                                             -> APP pput size=N
 *   pput end <sha256 hex>        whole-file check, then <path>.new replaces <path>     -> APP pput done|badsum size=N sha=...
 *                                (not while <path> is open on the screen as a deck/list   -> APP pput busy ... size=N)
 *   pput abort                   removes <path>.new                                    -> APP pput aborted
 * Any other line, or 10 s of silence, closes <path>.new (kept: the next pput resumes it). */
static struct { FILE *f; uint32_t cur; char path[160], tmp[168]; } P;   /* main's .bss: PSRAM (linker.lf) */

static bool file_sha(const char *path, char hex[65], uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    setvbuf(f, NULL, _IONBF, 0);
    EXT_RAM_BSS_ATTR static uint8_t b[8192];
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    psa_hash_setup(&op, PSA_ALG_SHA_256);
    size_t n;
    uint32_t total = 0;
    while ((n = fread(b, 1, sizeof b, f)) > 0) { psa_hash_update(&op, b, n); total += n; }
    fclose(f);
    uint8_t h[32];
    size_t hl = 0;
    psa_hash_finish(&op, h, sizeof h, &hl);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    *size = total;
    return true;
}

/* is <path> open as a glyph pack on the screen (a deck in the quiz, a list)? Asked on the LVGL task, which owns that
 * list; no answer within 2 s counts as yes */
static struct { const char *path; bool busy; SemaphoreHandle_t done; } s_use;
static void in_use_ui(void *arg) { (void)arg; s_use.busy = gt_pack_in_use(s_use.path); xSemaphoreGive(s_use.done); }
static bool pack_in_use(const char *path)
{
    if (!s_use.done && !(s_use.done = xSemaphoreCreateBinary())) return true;
    s_use.path = path;
    s_use.busy = true;
    ui_async(in_use_ui, NULL);
    return xSemaphoreTake(s_use.done, pdMS_TO_TICKS(2000)) != pdTRUE || s_use.busy;
}

static void pput_close(const char *why)
{
    if (!P.f) return;
    fclose(P.f);
    P.f = NULL;
    printf("APP pput closed (%s) size=%lu\n", why, (unsigned long)P.cur);
}

static void pput_cmd(char *a)
{
    if (!strcmp(a, "?")) { printf("APP pput size=%lu\n", (unsigned long)P.cur); return; }
    if (!strcmp(a, "abort")) {
        pput_close("abort");
        if (P.tmp[0]) remove(P.tmp);
        P.tmp[0] = 0;
        printf("APP pput aborted\n");
        return;
    }
    if (!strncmp(a, "end ", 4)) {
        if (!P.tmp[0]) { printf("APP pput fail (no upload)\n"); return; }
        if (P.f) { fclose(P.f); P.f = NULL; }
        char hex[65] = "";
        uint32_t size = 0;
        if (pack_in_use(P.path)) { printf("APP pput busy (open on the screen: leave it, then end again) size=%lu\n", (unsigned long)P.cur); return; }
        int64_t t0 = esp_timer_get_time();
        bool ok = file_sha(P.tmp, hex, &size) && !strcmp(hex, a + 4);
        if (ok) { remove(P.path); ok = !rename(P.tmp, P.path); }   /* FAT rename does not replace */
        else remove(P.tmp);                                          /* bad: the next try starts from 0 */
        printf("APP pput %s size=%lu sha=%s ms=%lld\n", ok ? "done" : "badsum", (unsigned long)size, hex, (esp_timer_get_time() - t0) / 1000);
        P.tmp[0] = 0;
        P.cur = 0;
        return;
    }
    bool fresh = !strncmp(a, "-f ", 3);
    if (fresh) a += 3;
    pput_close("new pput");
    if (strncmp(a, "/sdcard/", 8) || strlen(a) >= sizeof P.path) { printf("APP pput fail (path)\n"); return; }
    snprintf(P.path, sizeof P.path, "%s", a);
    snprintf(P.tmp, sizeof P.tmp, "%s.new", a);
    mkdirs(P.path);
    if (fresh) remove(P.tmp);
    P.f = fopen(P.tmp, "ab");
    struct stat st;
    P.cur = P.f && !stat(P.tmp, &st) ? (uint32_t)st.st_size : 0;
    if (P.f) fseek(P.f, 0, SEEK_END);
    else P.tmp[0] = 0;
    printf("APP pput %s size=%lu\n", P.f ? "ready" : "fail", (unsigned long)P.cur);
}

static void pput_chunk(const char *line)
{
    EXT_RAM_BSS_ATTR static uint8_t bin[PC_PUT_CHUNK_MAX];
    pc_put_chunk_t c;
    const char *r = "bad";
    int n;
    if (!P.f) r = "fail";
    else if (pc_put_parse(line, &c) || (n = pc_b64_decode(c.b64, bin, sizeof bin)) != (int)c.n) r = "bad";
    else switch (pc_put_decide(P.cur, c.off, c.n)) {
        case PC_PUT_WRITE:
            if (fwrite(bin, 1, c.n, P.f) == c.n) { P.cur += c.n; r = "ok"; }
            else { fflush(P.f); long t = ftell(P.f); P.cur = t > 0 ? (uint32_t)t : 0; r = "err"; }
            break;
        case PC_PUT_HAVE: r = "ok"; break;
        default: r = "gap"; break;
    }
    if (printf("APP pput %s size=%lu\n", r, (unsigned long)P.cur) < 0) { s_pr_fail++; s_pr_errno = errno; }
}

static void task(void *arg)
{
    (void)arg;
    EXT_RAM_BSS_ATTR static char line[100 * 1024];   /* JSON requests carry station lists and TOML */
    EXT_RAM_BSS_ATTR static uint8_t bin[800];
    size_t len = 0;
    FILE *put = NULL;
    size_t put_bytes = 0;
    int idle = 0;
    for (;;) {
        static uint8_t rx[256];
        static int rx_n, rx_i;
        if (rx_i >= rx_n) {
#if CONFIG_ESP_CONSOLE_UART
            rx_n = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, rx, sizeof rx, pdMS_TO_TICKS(200));   /* peer bench build (make build-peer) */
#else
            rx_n = usb_serial_jtag_read_bytes(rx, sizeof rx, pdMS_TO_TICKS(200));
#endif
            rx_i = 0;
            if (rx_n <= 0) {
                rx_n = 0;
                if ((put || P.f) && ++idle > 50 && put) { fclose(put); put = NULL; printf("APP put abandoned (silence) bytes=%u\n", (unsigned)put_bytes); }   /* ~10 s */
                if (P.f && idle > 50) { pput_close("silence"); idle = 0; }
                continue;
            }
            idle = 0;
        }
        uint8_t ch = rx[rx_i++];
        if (ch != '\r' && ch != '\n') { if (len < sizeof line - 1) line[len++] = (char)ch; continue; }
        line[len] = 0;
        len = 0;
        if (cast_mode_active() && (line[0] == '@' || !strncmp(line, "pput ", 5) || !strncmp(line, "put /sdcard/", 12) || !strncmp(line, "rm /sdcard/", 11))) {   /* the broadcast radio is play-only: no file written or removed from the serial line either */
            printf("APP refused: the broadcast radio is play-only\n");
            continue;
        }
        if (line[0] == '@') { pput_chunk(line); continue; }
        if (!strncmp(line, "pput ", 5)) { pput_cmd(line + 5); continue; }
        if (P.f && line[0]) pput_close("other command");
        if (put && (line[0] == '{' || !strncmp(line, "put ", 4))) {   /* the uploader went away without "end" */
            fclose(put); put = NULL; printf("APP put abandoned bytes=%u\n", (unsigned)put_bytes);
        }
        if (put) {
            if (!strcmp(line, "end")) { fclose(put); put = NULL; printf("APP put done bytes=%u\n", (unsigned)put_bytes); }
            else if (line[0]) {                  /* "<bytes> <base64>": checked, the host resends on "bad" */
                char *sp = strchr(line, ' ');
                size_t want = (size_t)atoi(line), n = sp ? b64dec(sp + 1, bin, sizeof bin) : 0;
                if (!sp || n != want) { printf("APP put bad\n"); continue; }
                put_bytes += fwrite(bin, 1, n, put);
                if (printf("APP put ok\n") < 0) { s_pr_fail++; s_pr_errno = errno; }
            }
            continue;
        }
        if (!strncmp(line, "put /sdcard/", 12)) {
            mkdirs(line + 4);
            put = fopen(line + 4, "wb");
            put_bytes = 0;
            printf("APP put %s\n", put ? "ready" : "fail");
            continue;
        }
        if (!strncmp(line, "get /sdcard/", 12) || !strncmp(line, "sha /sdcard/", 12)) { get_file(line + 4, line[0] == 'g'); continue; }
        if (!strncmp(line, "tfread ", 7)) { tfread(line + 7); continue; }
        if (!strncmp(line, "ping ", 5)) { if (printf("APP pong %s\n", line + 5) < 0) { s_pr_fail++; s_pr_errno = errno; } continue; }
        if (!strcmp(line, "serstat")) { serstat(false); continue; }
        if (!strcmp(line, "serstat reset")) { serstat(true); continue; }
        if (!strncmp(line, "rm /sdcard/", 11)) { printf("APP rm rc=%d\n", remove(line + 3)); continue; }
        if (!strncmp(line, "ls /sdcard", 10)) {
            DIR *d = opendir(line + 3);
            struct dirent *e;
            while (d && (e = readdir(d))) {
                char fp[300];
                struct stat st;
                snprintf(fp, sizeof fp, "%s/%s", line + 3, e->d_name);
                printf("APP ls %s size=%ld\n", e->d_name, stat(fp, &st) ? -1L : (long)st.st_size);
            }
            if (d) closedir(d);
            printf("APP ls end\n");
            continue;
        }
        if (line[0] == '{' && console_app_cmd) {
            if (cast_mode_active()) printf("\nCTL {\"ok\":false,\"error\":\"%s\"}\n", T(S_CAST_NO_SETTINGS) /* 廣播電台模式不能改設定，請回播放器 */);   /* the broadcast radio is play-only: no settings, no files (the serial put / pput / rm and cfgset / cfgdel are refused above), no status either */
            else printf("APP ctl unavailable in this app\n");
            continue;
        }
        console_exec(line);
    }
}

void console_start(void)
{
#if CONFIG_ESP_CONSOLE_UART
    if (uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 4096, 0, 0, NULL, 0) != ESP_OK) { printf("APP console fail\n"); return; }
#else
    usb_serial_jtag_driver_config_t c = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    c.rx_buffer_size = 4096;   /* a whole upload chunk fits */
    if (usb_serial_jtag_driver_install(&c) != ESP_OK) { printf("APP console fail\n"); return; }
    usb_serial_jtag_vfs_use_driver();
#endif
    xTaskCreatePinnedToCoreWithCaps(task, "console", 4096, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);   /* PSRAM stack (PSR, 4 KB internal): no command here writes flash/NVS on this task (those go through flash_safe_call or the BT task); the LVGL task and audio do the same */
}
#endif   /* !SIM */
