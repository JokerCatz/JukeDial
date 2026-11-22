// SPDX-License-Identifier: LGPL-2.1-or-later
#include "chip_ctl.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "audio_svc.h"
#include "backend_gme.h"
#include "cfg.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "pc/pc_backend.h"
#include "pc/pc_chipseq.h"
#include "pc/pc_failstreak.h"
#include "pc/pc_fmtreason.h"
#include "pc/pc_zip.h"
#include "player_ctl.h"
#include "stats_svc.h"
#include "font.h"
#include "ui.h"
#include "screens.h"

/* A system = one opened <system>.zip. Browsing and playing each hold one (the same one when they match), so
 * looking around another system's games does not stop what is playing. */
typedef struct {
    char name[64];
    FILE *zf;
    pc_reader_t zr;
    pc_zip_entry_t *ents;
    int nents, *files, nfiles;
    int cache_file, cache_tracks;          /* last extracted entry */
    const pc_backend_t *cache_be;          /* its decoder: NULL = Game_Music_Emu, else a pure backend (HVL, ...) */
    uint8_t *cache;
    uint32_t cache_len;
    char game[96];
} chipsys_t;
static chipsys_t *s_b, *s_p;               /* browsing, playing */
/* playing */
static pc_chippos_t s_pos = { -1, 0 };
static uint32_t s_gen;
static pc_failstreak_t s_fails;
EXT_RAM_BSS_ATTR static char s_why[128];     /* the reason the last load() refused a file (honest disclosure); chip_play shows it, then clears it */
static int s_play_tracks;   /* sub-track count of the playing file */
static char s_game[96];     /* the playing file's game name */

static const now_ops_t s_ops;
static void on_event(const aud_event_t *e);
static void forget(void) { s_pos.file = -1; }

/* the zip entry of the file-th playable file; NULL for an index outside the list (console, favourites and stale positions give any number) */
static const pc_zip_entry_t *fentry(const chipsys_t *y, int file)
{
    return y && file >= 0 && file < y->nfiles ? pc_zip_at(y->ents, y->nents, y->files[file]) : NULL;
}

static int cmp64(const void *a, const void *b) { return strcmp(a, b); }

int chip_list_systems(char (*names)[64], int max)
{
    DIR *d = opendir(CHIP_DIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (e->d_name[0] == '.' || l < 5 || strcasecmp(e->d_name + l - 4, ".zip")) continue;
        snprintf(names[n], 64, "%.*s", (int)(l - 4), e->d_name);
        n++;
    }
    closedir(d);
    qsort(names, (size_t)n, 64, cmp64);
    return n;
}

static void sys_close(chipsys_t *y)
{
    if (!y) return;
    pc_zip_free(y->ents, y->nents);
    free(y->files);
    if (y->zf) fclose(y->zf);
    heap_caps_free(y->cache);
    free(y);
}

static chipsys_t *sys_open(const char *name)
{
    chipsys_t *y = calloc(1, sizeof *y);
    if (!y) return NULL;
    snprintf(y->name, sizeof y->name, "%s", name);
    y->cache_file = -1;
    char path[128];
    snprintf(path, sizeof path, CHIP_DIR "/%s.zip", name);
    if (!(y->zf = fopen(path, "rb")) || pc_reader_file(&y->zr, y->zf) || pc_zip_open(&y->zr, &y->ents, &y->nents)) { sys_close(y); return NULL; }
    y->files = malloc((size_t)(y->nents ? y->nents : 1) * sizeof *y->files);
    for (int i = 0; y->files && i < y->nents; i++)
        if (pc_chip_supported_ext(y->ents[i].name)) y->files[y->nfiles++] = i;   /* zip-index-ok: i < nents */
    printf("APP chip system=%s entries=%d playable=%d\n", name, y->nents, y->nfiles);
    return y;
}

int chip_open_system(const char *name)
{
    if (s_b && !strcmp(s_b->name, name)) return s_b->nfiles;
    if (s_b != s_p) sys_close(s_b);
    s_b = s_p && !strcmp(s_p->name, name) ? s_p : sys_open(name);   /* the playing system is shared, not reopened */
    return s_b ? s_b->nfiles : -ENOENT;
}

int chip_file_count(void) { return s_b ? s_b->nfiles : 0; }
bool chip_now(char *sys, size_t cap, int *file, int *sub)
{
    if (!s_p || s_pos.file < 0) return false;
    snprintf(sys, cap, "%s", s_p->name);
    *file = s_pos.file;
    *sub = s_pos.sub;
    return true;
}

static void label(const chipsys_t *y, int file, char *out, size_t cap)
{
    const pc_zip_entry_t *ze = fentry(y, file);
    if (!ze) { if (cap) out[0] = 0; return; }
    const char *n = ze->name, *slash = strrchr(n, '/');
    if (slash) n = slash + 1;
    const char *dot = strrchr(n, '.');
    snprintf(out, cap, "%.*s", (int)(dot ? dot - n : (long)strlen(n)), n);
}
void chip_file_label(int file, char *out, size_t cap) { if (s_b && file >= 0 && file < s_b->nfiles) label(s_b, file, out, cap); else if (cap) out[0] = 0; }

/* The decoder an entry's extension picks: NULL = Game_Music_Emu (the default for every chip format), else one of our own backends. */
static const pc_backend_t *entry_backend(const char *name)
{
    const char *d = strrchr(name, '.');
    size_t nl = strlen(name);
    if (nl > 7 && !strcasecmp(name + nl - 7, ".p8.png")) return &pc_backend_p8;
    if (d && !strcasecmp(d, ".p8")) return &pc_backend_p8;
    if (d && !strcasecmp(d, ".ym")) return &pc_backend_ym;
    if (d && !strcasecmp(d, ".sid")) return &pc_backend_sid;
    if (d && pc_opl_ext(d)) return &pc_backend_chain;   /* tier + probe order (pc_chipseq.c) */
    if (d && (!strcasecmp(d, ".ahx") || !strcasecmp(d, ".hvl"))) return &pc_backend_hvl;
    return NULL;
}

/* sub-tune count of a non-GME file: open it once and ask */
static int probe_backend(const pc_backend_t *be, const uint8_t *data, uint32_t len, const char *name)
{
    pc_reader_t r;
    void *h;
    pc_reader_mem(&r, data, len);
    pc_reader_set_ext(&r, name);
    if (be->open(&h, &r, 0)) return -1;
    int n = be->track_count ? be->track_count(h) : 1;
    be->close(h);
    return n < 1 ? 1 : n;
}

/* entry -> y->cache (gunzipped for .vgz); 0 or <0 */
static int load(chipsys_t *y, int file)
{
    if (file == y->cache_file) return 0;
    int64_t t0 = esp_timer_get_time();
    heap_caps_free(y->cache);
    y->cache = NULL;
    y->cache_file = -1;
    const pc_zip_entry_t *e = fentry(y, file);
    if (!e) { printf("APP skip reason=range file=%d nfiles=%d\n", file, y->nfiles); return -ERANGE; }
    uint8_t *data;
    uint32_t len;
    int rc = pc_zip_extract(&y->zr, e, &data, &len);
    if (rc) { printf("APP skip reason=zip rc=%d entry=%s\n", rc, e->name); return rc; }
    size_t l = strlen(e->name);
    if (l > 4 && !strcasecmp(e->name + l - 4, ".vgz")) {
        uint8_t *raw;
        uint32_t rl;
        rc = pc_gunzip(data, len, &raw, &rl);
        free(data);
        if (rc) { printf("APP skip reason=gunzip rc=%d entry=%s\n", rc, e->name); return rc; }
        data = raw;
        len = rl;
    }
    char sys[32], chips[80];
    y->game[0] = 0;
    s_why[0] = 0;
    const pc_backend_t *be = entry_backend(e->name);
    /* a VGM whose only unsupported chip is the OPL2 / OPL3 goes to AdPlug's VGM player (its register log through our OPL2 core) */
    if (!be && pc_vgm_unsupported_chips(data, len, chips, sizeof chips) > 0 && (!strcmp(chips, "YM3812") || !strcmp(chips, "YMF262") || !strcmp(chips, "YMF262, YM3812") || !strcmp(chips, "YM3812, YMF262")))
        be = &pc_backend_opl;
    if (be) {
        int n = probe_backend(be, data, len, e->name);
        if (n < 1) { free(data); printf("APP skip reason=probe entry=%s\n", e->name); return -EBADMSG; }
        y->cache_tracks = n;
        sys[0] = chips[0] = 0;
    } else if (pc_vgm_unsupported_chips(data, len, chips, sizeof chips) > 0) {   /* GME renders silence for these: say so instead */
        free(data);
        snprintf(s_why, sizeof s_why, T(S_REFUSE_VGM_CHIPS) /* 此檔需要尚未模擬的晶片：%s */, chips);
        printf("APP skip reason=chips chips=%s entry=%s\n", chips, e->name);
        return -ENOTSUP;
    }
    if (!be && backend_gme_probe(data, len, &y->cache_tracks, sys, sizeof sys, y->game, sizeof y->game)) {
        free(data);
        printf("APP skip reason=gme entry=%s\n", e->name);
        return -EBADMSG;
    }
    y->cache = data;
    y->cache_be = be;
    y->cache_len = len;
    y->cache_file = file;
    printf("APP chip load ms=%d bytes=%lu entry=%s\n", (int)((esp_timer_get_time() - t0) / 1000), (unsigned long)len, e->name);
    return 0;
}

int chip_track_names(int file, char (*names)[64], int max)
{
    if (!s_b) return -ENOENT;
    int rc = load(s_b, file);
    if (rc) return rc;
    if (s_b->cache_be) {                       /* our own backends have no sub-tune names: "1", "2", ... */
        int n = s_b->cache_tracks < max ? s_b->cache_tracks : max;
        for (int i = 0; i < n; i++) snprintf(names[i], 64, "%d", i + 1);
        return n;
    }
    return backend_gme_track_names(s_b->cache, s_b->cache_len, names, max);
}

pc_seq_t chip_mode(void) { return strcmp(cfg_str("chip_mode", "random"), "loop") ? PC_SEQ_RANDOM : PC_SEQ_LOOP; }
void chip_set_mode(pc_seq_t m) { cfg_set_str("chip_mode", m == PC_SEQ_LOOP ? "loop" : "random"); }

static void free_copy(void *p) { heap_caps_free(p); }

static bool start(int file, int sub)
{
    chipsys_t *y = s_p;
    if (!y || load(y, file)) return false;
    const pc_zip_entry_t *fe = fentry(y, file);
    if (!fe) return false;                       /* load() accepted it; kept for the analyzer and for any future reorder */
    if (sub < 0 || sub >= y->cache_tracks) sub = 0;
    uint8_t *copy = heap_caps_malloc(y->cache_len, MALLOC_CAP_SPIRAM);   /* the audio task owns this copy */
    if (!copy) return false;
    memcpy(copy, y->cache, y->cache_len);
    pc_reader_t r;
    pc_reader_mem(&r, copy, y->cache_len);
    pc_reader_set_ext(&r, fe->name);
    s_pos = (pc_chippos_t){ file, sub };
    s_play_tracks = y->cache_tracks;
    snprintf(s_game, sizeof s_game, "%s", y->game);
    char id[200];
    snprintf(id, sizeof id, "chip:%s.zip|%s|%d", y->name, fe->name, sub);
    stats_svc_add_play(id);
    player_ctl_take(&s_ops, forget, on_event);
    s_gen = audio_svc_play(y->cache_be ? y->cache_be : &backend_gme, &r, 0, sub, free_copy, copy);
    if (!s_gen) {                              /* not queued (the copy was freed): stopped, not playing */
        printf("APP play not queued\n");
        s_pos.file = -1;
        player_ctl_release();
        player_ctl_changed();
        return true;
    }
    printf("APP chip file=%d sub=%d/%d gen=%lu entry=%s\n", file, sub, y->cache_tracks, (unsigned long)s_gen, fe->name);
    player_ctl_changed();
    return true;
}

static void redo_play(int a, int b) { chip_play(a, b); }
void chip_play(int file, int sub)      /* a file of the browsed system: it becomes the playing one */
{
    if (!s_b) return;
    if (spk_excl_gate(redo_play, file, sub)) return;   /* speaker mode is an exclusive source: asked first (player_ctl.h) */
    if (s_p != s_b) {
        chipsys_t *old = s_p;
        s_p = s_b;
        sys_close(old);                /* the audio task plays its own copy of the data */
    }
    s_fails.fails = 0;
    s_why[0] = 0;
    if (!start(file, sub)) {
        if (s_why[0]) ui_push_message(s_why);     /* the file the user chose was refused: tell why before the next one starts */
        chip_next();
    }
}

/* after a failure: try further files */
static void skip_from(pc_chippos_t p)
{
    for (int tries = 0; s_p && tries < 8; tries++) {
        p = pc_chip_next((pc_chippos_t){ p.file, 0 }, 1, s_p->nfiles, chip_mode(), esp_random());   /* sub_count 1: next file */
        if (start(p.file, 0)) return;
        if (pc_failstreak_record(&s_fails, false)) break;
    }
    printf("APP chip stop reason=failures\n");
    ui_push_message(T(S_CANNOT_PLAY) /* 無法播放 */);
    chip_stop();
}

void chip_next(void)
{
    if (!s_p || !s_p->nfiles) return;
    pc_chippos_t p = s_pos.file >= 0 ? s_pos : (pc_chippos_t){ 0, -1 };
    int subs = s_pos.file >= 0 ? s_play_tracks : 1;
    pc_chippos_t n = pc_chip_next(p, subs, s_p->nfiles, chip_mode(), esp_random());
    if (!start(n.file, n.sub)) skip_from(n);
}

void chip_stop(void)
{
    if (s_pos.file < 0) return;
    audio_svc_stop(false);
    s_pos.file = -1;
    stats_svc_flush();
    player_ctl_release();
}

static void on_event(const aud_event_t *e)
{
    if (e->gen != s_gen || s_pos.file < 0) return;
    if (e->type == AUD_FADED) { s_pos.file = -1; stats_svc_flush(); player_ctl_release(); return; }
    bool ok = e->type == AUD_ENDED;
    if (pc_failstreak_record(&s_fails, ok)) {
        printf("APP chip stop reason=failures\n");
        ui_push_message(T(S_CANNOT_PLAY) /* 無法播放 */);
        chip_stop();
        return;
    }
    if (ok) chip_next();
    else skip_from(s_pos);
}

static void title(char *out, size_t cap)
{
    if (!s_p || s_pos.file < 0) { if (cap) out[0] = 0; return; }
    char f[96];
    label(s_p, s_pos.file, f, sizeof f);
    snprintf(out, cap, "%s  %d/%d  -  %s", s_game[0] ? s_game : f, s_pos.sub + 1, s_play_tracks, s_p->name);
}
static bool fav_id(char *out, size_t cap)
{
    if (!s_p || s_pos.file < 0) return false;
    const pc_zip_entry_t *pe = fentry(s_p, s_pos.file);
    snprintf(out, cap, "chip:%s.zip|%s|%d", s_p->name, pe ? pe->name : "", s_pos.sub);
    return true;
}
static bool playing(void) { return s_pos.file >= 0; }
static void open_games(void)                      /* the playing system's games (spec 5.8 "list") */
{
    static char sys[64];
    if (!s_p) return;
    snprintf(sys, sizeof sys, "%s", s_p->name);
    ui_open_once(&scr_chip_games, sys);           /* a list page is on the stack once */
}
static const char *games_label(void) { return T(S_GAMES) /* 遊戲清單 */; }
static const char *mode_label(void) { return chip_mode() == PC_SEQ_LOOP ? T(S_IN_ORDER) /* 循序 */ : T(S_RANDOM) /* 隨機 */; }
static uint8_t mode_kind(void) { return chip_mode() == PC_SEQ_LOOP ? NOW_MODE_ORDER : NOW_MODE_SHUFFLE; }
static void mode_step(int d) { if (d & 1) chip_set_mode(chip_mode() == PC_SEQ_LOOP ? PC_SEQ_RANDOM : PC_SEQ_LOOP); }   /* two orders: an odd step switches */
static const now_ops_t s_ops = { .src = NOW_SRC_CHIP, .title = title, .fav_id = fav_id, .next = chip_next, .stop = chip_stop, .playing = playing,
                                 .mode_label = mode_label, .mode_kind = mode_kind, .mode_step = mode_step, .modes = 2, .open_list = open_games, .list_label = games_label };
