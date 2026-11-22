// SPDX-License-Identifier: LGPL-2.1-or-later
#include "mp3_ctl.h"
#include "cfg.h"
#include "crashnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "audio_svc.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "backend_sdec.h"
#include "esp_random.h"
#include "library_svc.h"
#include "pc/pc_chipseq.h"
#include "pc/pc_failstreak.h"
#include "pc/pc_fmtreason.h"
#include "pc/pc_hist.h"
#include "pc/pc_rg.h"
#include "pc/pc_shuffle.h"
#include "pc/pc_game_guess.h"
#include "player_ctl.h"
#include "stats_svc.h"
#include "screens.h"
#include "font.h"

static int s_cur = -1, s_folder = -1;
static uint32_t s_gen;
static pc_recent_t s_recent;
static pc_hist_t s_hist;                      /* previous track: the last 20 started songs and a cursor (88 bytes) */
typedef enum { START_NEW, START_WALK, START_RESTART, START_CLIP, START_PUTBACK } start_how_t;   /* NEW: a history entry; WALK: the cursor already moved; RESTART: the same song again;
                                                                                                 * CLIP, PUTBACK (猜歌): restarts that count no play (no history, no recent entry, no statistics); a CLIP is a short look into a song:
                                                                                                 * it never queues a next song and its end or failure is not the player's to answer (mp3_ctl_clip_state) */
static pc_failstreak_t s_fails;
EXT_RAM_BSS_ATTR static int s_cfirst, s_cn;      /* the pool: tracks [s_cfirst, s_cfirst + s_cn) = the folder with its sub-folders, or everything (PSRAM: no internal RAM for it) */
EXT_RAM_BSS_ATTR static pc_wscope_t s_ws;       /* the weights of that pool, made on the first pick after the pool or the favourites changed (pc_wscope_*) */
static uint32_t s_qgen;                       /* gapless: the queued next track (audio_svc_queue_next), 0 none */
static int s_qidx = -1;
static bool s_qhist;                          /* the queued track is the history's next one: the cursor moves when it starts */
#define NEAR_MS 8000                          /* pre-open the next track this long before the end */

EXT_RAM_BSS_ATTR static int s_xdisp;          /* crossfade: the queued track (index + 1) already shown (title, cover) while the old one still plays; 0 none; valid only while that track is still the queued one (PSRAM: no internal bytes) */
static int disp_idx(void) { return s_xdisp > 0 && s_qgen && s_qidx == s_xdisp - 1 ? s_xdisp - 1 : s_cur; }

int mp3_ctl_current(void) { return s_cur; }
int mp3_ctl_folder(void) { return s_folder; }

static void close_file(void *f) { fclose(f); }

static void on_event(const aud_event_t *e);
static const now_ops_t s_ops;
EXT_RAM_BSS_ATTR static uint8_t s_clip;       /* mp3_ctl_clip_state: what became of the last clip (0 none); PSRAM: no internal RAM for one flag (W1 review) */
static void forget(void) { crashnet_idle(); s_cur = -1; if (s_clip == MP3_CLIP_PLAYING) s_clip = MP3_CLIP_NONE; }

static void build_candidates(void)
{
    pc_library_scope(&g_lib, s_folder, &s_cfirst, &s_cn);   /* a contiguous range: the tree is stored depth first */
    if (s_cn < 0) s_cn = 0;
}

/* the weighted pick over the pool; the weights are made once (not per pick) and again only when the pool, the library or a favourite changed */
static int shuffle_pick(void)
{
    if (!pc_wscope_fresh(&s_ws, &g_lib, s_cfirst, s_cn, g_stats)) pc_wscope_build(&s_ws, &g_lib, s_cfirst, s_cn, g_stats);
    return pc_wscope_pick(&s_ws, &s_recent, esp_random());
}

/* false when the file could not even be opened */
/* a track of g_lib -> its index in next, -1 gone (u: the library being swapped in and its path order; no static: no internal RAM). Fix 1
 * (review M2): binary search on a path index (PSRAM, made once per swap); without the memory for it, the linear lookup */
typedef struct { const pc_library_t *lib; const int *ix; } remap_t;
static int remap(int idx, void *u)
{
    const remap_t *m = u;
    if (idx < 0 || idx >= g_lib.n) return -1;
    const char *p = pc_str(&g_lib, g_lib.tracks[idx].path);
    return m->ix ? pc_library_find_sorted(m->lib, m->ix, p) : pc_library_find(m->lib, p);
}

void mp3_ctl_library_changed(const pc_library_t *next)
{
    pc_wscope_free(&s_ws);                        /* the pool is built again from the new library at the next pick */
    s_cn = 0;
    int *ix = next->n ? heap_caps_malloc(sizeof(int) * (size_t)next->n, MALLOC_CAP_SPIRAM) : NULL;
    if (ix) pc_library_path_order(next, ix);
    remap_t m = { next, ix };
    void *u = &m;
    int cur = s_cur, nc = remap(s_cur, u), nq = s_qidx >= 0 ? remap(s_qidx, u) : -1;
    int nf = s_folder >= 0 && s_folder < g_lib.nfolders ? pc_library_find_folder(next, pc_str(&g_lib, g_lib.folders[s_folder].path)) : -1;
    pc_hist_remap(&s_hist, remap, u);
    pc_recent_remap(&s_recent, remap, u);
    heap_caps_free(ix);
    s_folder = nf;                                /* a folder that is gone: the whole library */
    s_qidx = nq;                                  /* a queued track that is gone: its join stops (commit_next) */
    if (cur < 0) return;
    if (nc < 0) {
        printf("APP mp3 stop reason=gone path=%s\n", pc_str(&g_lib, g_lib.tracks[cur].path));
        mp3_ctl_stop();
        ui_push_message(T(S_THE_PLAYING_SONG_IS_NO_LONGER_ON_THE) /* the playing song is no longer on the card */);
        return;
    }
    s_cur = nc;
    printf("APP mp3 library kept cur=%d>%d queued=%d folder=%d hist=%d recent=%d\n", cur, nc, nq, nf, s_hist.n, s_recent.n);
}

void mp3_ctl_print(void)
{
    printf("APP mp3 cur=%d folder=%d qidx=%d hist=%d back=%d recent=%d path=%s\n", s_cur, s_folder, s_qidx, s_hist.n, s_hist.back, s_recent.n,
           s_cur >= 0 ? pc_str(&g_lib, g_lib.tracks[s_cur].path) : "-");
}

/* the extension of a path: ".p8.png" counts as one (a PICO-8 cart), else from the last dot */
static const char *file_ext(const char *path)
{
    size_t n = strlen(path);
    if (n > 7 && !strcasecmp(path + n - 7, ".p8.png")) return path + n - 7;
    return strrchr(path, '.');
}

static const pc_backend_t *backend_for(const char *path)
{
    const char *ext = file_ext(path);
    return !ext ? &pc_backend_mp3 : (!strcasecmp(ext, ".p8") || !strcasecmp(ext, ".p8.png")) ? &pc_backend_p8 : !strcasecmp(ext, ".flac") ? &pc_backend_flac :
           (!strcasecmp(ext, ".ogg") || !strcasecmp(ext, ".oga") || !strcasecmp(ext, ".opus")) ? &backend_ogg :
           !strcasecmp(ext, ".m4a") ? &backend_m4a : !strcasecmp(ext, ".wav") ? &pc_backend_wav :
           (pc_xmp_ext(ext) || pc_opl_ext(ext)) ? &pc_backend_chain :   /* tracker modules and DOS OPL music: the one table in pc_chipseq.c (tier, probe order) */
           (!strcasecmp(ext, ".aif") || !strcasecmp(ext, ".aiff") || !strcasecmp(ext, ".aifc")) ? &pc_backend_aiff :
           (!strcasecmp(ext, ".mpc") || !strcasecmp(ext, ".mp+")) ? &pc_backend_mpc :
           !strcasecmp(ext, ".tta") ? &pc_backend_tta :
           (!strcasecmp(ext, ".ahx") || !strcasecmp(ext, ".hvl")) ? &pc_backend_hvl :
           !strcasecmp(ext, ".sid") ? &pc_backend_sid : !strcasecmp(ext, ".ym") ? &pc_backend_ym : !strcasecmp(ext, ".adx") ? &pc_backend_adx : !strcasecmp(ext, ".vag") ? &pc_backend_vag : !strcasecmp(ext, ".brstm") ? &pc_backend_brstm :
           &pc_backend_mp3;
}

/* ReplayGain (cfg rg: 0 off, 1 track, 2 album; rg_untag: dB for files without a tag) */
static float rg_gain(int idx, float *db) { return pc_rg_linear(&g_lib.tracks[idx].rg, cfg_int("rg", PC_RG_TRACK), (float)cfg_int("rg_untag", 0), db); }
static aud_opts_t opts_for(int idx, float *db)
{
    aud_opts_t o = { .gain = rg_gain(idx, db), .near_ms = cfg_int("gapless", 1) ? NEAR_MS : 0, .speed = true };
    return o;
}
static void log_track(int idx, uint32_t gen, float db, const char *how)
{
    const pc_rg_t *g = &g_lib.tracks[idx].rg;
    printf("APP track idx=%d gen=%lu title=%s rg=%+.2fdB (mode %d, tag track %d album %d cdB, peak %u)%s\n", idx, (unsigned long)gen,
           pc_str(&g_lib, g_lib.tracks[idx].title), db, cfg_int("rg", PC_RG_TRACK), g->track_cdb == PC_RG_NONE ? 0 : g->track_cdb,
           g->album_cdb == PC_RG_NONE ? 0 : g->album_cdb, g->track_peak, how);
}

/* crash safety net: a track that was playing at a crash is not picked by itself (a deliberate play clears the mark) */
static bool crash_marked(int idx) { return idx >= 0 && idx < g_lib.n && crashnet_marked(pc_str(&g_lib, g_lib.tracks[idx].path)); }

static bool open_track(int idx, FILE **fp, pc_reader_t *r)
{
    const char *path = pc_str(&g_lib, g_lib.tracks[idx].path);
    FILE *f = fopen(path, "rb");
    if (f) setvbuf(f, NULL, _IONBF, 0);       /* before any I/O; the backend has its own 16 KB buffer (PSRAM) */
    if (!f || pc_reader_file(r, f)) {
        if (f) fclose(f);
        printf("APP skip reason=open path=%s\n", path);
        return false;
    }
    pc_reader_set_ext(r, path);
    *fp = f;
    return true;
}

static uint32_t s_start_at;                   /* the next start() opens here (resume after a reboot), once */
/* where a clip at pct % starts, in what the song's backend takes (pc_guess_clip_token): a byte for MP3 / WAV, a PCM frame index for FLAC, the start for the others */
static uint32_t clip_at(const pc_backend_t *be, const pc_reader_t *r, int pct, int *kind)
{
    uint8_t head[PC_CLIP_FLAC_HEAD] = { 0 };
    *kind = be == &pc_backend_mp3 || be == &pc_backend_wav || be == &pc_backend_aiff ? PC_CLIP_BYTES : be == &pc_backend_flac ? PC_CLIP_FLAC : PC_CLIP_NONE;
    if (*kind == PC_CLIP_FLAC && r->read(r->u, 0, head, sizeof head) != (int)sizeof head) *kind = PC_CLIP_NONE;
    return pc_guess_clip_token(*kind, r->size, head, pct);
}

static bool start_at(int idx, start_how_t how, int pct)
{
    if (idx < 0 || idx >= g_lib.n) return false;
    const char *path = pc_str(&g_lib, g_lib.tracks[idx].path);
    FILE *f;
    pc_reader_t r;
    s_qgen = 0;                               /* a play replaces whatever was queued */
    s_qidx = -1;
    s_qhist = false;
    if (!open_track(idx, &f, &r)) return false;
    s_cur = idx;
    const bool quiet = how >= START_CLIP;
    if (how != START_RESTART && !quiet) pc_recent_push(&s_recent, idx);
    if (how == START_NEW) pc_hist_add(&s_hist, idx);
    char id[300];
    snprintf(id, sizeof id, "mp3:%s", path);
    if (!quiet) stats_svc_add_play(id);
    player_ctl_take(&s_ops, forget, on_event);
    uint32_t at = s_start_at < r.size ? s_start_at : 0;
    s_start_at = 0;
    s_clip = MP3_CLIP_NONE;
    if (how == START_CLIP) {
        int kind;
        at = clip_at(backend_for(path), &r, pct, &kind);
        s_clip = MP3_CLIP_PLAYING;
        printf("APP mp3 clip idx=%d pct=%d kind=%d at=%lu\n", idx, pct, kind, (unsigned long)at);
    }
    if (at) printf("APP mp3 resume at byte %lu of %lu\n", (unsigned long)at, (unsigned long)r.size);
    float db;
    aud_opts_t o = opts_for(idx, &db);
    s_gen = audio_svc_play_ex(backend_for(path), &r, at, 0, &o, close_file, f);
    if (!s_gen) {                              /* the audio task did not take it (it closed the file): stopped, not playing */
        printf("APP play not queued\n");
        if (s_clip == MP3_CLIP_PLAYING) s_clip = MP3_CLIP_FAILED;
        s_cur = -1;
        player_ctl_release();
        player_ctl_changed();
        return true;                           /* not the file's fault: do not skip on to the next one */
    }
    crashnet_playing(path);                    /* the track being decoded now: RTC memory, read at the next boot if this run ends in a crash */
    log_track(idx, s_gen, db, "");
    player_ctl_changed();
    return true;
}
static bool start(int idx, start_how_t how) { return start_at(idx, how, 0); }

/* the track the play order picks after the current one (repeat-one: the same one), -1 none */
static int pick_next(void)
{
    if (!s_cn) build_candidates();
    if (!s_cn) return -1;
    pc_order_t o = mp3_ctl_order();
    s_qhist = false;
    int h = o == PC_ORDER_REPEAT_ONE ? -1 : pc_hist_peek_next(&s_hist);   /* behind the newest song: the history goes on (the cursor does not move yet) */
    if (h >= 0) { s_qhist = true; return crash_marked(h) ? -1 : h; }   /* a marked one is not queued: the end takes the usual path, which skips it */
    int sh = o == PC_ORDER_SHUFFLE || s_cur < 0 ? shuffle_pick() : -1;
    int n = pc_order_next_range(o, s_cfirst, s_cn, s_cur, sh);
    return crash_marked(n) ? -1 : n;
}

/* AUD_NEAR: open the next track now, so it follows without a gap */
static void queue_next(void)
{
    int idx = pick_next();
    if (idx < 0 || idx >= g_lib.n) return;
    FILE *f;
    pc_reader_t r;
    if (!open_track(idx, &f, &r)) return;     /* the end then takes the usual path (which skips it) */
    float db;
    aud_opts_t o = opts_for(idx, &db);
    uint32_t g = audio_svc_queue_next(s_gen, backend_for(pc_str(&g_lib, g_lib.tracks[idx].path)), &r, &o, close_file, f);
    if (!g) return;
    s_qgen = g;
    s_qidx = idx;
    crashnet_queued(pc_str(&g_lib, g_lib.tracks[idx].path));   /* its open runs now, while the current one plays */
    printf("APP gapless queued idx=%d gen=%lu after=%lu\n", idx, (unsigned long)g, (unsigned long)s_gen);
}

/* AUD_NEXT: the queued track is playing */
static void commit_next(void)
{
    if (s_qidx < 0) {                             /* hires T8e: the queued file left the library in a rescan */
        printf("APP mp3 stop reason=gone (queued)\n");
        s_qgen = 0;
        mp3_ctl_stop();
        return;
    }
    bool same = s_qidx == s_cur;
    s_xdisp = 0;
    s_cur = s_qidx;
    s_gen = s_qgen;
    s_qgen = 0;
    s_qidx = -1;
    crashnet_playing(pc_str(&g_lib, g_lib.tracks[s_cur].path));
    pc_recent_push(&s_recent, s_cur);
    if (s_qhist) pc_hist_next(&s_hist); else if (!(same && mp3_ctl_order() == PC_ORDER_REPEAT_ONE)) pc_hist_add(&s_hist, s_cur);   /* repeat-one: the same song again is no new entry */
    s_qhist = false;
    char id[300];
    snprintf(id, sizeof id, "mp3:%s", pc_str(&g_lib, g_lib.tracks[s_cur].path));
    stats_svc_add_play(id);
    pc_failstreak_record(&s_fails, true);
    float db;
    rg_gain(s_cur, &db);
    log_track(s_cur, s_gen, db, " gapless");
    player_ctl_changed();
}

void mp3_ctl_rg_changed(void)
{
    float db;
    if (s_cur >= 0) audio_svc_set_gain(s_gen, rg_gain(s_cur, &db));
    if (s_qgen) audio_svc_set_gain(s_qgen, rg_gain(s_qidx, &db));
    if (s_cur >= 0) printf("APP rg mode=%d idx=%d now %+.2fdB\n", cfg_int("rg", PC_RG_TRACK), s_cur, (rg_gain(s_cur, &db), db));
}

pc_order_t mp3_ctl_order(void) { int o = cfg_int("mp3_order", 0); return o >= 0 && o < PC_ORDER_COUNT ? (pc_order_t)o : PC_ORDER_SHUFFLE; }
void mp3_ctl_order_step(int d) { cfg_set_int("mp3_order", (((int)mp3_ctl_order() + d) % PC_ORDER_COUNT + PC_ORDER_COUNT) % PC_ORDER_COUNT); }
static uint8_t order_kind(void) { const pc_order_t o = mp3_ctl_order(); return o == PC_ORDER_SHUFFLE ? NOW_MODE_SHUFFLE : o == PC_ORDER_REPEAT_ONE ? NOW_MODE_ONE : NOW_MODE_ORDER; }
const char *mp3_ctl_order_label(void)
{
    static const char *const zh[] = { "隨機", "循序", "單曲循環" }, *const en[] = { "Shuffle", "In order", "Repeat one" };
    return T_RAW(zh[mp3_ctl_order()], en[mp3_ctl_order()]);
}

/* walk_history: only the user's "next" and a track that ends by itself go on through the history; a folder / all start and the
 * fallback after a failed start are fresh picks (pc_hist_add drops what is newer than the cursor).
 * user: the "next" button - in repeat-one it moves on in folder order; a track that ends repeats */
static void next_in_order(bool user, bool walk_history)
{
    if (!s_cn) build_candidates();
    int prev = s_cur;
    for (int tries = 0; tries < 8 && s_cn; tries++) {
        pc_order_t o = mp3_ctl_order();
        if (o == PC_ORDER_REPEAT_ONE && (user || tries)) o = PC_ORDER_SEQUENTIAL;   /* a failing track is not retried */
        int idx = o == PC_ORDER_REPEAT_ONE || !walk_history ? -1 : pc_hist_next(&s_hist);   /* behind the newest song: the history goes on */
        bool walk = idx >= 0;
        if (!walk) {
            int sh = o == PC_ORDER_SHUFFLE || prev < 0 ? shuffle_pick() : -1;
            idx = pc_order_next_range(o, s_cfirst, s_cn, prev, sh);
        }
        if (idx < 0) break;
        prev = idx;
        if (crash_marked(idx)) { printf("APP mp3 skip idx=%d reason=crash-mark\n", idx); continue; }   /* crash safety net: never by itself */
        start_how_t how = walk ? START_WALK : o == PC_ORDER_REPEAT_ONE && idx == s_cur ? START_RESTART : START_NEW;   /* a repeat is no new entry */
        if (start(idx, how)) return;
        if (pc_failstreak_record(&s_fails, false)) break;
    }
    printf("APP mp3 stop reason=failures\n");
        ui_push_message(T(S_CANNOT_PLAY) /* cannot play */);
    mp3_ctl_stop();
}

void mp3_ctl_next(void) { next_in_order(true, true); }

/* user: the "previous" button - the history one step back; at its oldest end: the folder's previous song (in order / repeat-one; it becomes the oldest entry),
 * or this song from its start (shuffle) */
void mp3_ctl_prev(void)
{
    if (s_cur < 0) return;
    if (!s_cn) build_candidates();
    int from = s_cur;
    for (int tries = 0; tries < 8; tries++) {
        int idx = pc_hist_prev(&s_hist);
        start_how_t how = START_WALK;
        if (idx < 0) {
            if (mp3_ctl_order() == PC_ORDER_SHUFFLE || !s_cn) { idx = from; how = START_RESTART; }
            else {
                const int i = from - s_cfirst;
                idx = s_cfirst + (i >= 0 && i < s_cn ? (i + s_cn - 1) % s_cn : s_cn - 1);
                how = START_WALK;                          /* the oldest entry now, the cursor is on it */
                pc_hist_add_oldest(&s_hist, idx);
            }
        }
        from = idx;
        if (crash_marked(idx)) { printf("APP mp3 skip idx=%d reason=crash-mark\n", idx); continue; }
        if (start(idx, how)) return;
        if (pc_failstreak_record(&s_fails, false)) break;
    }
    printf("APP mp3 stop reason=failures\n");
    ui_push_message(T(S_CANNOT_PLAY) /* cannot play */);
    mp3_ctl_stop();
}

/* speaker mode is an exclusive source (player_ctl.h spk_excl_gate): the three plays a pick makes ask first; next / prev, a clip and the resume do not */
static void redo_folder(int a, int b) { (void)b; mp3_ctl_play_folder(a); }
static void redo_track(int a, int b) { (void)b; mp3_ctl_play_track(a); }
static void redo_folder_track(int a, int b) { mp3_ctl_play_folder_track(a, b); }

void mp3_ctl_play_folder(int folder)
{
    if (spk_excl_gate(redo_folder, folder, 0)) return;
    s_folder = folder;
    s_fails.fails = 0;
    build_candidates();
    next_in_order(true, false);   /* user: repeat-one is for a song that ends by itself, not for a start */
}

void mp3_ctl_play_folder_track_at(int folder, int idx, uint32_t byte)
{
    if (idx >= 0 && idx < g_lib.n && crash_marked(idx)) {   /* a resume of a track that crashed the player: not resumed, the next one plays */
        printf("APP mp3 resume skipped idx=%d reason=crash-mark\n", idx);
        s_folder = folder;
        s_fails.fails = 0;
        build_candidates();
        s_cur = idx;                                   /* "next" goes on from it */
        next_in_order(true, false);
        return;
    }
    s_start_at = byte;
    mp3_ctl_play_folder_track(folder, idx);
}

void mp3_ctl_play_folder_track(int folder, int idx)
{
    if (spk_excl_gate(redo_folder_track, folder, idx)) return;
    s_folder = folder;
    build_candidates();
    mp3_ctl_play_track(idx);
}

void mp3_ctl_play_track(int idx)
{
    if (idx < 0 || idx >= g_lib.n) return;
    if (spk_excl_gate(redo_track, idx, 0)) return;
    s_fails.fails = 0;
    crashnet_clear(pc_str(&g_lib, g_lib.tracks[idx].path));   /* a deliberate play: the crash mark goes (a crash again marks it again) */
    if (!start(idx, START_NEW)) next_in_order(true, false);
}

/* 猜歌: a clip of song idx from pct % of it (the position in the unit the song's backend takes, see clip_at) and the way the page puts the user's song back
 * (from the byte the player reported, as a resume). Both are the normal start (the same file, backend, gain and player owner) as a restart that counts no play. A clip is not
 * the player's business once it runs: an open that fails (AUD_ERROR), a song that ends within it, never moves on to another song and never queues the next one: the clip
 * stops and mp3_ctl_clip_state says ENDED / FAILED for the page to answer. false: the file did not open or the audio task did not take it. */
bool mp3_ctl_play_clip(int idx, int pct)
{
    if (idx < 0 || idx >= g_lib.n) return false;
    s_fails.fails = 0;
    bool ok = start_at(idx, START_CLIP, pct) && s_cur == idx;
    if (!ok && s_clip != MP3_CLIP_FAILED) s_clip = MP3_CLIP_FAILED;
    return ok;
}

bool mp3_ctl_play_back(int idx, uint32_t byte)
{
    if (idx < 0 || idx >= g_lib.n) return false;
    s_fails.fails = 0;
    s_start_at = byte;
    bool ok = start(idx, START_PUTBACK);
    s_start_at = 0;
    return ok && s_cur == idx;
}

int mp3_ctl_clip_state(void) { return s_clip; }

void mp3_ctl_stop(void)
{
    if (s_clip == MP3_CLIP_PLAYING) s_clip = MP3_CLIP_NONE;
    if (s_cur < 0) return;
    audio_svc_stop(false);
    crashnet_idle();
    s_cur = -1;
    stats_svc_flush();
    player_ctl_release();
}

/* DACFS: what the wired DAC wants the user to know, once (FakePod): PLL fallback, DAC not answering, serial-port errors that took the rate one tier down */
static void show_dac_note(void)
{
    uint32_t arg = 0;
    const uint8_t n = audio_svc_take_dac_note(&arg);
    if (!n) return;
    static EXT_RAM_BSS_ATTR char m[96];   /* the round glass keeps the pointer while its message page is up */
    if (n == 1) snprintf(m, sizeof m, "%s", T(S_DAC_NOTE_PLL) /* PLL not locked: playing on the 24.576 MHz clock */);
    else if (n == 2) snprintf(m, sizeof m, "%s", T(S_DAC_NOTE_DEAD) /* DAC not responding, muted */);
    else if (n == 4) snprintf(m, sizeof m, "%s", T(S_DAC_NOTE_ABSENT) /* no CS43131 DAC found (PCM5102 variant?): wired sound off */);
    else { char kh[16]; snprintf(kh, sizeof kh, "%lu.%02lu", (unsigned long)(arg / 1000), (unsigned long)(arg % 1000 / 10)); snprintf(m, sizeof m, T(S_DAC_NOTE_SERIAL) /* DAC serial-port error (%s kHz), playing one step lower */, kh); }
    printf("APP dac note %u\n", (unsigned)n);
    ui_push_message(m);
}

/* honest degrade: a file the backend refused for a known reason (OPL module, AIFF compression, ...): say why, then skip as for any unplayable file */
static void show_refusal(int idx)
{
    if (idx < 0 || idx >= g_lib.n) return;
    const char *path = pc_str(&g_lib, g_lib.tracks[idx].path), *ext = file_ext(path);
    static EXT_RAM_BSS_ATTR char m[96];   /* the round glass keeps the pointer while its message page is up */
    if (ext && pc_ext_blocked(ext)) {      /* an experimental format with the setting off: said without opening the file (tier 2026-11-18) */
        snprintf(m, sizeof m, "%s", T(S_REFUSE_EXPERIMENTAL_OFF));
        printf("APP skip reason=experimental path=%s\n", path);
        ui_push_message(m);
        return;
    }
    FILE *f = ext ? fopen(path, "rb") : NULL;
    pc_reader_t r;
    if (!f) return;
    char detail[16];
    int opl_cell = 0, xmp_cell = 0;
    if (ext) pc_ext_backends(ext, &opl_cell, &xmp_cell);
    pc_refuse_t why = pc_reader_file(&r, f) ? PC_REFUSE_NONE : pc_refuse_reason(&r, ext, detail, sizeof detail);
    if (why == PC_REFUSE_NONE && !strcasecmp(ext, ".sid") && !pc_reader_file(&r, f)) why = pc_sid_check(&r) == -2 ? PC_REFUSE_SID_BASIC : PC_REFUSE_NONE;
    if (why == PC_REFUSE_NONE && opl_cell && pc_tier_allowed(opl_cell) && !pc_reader_file(&r, f)) {
        pc_reader_set_ext(&r, path);
        int c = pc_opl_check(&r);
        why = c == -2 ? PC_REFUSE_OPL_4OP : c == -3 ? PC_REFUSE_OPL_PACKED : c == -4 ? PC_REFUSE_OPL_DAMAGED : PC_REFUSE_NONE;
    }
    if (why == PC_REFUSE_NONE && !strcasecmp(ext, ".ym") && !pc_reader_file(&r, f)) why = pc_ym_check(&r) == -2 ? PC_REFUSE_YM_VARIANT : PC_REFUSE_NONE;
    if (why == PC_REFUSE_NONE && (!strcasecmp(ext, ".adx") || !strcasecmp(ext, ".brstm")) && !pc_reader_file(&r, f)) {   /* the stream decoder says why */
        int c = pc_stream_check(&r, ext);
        why = c == 1 ? PC_REFUSE_ADX_TYPE : c == 2 ? PC_REFUSE_BRSTM_CODEC : PC_REFUSE_NONE;
    }
    if (why == PC_REFUSE_NONE && (!strcasecmp(ext, ".p8") || !strcasecmp(ext, ".p8.png")) && !pc_reader_file(&r, f)) {   /* the cart backend says why it refused */
        int c = pc_p8_check(&r);
        why = c == -3 ? PC_REFUSE_P8_NO_MUSIC : c == -2 ? PC_REFUSE_P8_PNG : PC_REFUSE_NONE;
    }
    if (why == PC_REFUSE_NONE && xmp_cell && pc_tier_allowed(xmp_cell)) why = PC_REFUSE_XMP_BAD;   /* libxmp refused it and no check above knows more */
    if ((why == PC_REFUSE_NONE || why == PC_REFUSE_XMP_BAD) && ext && pc_ext_has_gated(ext)) why = PC_REFUSE_EXPERIMENTAL_OFF;   /* the other reader of this suffix (e.g. .dtm) is experimental and off: the file may be its kind */
    fclose(f);
    const char *fmt = why == PC_REFUSE_OPL_MODULE ? T(S_REFUSE_OPL_MODULE) : why == PC_REFUSE_AIFF_CODEC ? T(S_REFUSE_AIFF_CODEC) :
                      why == PC_REFUSE_TTA_ENCRYPTED ? T(S_REFUSE_TTA_ENCRYPTED) : why == PC_REFUSE_MPC_OLD ? T(S_REFUSE_MPC_OLD) :
                      why == PC_REFUSE_YM_VARIANT ? T(S_REFUSE_YM_VARIANT) : why == PC_REFUSE_ADX_TYPE ? T(S_REFUSE_ADX_TYPE) : why == PC_REFUSE_BRSTM_CODEC ? T(S_REFUSE_BRSTM_CODEC) :
                      why == PC_REFUSE_P8_NO_MUSIC ? T(S_REFUSE_P8_NO_MUSIC) : why == PC_REFUSE_P8_PNG ? T(S_REFUSE_P8_PNG) :
                      why == PC_REFUSE_SID_BASIC ? T(S_REFUSE_SID_BASIC) : why == PC_REFUSE_OPL_4OP ? T(S_REFUSE_OPL_4OP) :
                      why == PC_REFUSE_OPL_PACKED ? T(S_REFUSE_OPL_PACKED) : why == PC_REFUSE_XMP_BAD ? T(S_REFUSE_XMP_BAD) : why == PC_REFUSE_OPL_DAMAGED ? T(S_REFUSE_OPL_DAMAGED) :
                      why == PC_REFUSE_EXPERIMENTAL_OFF ? T(S_REFUSE_EXPERIMENTAL_OFF) : NULL;
    if (!fmt) return;
    snprintf(m, sizeof m, fmt, detail);
    printf("APP skip reason=format why=%d path=%s\n", (int)why, path);
    ui_push_message(m);
}

static void on_event(const aud_event_t *e)
{
    show_dac_note();
    if (e->type == AUD_XFADE) { if (s_qgen && e->gen == s_qgen && s_qidx >= 0 && s_cur >= 0) { s_xdisp = s_qidx + 1; printf("APP mp3 xfade show idx=%d\n", s_qidx); player_ctl_changed(); } return; }   /* the next track fades in: title and cover switch now (s_cur, s_gen, the history wait for AUD_NEXT) */
    if (e->type == AUD_NEXT) { if (s_qgen && e->gen == s_qgen && s_cur >= 0) commit_next(); return; }
    if (e->gen != s_gen || s_cur < 0) return;          /* stale */
    if (s_clip == MP3_CLIP_PLAYING) {                  /* a clip: no queue, no skip, no next song */
        if (e->type == AUD_NEAR) return;
        bool end = e->type == AUD_ENDED || e->type == AUD_FADED;
        mp3_ctl_stop();                                /* releases the player; it also clears PLAYING */
        s_clip = end ? MP3_CLIP_ENDED : MP3_CLIP_FAILED;
        printf("APP mp3 clip %s\n", end ? "ended" : "failed");
        return;
    }
    if (e->type == AUD_NEAR) { if (!s_qgen) queue_next(); return; }
    s_qgen = 0;                                        /* ended without taking the queued one (it was dropped) */
    s_qidx = -1;
    crashnet_queued(NULL);
    if (e->type == AUD_FADED) { crashnet_idle(); s_cur = -1; stats_svc_flush(); player_ctl_release(); return; }
    bool ok = e->type == AUD_ENDED;
    if (!ok) printf("APP skip reason=decode idx=%d\n", s_cur);
    if (!ok) show_refusal(s_cur);
    if (!ok) {   /* honest degrade: the wired DAC could not take this rate (FakePod): say so, then skip as for any unplayable file */
        const uint32_t low = audio_svc_take_low_rate();
        if (low) {
            static char m[96];   /* the round glass keeps the pointer while its message page is up */
            char kh[16];
            snprintf(kh, sizeof kh, "%lu.%02lu", (unsigned long)(low / 1000), (unsigned long)(low % 1000 / 10));
            snprintf(m, sizeof m, audio_svc_low_is_hard() ? T(S_DAC_RATE_HARD) /* above the wired refusal limit (%s kHz) */ : T(S_DAC_RATE_LOW) /* this sample rate (%s kHz) is not supported by the wired output */, kh);
            printf("APP skip reason=rate_low rate=%lu\n", (unsigned long)low);
            ui_push_message(m);
        }
    }
    if (pc_failstreak_record(&s_fails, ok)) {
        printf("APP mp3 stop reason=failures\n");
        ui_push_message(T(S_CANNOT_PLAY) /* cannot play */);
        mp3_ctl_stop();
        return;
    }
    next_in_order(!ok, true);                                /* ended: the play order (repeat-one repeats); failed: move on */
}

/* now-playing screen hooks */
static void title(char *out, size_t cap)
{
    const int di = disp_idx();
    if (s_cur < 0) { out[0] = 0; return; }
    snprintf(out, cap, "%s  -  %s", pc_str(&g_lib, g_lib.tracks[di].title), pc_str(&g_lib, g_lib.tracks[di].artist));
}
static bool cover(char *path, size_t cap, uint32_t *off, uint32_t *len)
{
    const int di = disp_idx();
    if (s_cur < 0) return false;
    snprintf(path, cap, "%s", pc_str(&g_lib, g_lib.tracks[di].path));
    *off = g_lib.tracks[di].apic_off;
    *len = g_lib.tracks[di].apic_len;
    return true;                                   /* len 0: no embedded picture; thumb_svc looks for an image file next to the track (hires T8c) */
}
static bool fav_id(char *out, size_t cap)
{
    const int di = disp_idx();
    if (s_cur < 0) return false;
    snprintf(out, cap, "mp3:%s", pc_str(&g_lib, g_lib.tracks[di].path));
    return true;
}
static bool playing(void) { return s_cur >= 0; }
static void open_list(void) { ui_open_once(&scr_tracks, (void *)(intptr_t)(s_cur >= 0 ? pc_track_folder(&g_lib, s_cur) : s_folder)); }   /* the level the playing song lives in */   /* a list page is on the stack once */
static const char *list_label(void) { return T(S_SONGS) /* song list */; }
static void open_info(void) { if (s_cur >= 0) ui_open_once(&scr_meta, (void *)(intptr_t)s_cur); }
static const now_ops_t s_ops = { .src = NOW_SRC_MP3, .title = title, .cover = cover, .fav_id = fav_id, .next = mp3_ctl_next, .prev = mp3_ctl_prev, .stop = mp3_ctl_stop,
                                 .playing = playing, .open_list = open_list, .open_info = open_info,
                                 .list_label = list_label, .mode_label = mp3_ctl_order_label, .mode_kind = order_kind, .mode_step = mp3_ctl_order_step, .modes = PC_ORDER_COUNT, .speed = true };
