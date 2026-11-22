// SPDX-License-Identifier: LGPL-2.1-or-later
#include "crashlog.h"
#include "pc/pc_paths.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_private/panic_internal.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "guard.h"
#include "pc/pc_crash.h"
#include "riscv/rvruntime-frames.h"
#include "sdkconfig.h"

#define DIR_       PC_LOGS_DIR
#define PATH       DIR_ "/crash.txt"
#define FAIL_PATH  DIR_ "/.no_such_dir/crash.txt"   /* crashtest fail: a directory that is never created */
#define FLAG       PC_FLAG_CRASHTEST
#define MAX_FILE   (64 * 1024)
#define KEEP       (32 * 1024)
#define TAIL_LINES 10
#define REASON_TAG 0xC4A50000u                       /* s_pending_reason = tag | esp_reset_reason() */
#define TEST_TAG   0xC7E57000u                       /* s_test = tag | variant: the next boot only */
enum { T_NONE, T_NOSD, T_KEEP, T_FAIL, T_EARLY, T_N };

/* RTC memory survives a panic / watchdog reset; after power-on it is garbage, so every read is validated */
static RTC_NOINIT_ATTR pc_crash_t s_live;            /* this boot's breadcrumbs (+ PC/RA from the panic wrapper) */
static RTC_NOINIT_ATTR pc_crash_t s_pending;         /* the last crash, not yet printed / written */
static RTC_NOINIT_ATTR uint32_t s_pending_reason;    /* reset reason of that crash (a later boot has its own) */
static RTC_NOINIT_ATTR uint32_t s_test, s_test_inv;  /* crashtest variant for the next boot; s_test_inv = ~s_test */
static DRAM_ATTR volatile uint8_t s_in_panic;        /* internal RAM (this component's .bss is in PSRAM) */
static int s_boot_test;                              /* this boot's variant, taken from s_test at boot */
static volatile uintptr_t s_null_addr;               /* 0; a literal NULL store would be deleted by the compiler as UB */

static bool valid(const pc_crash_t *c)
{
    return c->magic == PC_CRASH_MAGIC && c->mode <= PC_CRASH_MODE_CAST && c->has_pc <= 1;
}

static void fresh(pc_crash_t *c)
{
    *c = (pc_crash_t){ .magic = PC_CRASH_MAGIC };
}

/* The panic entry (IDF's handler runs from flash in this config; with CONFIG_ESP_PANIC_HANDLER_IRAM so does this).
 * RTC writes only: no allocation, no printing, no locks; a panic inside the panic goes straight on. */
void __real_esp_panic_handler(panic_info_t *info);
#if CONFIG_ESP_PANIC_HANDLER_IRAM
IRAM_ATTR
#endif
void __wrap_esp_panic_handler(panic_info_t *info)
{
    if (!s_in_panic) {
        s_in_panic = 1;
        if (!valid(&s_live)) fresh(&s_live);         /* a crash before crashlog_boot() after power-on */
        const RvExcFrame *f = info ? (const RvExcFrame *)info->frame : NULL;
        s_live.pc = info ? (uint32_t)(uintptr_t)info->addr : 0;
        s_live.ra = f ? (uint32_t)f->ra : 0;
        s_live.cause = f ? (uint32_t)f->mcause : 0;
        s_live.has_pc = 1;
    }
    __real_esp_panic_handler(info);
}

/* crashtest: a store to address 0 from our own code, so the PC names this function (abort() would land in IDF) */
__attribute__((noinline, noipa)) void crashlog_crash_here(void)
{
    *(volatile uint32_t *)s_null_addr = 0xDEADu;
    __asm__ volatile("unimp");                        /* only if the store did not fault */
}

void crashlog_boot(void)
{
    uint32_t t = s_test, inv = s_test_inv;
    s_test = s_test_inv = 0;                          /* cleared before it acts: a variant never repeats */
    s_boot_test = inv == ~t && (t & 0xFFFFFF00u) == TEST_TAG && (t & 0xFFu) < T_N ? (int)(t & 0xFFu) : T_NONE;
    if (guard_last_reset_was_crash() && valid(&s_live)) {
        s_pending = s_live;
        s_pending_reason = REASON_TAG | ((uint32_t)esp_reset_reason() & 0xFFFFu);
    }
    fresh(&s_live);
    if (s_boot_test == T_EARLY) {                     /* crash inside the first seconds: flag already cleared above */
        s_boot_test = T_NONE;
        printf("APP crashtest early: crashing again now\n");
        fflush(stdout);
        crashlog_crash_here();
    }
}

void crashlog_mode(int mode)
{
    if (mode >= PC_CRASH_MODE_UNKNOWN && mode <= PC_CRASH_MODE_CAST) s_live.mode = (uint8_t)mode;
}

void crashlog_crumb(const char *screen, int bt_mode, int src)
{
    size_t i = 0;
    for (; screen && i < sizeof s_live.screen && screen[i]; i++) s_live.screen[i] = screen[i];
    for (; i < sizeof s_live.screen; i++) s_live.screen[i] = 0;
    s_live.bt_mode = (uint8_t)bt_mode;
    s_live.src = (uint8_t)src;
    s_live.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    s_live.internal_min = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    time_t now = time(NULL);
    s_live.wall = now > 1700000000 ? (uint32_t)now : 0;   /* before SNTP the clock starts near 1970 */
}

/* 0 ok; the file is cut to its last KEEP bytes (whole lines) once it is over MAX_FILE */
static int write_line(const char *path, const char *line, int n)
{
    if (pc_int_mkdir(DIR_) != 0 && errno != EEXIST) return -1;
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    bool ok = fwrite(line, 1, (size_t)n, f) == (size_t)n;
    long size = ok && fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (fclose(f) != 0 || !ok || size < 0) return -1;
    if (size <= MAX_FILE) return 0;
    size_t m = KEEP + 1;                              /* one byte before the kept part: pc_crash_tail sees a line start */
    char *buf = heap_caps_malloc(m, MALLOC_CAP_SPIRAM);
    if (!buf) return -1;
    int rc = -1;
    f = fopen(path, "rb");
    if (f && fseek(f, size - (long)m, SEEK_SET) == 0 && fread(buf, 1, m, f) == m) {
        fclose(f);
        size_t start = pc_crash_tail(buf, m, KEEP);
        f = fopen(path, "wb");
        if (f) {
            ok = fwrite(buf + start, 1, m - start, f) == m - start;
            rc = fclose(f) == 0 && ok ? 0 : -1;
        }
    } else if (f) fclose(f);
    heap_caps_free(buf);
    return rc;
}

void crashlog_flush(bool sd_ok)
{
    if (!valid(&s_pending) || (s_pending_reason & 0xFFFF0000u) != REASON_TAG) { s_pending.magic = 0; return; }
    if (s_boot_test == T_KEEP) {                      /* the USB-mode path: left for the next boot */
        s_boot_test = T_NONE;
        printf("APP crashtest keep: crash line left for the next boot\n");
        return;
    }
    char line[256];
    int n = pc_crash_format(&s_pending, (int)(s_pending_reason & 0xFFFFu), esp_app_get_description()->version, line, sizeof line);
    s_pending.magic = 0;                              /* before any file access: a fault while writing never repeats */
    int test = s_boot_test;
    s_boot_test = T_NONE;
    if (!n) { printf("APP crash log format failed\n"); return; }
    printf("APP crash %s", line);
    if (test == T_NOSD) sd_ok = false;
    if (sd_ok && write_line(test == T_FAIL ? FAIL_PATH : PATH, line, n) != 0) printf("APP crash log write failed\n");
}

void crashlog_print_tail(void)
{
    FILE *f = fopen(PATH, "rb");
    long size = f && fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    size_t m = size > 4096 ? 4096 : size > 0 ? (size_t)size : 0;   /* 10 lines of ~200 bytes */
    char *buf = m ? heap_caps_malloc(m, MALLOC_CAP_SPIRAM) : NULL;
    int k = 0;
    if (buf && fseek(f, size - (long)m, SEEK_SET) == 0 && fread(buf, 1, m, f) == m) {
        size_t start = buf[m - 1] == '\n' ? m - 1 : m;    /* walk back over TAIL_LINES line starts */
        for (int lines = 0; start > 0; start--)
            if (buf[start - 1] == '\n' && ++lines == TAIL_LINES) break;
        if (start == 0 && size > (long)m) {            /* the window cut its first line: skip that part */
            while (start < m && buf[start] != '\n') start++;
            start++;
        }
        for (size_t i = start; i < m;) {
            size_t j = i;
            while (j < m && buf[j] != '\n') j++;
            if (j > i) { printf("APP crashlog %.*s\n", (int)(j - i), buf + i); k++; }
            i = j + 1;
        }
    }
    heap_caps_free(buf);
    if (f) fclose(f);
    printf("APP crashlog end lines=%d%s\n", k, f ? "" : " (no " PATH ")");
}

void crashlog_test(const char *arg)
{
    struct stat st;
    if (stat(FLAG, &st) != 0) { printf("APP crashtest needs " FLAG "\n"); return; }
    if (guard_crashes() != 0) { printf("APP crashtest wait for guard healthy\n"); return; }   /* from 1, early would reach safe mode */
    static const char *const names[T_N] = { "", "nosd", "keep", "fail", "early" };
    if (arg && !strcmp(arg, "safe")) {                /* same gates as above; the next boot counts this crash as the third */
        guard_arm_for_test();
        if (guard_crashes() == 0) { printf("APP crashtest safe: counter not armed\n"); return; }
        s_test = s_test_inv = 0;
        printf("APP crashtest safe: next boot is safe mode; leave it with a host reset or a reflash\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
        crashlog_crash_here();
        return;
    }
    int v = -1;
    for (int i = 0; i < T_N; i++) if (!strcmp(arg ? arg : "", names[i])) v = i;
    if (v < 0) { printf("APP crashtest usage: crashtest [nosd|keep|fail|early|safe]\n"); return; }
    s_test = v ? TEST_TAG | (uint32_t)v : 0;
    s_test_inv = ~s_test;
    printf("APP crashtest %s: crashing now\n", v ? names[v] : "plain");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));                   /* let the console line out first */
    crashlog_crash_here();
}
