// SPDX-License-Identifier: LGPL-2.1-or-later
/* The file half of the games' saved numbers (gamesave_svc.h). Statics: pointers and counters only (this file is built with -msmall-data-limit=0: PSRAM .bss). */
#include "gamesave_svc.h"
#include "pc/pc_paths.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cfg.h"
#include "lowmem.h"
#include "lvgl.h"
#include "pc/pc_gamesave.h"
#include "pc/pc_lowmem.h"
#include "psram_alloc.h"

#define GS_PATH PC_CFG_DIR "/games.txt"
static pc_gs_t *s_gs;
static char s_status[16];
static int s_writes;
static bool s_bad, s_full, s_ro;

static void status(const char *s) { snprintf(s_status, sizeof s_status, "%s", s); }
const char *gamesave_status(void) { return s_status[0] ? s_status : "ok"; }

static int s_fail_rename;                               /* console `gamesave failrename <n>`: the next n renames fail (tests of the power-cut paths) */
static bool s_tmp_pending;                              /* games.txt was removed and its .tmp could not be renamed: the .tmp is the only good copy */
static int do_rename(const char *from, const char *to)
{
    if (s_fail_rename > 0) { s_fail_rename--; return -1; }
    return rename(from, to);
}

/* games.txt is replaced by a complete, renamed .tmp. FatFs refuses a rename over an existing file: then games.txt is removed and the .tmp renamed. From the
 * remove on, the .tmp is the only good copy: it is never deleted again (gamesave_start recovers it), and the next save puts it in place before it writes. */
static bool write_file(void)
{
    if (s_tmp_pending) {
        if (do_rename(GS_PATH ".tmp", GS_PATH)) return false;       /* still no games.txt: keep the .tmp untouched, do not overwrite it */
        s_tmp_pending = false;
    }
    char *buf = psram_malloc(PC_GS_FILE_MAX + 64);
    if (!buf) { status("nomem"); return false; }
    size_t n = pc_gs_format(s_gs, buf, PC_GS_FILE_MAX + 64);
    bool ok = n > 0;
    if (ok) {
        pc_int_mkdir(PC_CFG_DIR);
        FILE *f = fopen(GS_PATH ".tmp", "wb");
        ok = f != NULL;
        if (f) { ok = fwrite(buf, 1, n, f) == n; ok &= fclose(f) == 0; }
        if (!ok) remove(GS_PATH ".tmp");                           /* games.txt is still the good copy here */
        else if (do_rename(GS_PATH ".tmp", GS_PATH)) {
            remove(GS_PATH);
            if (do_rename(GS_PATH ".tmp", GS_PATH)) { ok = false; s_tmp_pending = true; }   /* no delete of the .tmp, ever, after the remove */
        }
    }
    psram_free(buf);
    return ok;
}

static bool save(uint32_t now, const char *why)
{
    if (!s_gs || !s_gs->dirty) return true;
    if (s_ro) return false;                                      /* the file could not be read at boot: what is on the card is never overwritten */
    if (!g_sd_ok) { status("no_card"); pc_gs_written(s_gs, now); return false; }   /* memory only: nothing to retry until the next change */
    bool ok = write_file();
    if (ok) { pc_gs_written(s_gs, now); s_writes++; if (!strcmp(s_status, "write_failed")) status(s_bad ? "corrupt" : "ok"); }
    else if (strcmp(s_status, "nomem")) status("write_failed");
    printf("APP gamesave saved ok=%d why=%s keys=%d\n", ok, why, s_gs->n);
    return ok;
}

void gamesave_flush(void) { save(lv_tick_get(), "flush"); }

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    if (!s_gs || !pc_gs_due(s_gs, now, GAMESAVE_DEBOUNCE_MS)) return;
    if (lowmem_level() >= PC_LOWMEM_NO_BACKGND) { printf("APP gamesave save deferred reason=lowmem\n"); return; }
    save(now, "debounce");
}

enum { RP_ABSENT = -100, RP_IOERR = -101, RP_NOMEM = -102 };
/* Reads and parses one file into the store: keys read (>= 0), a PC_GS_BAD_* (corrupt, the store left empty), or RP_*. */
static int read_parse(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? RP_ABSENT : RP_IOERR;
    char *buf = psram_malloc(PC_GS_FILE_MAX + 2);
    if (!buf) { fclose(f); return RP_NOMEM; }
    size_t n = fread(buf, 1, PC_GS_FILE_MAX + 1, f);
    bool err = ferror(f) != 0;
    fclose(f);
    int r = err ? RP_IOERR : pc_gs_parse(s_gs, buf, n);
    psram_free(buf);
    return r;
}

void gamesave_start(void)
{
    s_gs = psram_calloc(1, sizeof *s_gs);
    if (!s_gs) { status("nomem"); s_ro = true; printf("APP gamesave no memory\n"); return; }
    pc_gs_init(s_gs);
    if (!g_sd_ok) status("no_card");
    else {
        int r = read_parse(GS_PATH);
        if (r == RP_IOERR || r == RP_NOMEM) { s_ro = true; status(r == RP_NOMEM ? "nomem" : "read_failed"); }    /* never "ok, empty": a save would overwrite good data */
        else if (r < 0) {                                           /* absent or corrupt: a complete .tmp (a power cut between the remove and the rename) is the newer copy */
            int rt = read_parse(GS_PATH ".tmp");
            if (rt == RP_IOERR || rt == RP_NOMEM) { s_ro = true; status(rt == RP_NOMEM ? "nomem" : "read_failed"); }
            else if (rt >= 0) {
                if (r != RP_ABSENT) { remove(GS_PATH ".bad"); s_bad = rename(GS_PATH, GS_PATH ".bad") == 0; }
                int rr = rename(GS_PATH ".tmp", GS_PATH);
                printf("APP gamesave recovered from .tmp keys=%d rename=%d\n", rt, rr);
                if (rr) s_tmp_pending = true;                       /* the next save tries again before it writes */
            } else if (r != RP_ABSENT) {                            /* corrupt: start empty, keep the file as .bad (the latest one) */
                remove(GS_PATH ".bad");
                s_bad = rename(GS_PATH, GS_PATH ".bad") == 0;
                status("corrupt");
                printf("APP gamesave corrupt rc=%d kept_bad=%d\n", r, s_bad);
            }
        }
    }
    printf("APP gamesave start status=%s keys=%d\n", gamesave_status(), s_gs->n);
    lv_timer_create(timer_cb, 1000, NULL);
}

bool gamesave_get(const char *key, uint32_t *v) { return s_gs && pc_gs_get(s_gs, key, v); }
uint32_t gamesave_get_or(const char *key, uint32_t def) { return s_gs ? pc_gs_get_or(s_gs, key, def) : def; }

bool gamesave_set(const char *key, uint32_t v)
{
    if (!s_gs) return false;
    bool ok = pc_gs_set(s_gs, key, v);
    if (!ok && pc_gs_valid_key(key)) { s_full = true; status("full"); }
    return ok;
}

void gamesave_load_score(const char *id, pc_game_score_t *s) { if (s_gs) pc_gs_load_score(s_gs, id, s); }

void gamesave_store_score(const char *id, const pc_game_score_t *s)
{
    if (!s_gs) return;
    if (!pc_gs_store_score(s_gs, id, s)) { s_full = true; status("full"); printf("APP gamesave refused a key of %s (full or id too long)\n", id); }
}

void gamesave_cmd(const char *a)
{
    char k[PC_GS_KEY_MAX + 4];
    unsigned v;
    if (!s_gs) { printf("APP gamesave none\n"); return; }
    if (!strcmp(a, "dump")) { for (int i = 0; i < s_gs->n; i++) printf("APP gamesave %s=%u\n", s_gs->e[i].key, (unsigned)s_gs->e[i].val); }
    else if (!strcmp(a, "flush")) gamesave_flush();
    else if (sscanf(a, "failrename %u", &v) == 1) s_fail_rename = (int)v;
    else if (!strcmp(a, "clear")) { pc_gs_init(s_gs); s_gs->dirty = true; s_full = false; }
    else if (sscanf(a, "get %23s", k) == 1) { uint32_t x; if (gamesave_get(k, &x)) printf("APP gamesave %s=%u\n", k, (unsigned)x); else printf("APP gamesave %s none\n", k); }
    else if (sscanf(a, "set %23s %u", k, &v) == 2) printf("APP gamesave set %s=%u ok=%d\n", k, v, gamesave_set(k, v));
    else if (a[0]) { printf("APP gamesave unknown %s\n", a); return; }
    printf("APP gamesave status=%s keys=%d dirty=%d writes=%d card=%d bad=%d full=%d ro=%d pending=%d\n", gamesave_status(), s_gs->n, s_gs->dirty, s_writes, g_sd_ok, s_bad, s_full, s_ro, s_tmp_pending);
}
