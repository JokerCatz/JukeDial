// SPDX-License-Identifier: LGPL-2.1-or-later
#include "noise_engine.h"
#include "pc/pc_paths.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "audio_svc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "pc/pc_rate.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pc/pc_backend.h"
#include "pc/pc_breath.h"
#include "pc/pc_loop.h"
#include "pc/pc_layerset.h"
#include "pc/pc_mix.h"
#include "player_ctl.h"
#include "stats_svc.h"
#include "ui/font.h"
#include "ui/ui.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PULLDATA_API
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#include "third_party/stb_vorbis.c"
#include "dr_flac.h"   /* implementation in player_core pc_backend_flac.c */
#pragma GCC diagnostic pop

#define OUT_RATE 44100
#define VCHUNK 16384
#define VARENA (256 * 1024)

/* ---------- scenes ---------- */
static pc_zip_entry_t *s_ents;
static int s_nents;
static pc_scene_t *s_scenes;
static int s_nscenes, s_ndefault, s_cap;
static pc_sound_t *s_sounds;
static int s_nsounds;
static pc_sound_cats_t s_cats;

int noise_scene_count(void) { return s_nscenes; }
const char *noise_scene_name(int i) { return i >= 0 && i < s_nscenes ? pc_pick_name(s_scenes[i].name, s_scenes[i].en) : ""; }
const pc_scene_t *noise_scene(int i) { return i >= 0 && i < s_nscenes ? &s_scenes[i] : NULL; }

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = n > 0 && n < (1 << 20) ? malloc((size_t)n + 1) : NULL;
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n) b[n] = 0;
    else { free(b); b = NULL; }
    fclose(f);
    return b;
}

int noise_load(void)
{
    if (s_ents) return s_nscenes;
    FILE *f = fopen(NOISE_MUK, "rb");
    pc_reader_t r;
    if (!f || pc_reader_file(&r, f)) { if (f) fclose(f); printf("APP noise muk=missing\n"); return 0; }
    int rc = pc_zip_open(&r, &s_ents, &s_nents);
    uint8_t *json = NULL;
    uint32_t len;
    const pc_zip_entry_t *e = rc ? NULL : pc_zip_find(s_ents, s_nents, "default_scenes.json");
    pc_scene_t *def = NULL, *cus = NULL;
    int nd = 0, nc = 0;
    if (e && !pc_zip_extract(&r, e, &json, &len)) pc_scenes_parse((const char *)json, s_ents, s_nents, false, &def, &nd);
    free(json);
    json = NULL;
    e = rc ? NULL : pc_zip_find(s_ents, s_nents, "seed.json");
    if (e && !pc_zip_extract(&r, e, &json, &len)) pc_sounds_parse((const char *)json, s_ents, s_nents, &s_sounds, &s_nsounds, &s_cats);
    free(json);
    fclose(f);
    char *cj = slurp(NOISE_CUSTOM);
    if (cj) { pc_scenes_parse(cj, s_ents, s_nents, true, &cus, &nc); free(cj); }
    s_cap = nd + nc + 1 + 16;                  /* + the blank scene, + room for scenes saved this session */
    s_scenes = calloc((size_t)s_cap, sizeof *s_scenes);
    if (s_scenes) {
        if (nc) memcpy(s_scenes, cus, (size_t)nc * sizeof *cus);   /* custom first, then the blank scene */
        pc_scene_t *b = &s_scenes[nc];
        snprintf(b->id, sizeof b->id, "blank");
        snprintf(b->name, sizeof b->name, "%s", T(S_BLANK_SCENE) /* 空白情境 */);
        if (nd) memcpy(s_scenes + nc + 1, def, (size_t)nd * sizeof *def);
        s_nscenes = nd + nc + 1;
        s_ndefault = nd + 1;                   /* everything after the custom ones */
    }
    free(def);
    free(cus);
    printf("APP noise entries=%d scenes=%d custom=%d sounds=%d cats=%d\n", s_nents, nd, nc, s_nsounds, s_cats.n);
    return s_nscenes;
}

/* ---------- one decoder of one layer ---------- */
typedef enum { L_VORBIS, L_MP3, L_FLAC } ltype_t;
typedef struct { pc_reader_t *muk; uint32_t base, size; ltype_t type; } layer_cfg_t;
typedef struct {
    layer_cfg_t cfg;
    pc_reader_t sub;
    uint32_t pos;                          /* vorbis/flac read cursor inside the entry */
    uint32_t audio_start;                  /* vorbis: first byte after the headers (starts an Ogg page) */
    /* vorbis */
    stb_vorbis *v; stb_vorbis_alloc al; uint8_t *buf; int fill, off; float **out; int out_n, out_pos, ch;
    /* mp3 */
    void *mp3;
    /* flac */
    drflac *fl; float *fbuf;
} ldec_t;

static int sub_read(void *u, uint32_t off, void *b, uint32_t n)
{
    ldec_t *d = u;
    if (off >= d->cfg.size) return 0;
    if (n > d->cfg.size - off) n = d->cfg.size - off;
    return d->cfg.muk->read(d->cfg.muk->u, d->cfg.base + off, b, n);
}

static int v_read(ldec_t *d)
{
    if (d->off > 0) { memmove(d->buf, d->buf + d->off, (size_t)(d->fill - d->off)); d->fill -= d->off; d->off = 0; }
    int got = sub_read(d, d->pos, d->buf + d->fill, (uint32_t)(VCHUNK - d->fill));
    if (got > 0) { d->fill += got; d->pos += (uint32_t)got; }
    return got > 0 ? got : 0;
}

static size_t fl_read(void *u, void *b, size_t n)
{
    ldec_t *d = u;
    int got = sub_read(d, d->pos, b, (uint32_t)n);
    if (got > 0) d->pos += (uint32_t)got;
    return got > 0 ? (size_t)got : 0;
}
static drflac_bool32 fl_seek(void *u, int off, drflac_seek_origin o)
{
    ldec_t *d = u;
    int64_t p = o == DRFLAC_SEEK_SET ? off : o == DRFLAC_SEEK_CUR ? (int64_t)d->pos + off : (int64_t)d->cfg.size + off;
    if (p < 0 || p > (int64_t)d->cfg.size) return DRFLAC_FALSE;
    d->pos = (uint32_t)p;
    return DRFLAC_TRUE;
}
static drflac_bool32 fl_tell(void *u, drflac_int64 *c) { *c = ((ldec_t *)u)->pos; return DRFLAC_TRUE; }

static void ldec_close(void *h)
{
    ldec_t *d = h;
    if (!d) return;
    if (d->v) stb_vorbis_close(d->v);
    heap_caps_free(d->al.alloc_buffer);
    heap_caps_free(d->buf);
    if (d->mp3) pc_backend_mp3.close(d->mp3);
    if (d->fl) drflac_close(d->fl);
    heap_caps_free(d->fbuf);
    heap_caps_free(d);
}

static int ldec_open(void *ctx, void **h, uint32_t *rate)
{
    ldec_t *d = heap_caps_calloc(1, sizeof *d, MALLOC_CAP_SPIRAM);
    if (!d) return -1;
    d->cfg = *(layer_cfg_t *)ctx;
    d->sub = (pc_reader_t){ .read = sub_read, .size = d->cfg.size, .u = d };
    if (d->cfg.type == L_VORBIS) {
        d->buf = heap_caps_malloc(VCHUNK, MALLOC_CAP_SPIRAM);
        d->al.alloc_buffer_length_in_bytes = VARENA;
        d->al.alloc_buffer = heap_caps_malloc(VARENA, MALLOC_CAP_SPIRAM);
        if (!d->buf || !d->al.alloc_buffer) goto fail;
        for (;;) {
            if (!v_read(d) && d->fill == 0) goto fail;
            int used = 0, err = 0;
            d->v = stb_vorbis_open_pushdata(d->buf, d->fill, &used, &err, &d->al);
            if (d->v) { d->off = used; d->audio_start = d->pos - (uint32_t)(d->fill - used); break; }
            if (err != VORBIS_need_more_data || d->fill >= VCHUNK) goto fail;
        }
        stb_vorbis_info vi = stb_vorbis_get_info(d->v);
        d->ch = vi.channels;
        *rate = vi.sample_rate;
    } else if (d->cfg.type == L_MP3) {
        if (pc_backend_mp3.open(&d->mp3, &d->sub, 0)) { d->mp3 = NULL; goto fail; }
        *rate = pc_backend_mp3.rate(d->mp3);
    } else {
        d->fl = drflac_open(fl_read, fl_seek, fl_tell, d, NULL);
        if (!d->fl || d->fl->channels < 1 || d->fl->channels > 2) goto fail;
        d->fbuf = heap_caps_malloc(512 * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
        if (!d->fbuf) goto fail;
        *rate = d->fl->sampleRate;
    }
    *h = d;
    return 0;
fail:
    ldec_close(d);
    return -1;
}

/* back to the first frame without re-parsing the headers (an open costs ~90 ms of setup on the audio task) */
static int ldec_rewind(void *h)
{
    ldec_t *d = h;
    if (d->cfg.type == L_FLAC) return drflac_seek_to_pcm_frame(d->fl, 0) ? 0 : -1;
    if (d->cfg.type != L_VORBIS) return -1;
    stb_vorbis_flush_pushdata(d->v);       /* the next decode resyncs on the Ogg page at audio_start */
    d->pos = d->audio_start;
    d->fill = d->off = 0;
    d->out_n = d->out_pos = 0;
    return 0;
}

static int ldec_read(void *h, float *st, int frames)
{
    ldec_t *d = h;
    if (d->cfg.type == L_MP3) return pc_backend_mp3.render(d->mp3, st, frames);
    if (d->cfg.type == L_FLAC) {
        if (frames > 512) frames = 512;
        int n = (int)drflac_read_pcm_frames_f32(d->fl, (drflac_uint64)frames, d->fbuf);
        for (int i = 0; i < n; i++) {
            float l = d->fbuf[i * d->fl->channels], r = d->fl->channels > 1 ? d->fbuf[i * 2 + 1] : l;
            st[2 * i] = l;
            st[2 * i + 1] = r;
        }
        return n;
    }
    for (int guard = 0; d->out_pos >= d->out_n && guard < 64; guard++) {
        int ch = 0, samples = 0;
        int used = stb_vorbis_decode_frame_pushdata(d->v, d->buf + d->off, d->fill - d->off, &ch, &d->out, &samples);
        d->off += used;
        if (samples > 0) { d->out_n = samples; d->out_pos = 0; d->ch = ch; break; }
        if (used == 0 && !v_read(d) && d->off >= d->fill) return 0;   /* end of entry */
    }
    int n = d->out_n - d->out_pos;
    if (n > frames) n = frames;
    for (int i = 0; i < n; i++) {
        st[2 * i] = d->out[0][d->out_pos + i];
        st[2 * i + 1] = d->out[d->ch > 1][d->out_pos + i];
    }
    d->out_pos += n;
    return n;
}

/* ---------- engine ----------
 * At most NOISE_MAX_ON layers play (pc_layerset, spec 5.6). A layer is opened by the worker task (an Ogg open
 * parses headers for ~90 ms; on the audio task that would be an underrun) and handed to the audio task through
 * an atomic inbox; the audio task fades it in, and fades a layer that was turned off out before closing it.
 * The engine is shared by LVGL, the worker and the audio task, so it is reference counted. */
#define FADE_STEP (1.0f / (OUT_RATE * 0.3f))   /* 300 ms layer fade */

typedef struct {
    layer_cfg_t cfg;
    pc_layer_src_t src;
    float base_vol;
    pc_loop_t *inbox;                  /* worker -> audio (atomic) */
    int want, active, failed, queued;  /* atomic: LVGL wants it on / audio holds a loop / open failed / open queued */
    pc_loop_t *loop;                   /* audio task only from here */
    pc_rs_t rs;
    float in[2 * 512];
    int in_len, in_pos;
    float gain;
    int seams;
} layer_t;

typedef struct {
    FILE *f;
    pc_reader_t file, muk;             /* muk = file behind a lock: the worker and the audio task both read */
    SemaphoreHandle_t lock;
    int refs, dead;
    int n;
    layer_t L[PC_SCENE_MAX_LAYERS];
    float tmp[2 * PC_AUDIO_MAX_BLOCK];  /* one block as the audio task hands it (audio_svc.h: up to PC_AUDIO_MAX_BLOCK frames) */
} engine_t;

/* the audio task hands a mixer up to PC_AUDIO_MAX_BLOCK frames (audio_svc.h): tmp holds one such block */
_Static_assert(sizeof(((engine_t *)0)->tmp) >= 2 * PC_AUDIO_MAX_BLOCK * sizeof(float), "engine_t.tmp must hold PC_AUDIO_MAX_BLOCK stereo frames");

typedef struct { engine_t *e; int layer; } job_t;

static volatile float s_user_vol[PC_SCENE_MAX_LAYERS];
EXT_RAM_BSS_ATTR static volatile float s_breath_cut;   /* ideas G7: the breathing coach's cut, 0..0.5 of the layers' volumes (0 = none): never above the user's level; PSRAM (no internal RAM for it) */
EXT_RAM_BSS_ATTR static volatile uint32_t s_breath_req;   /* the UI bumps it on every set_breath call; the audio task renews its own 64-bit deadline when it sees a new count (pc_br_hold), so no clock wraps and an old cut cannot come back */
EXT_RAM_BSS_ATTR static pc_br_hold_t s_br_hold;          /* audio task only */
EXT_RAM_BSS_ATTR static float s_br_cut;                  /* audio task: the cut the last block ended on (the next block ramps from it) */
#define BREATH_HOLD_US 1000000                       /* the follow lasts 1 s after the last call: a UI stall (card write, page build) does not pump the gain */
static int s_cur = -1;
static uint32_t s_gen;
static engine_t *s_eng;                /* LVGL's reference while a scene plays */
static pc_layerset_t s_ls;             /* LVGL: which layers of s_ls_scene are on */
static int s_ls_scene = -1;
static QueueHandle_t s_jobs;

#define LD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define ST(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

static int muk_read(void *u, uint32_t off, void *b, uint32_t n)
{
    engine_t *e = u;
    xSemaphoreTake(e->lock, portMAX_DELAY);
    int r = e->file.read(e->file.u, off, b, n);
    xSemaphoreGive(e->lock);
    return r;
}

static void eng_destroy(engine_t *e)
{
    for (int i = 0; i < e->n; i++) {
        pc_loop_destroy(e->L[i].loop);
        pc_loop_destroy(__atomic_exchange_n(&e->L[i].inbox, NULL, __ATOMIC_ACQ_REL));
    }
    if (e->f) fclose(e->f);
    if (e->lock) vSemaphoreDelete(e->lock);
    heap_caps_free(e);
}
static void eng_ref(engine_t *e) { __atomic_add_fetch(&e->refs, 1, __ATOMIC_ACQ_REL); }
static void eng_unref(engine_t *e) { if (e && __atomic_sub_fetch(&e->refs, 1, __ATOMIC_ACQ_REL) == 0) eng_destroy(e); }

static void engine_free(void *arg)     /* audio task (or the sender when the play command could not be queued) */
{
    engine_t *e = arg;
    ST(e->dead, 1);
    eng_unref(e);
}

static void worker(void *arg)
{
    (void)arg;
    job_t j;
    for (;;) {
        if (xQueueReceive(s_jobs, &j, portMAX_DELAY) != pdTRUE) continue;
        layer_t *L = &j.e->L[j.layer];
        ST(L->queued, 0);
        if (!LD(j.e->dead) && LD(L->want) && !LD(L->active) && !LD(L->inbox)) {
            int64_t t0 = esp_timer_get_time();
            pc_loop_t *lp = pc_loop_create(&L->src, 50);
            printf("APP noise layer=%d open=%s ms=%d\n", j.layer, lp ? "ok" : "failed", (int)((esp_timer_get_time() - t0) / 1000));
            if (!lp) ST(L->failed, 1);
            else if (LD(j.e->dead) || !LD(L->want)) pc_loop_destroy(lp);
            else __atomic_store_n(&L->inbox, lp, __ATOMIC_RELEASE);
        }
        eng_unref(j.e);
    }
}

static void queue_open(engine_t *e, int i)   /* LVGL */
{
    layer_t *L = &e->L[i];
    if (LD(L->queued)) return;
    ST(L->queued, 1);
    eng_ref(e);
    job_t j = { e, i };
    if (xQueueSend(s_jobs, &j, 0) != pdTRUE) { ST(L->queued, 0); eng_unref(e); }
}

static int engine_render(float *out, int frames, void *u)
{
    engine_t *e = u;
    memset(out, 0, (size_t)frames * 2 * sizeof(float));
    const float br0 = 1.0f - s_br_cut, want = 1.0f - (pc_br_hold_on(&s_br_hold, s_breath_req, esp_timer_get_time(), BREATH_HOLD_US) ? s_breath_cut : 0.0f), room = (float)frames / 44100.0f;   /* the gain 0.5..1 slews at most 1.0 a second (a breath moves 0.125), so a release is never a jump */
    const float br1 = want > br0 + room ? br0 + room : want < br0 - room ? br0 - room : want;
    s_br_cut = 1.0f - br1;
    int n = LD(e->n);                                /* a layer added from LVGL is published by its count */
    for (int i = 0; i < n; i++) {
        layer_t *L = &e->L[i];
        pc_loop_t *in = __atomic_exchange_n(&L->inbox, NULL, __ATOMIC_ACQ_REL);
        if (in) {
            if (L->loop) pc_loop_destroy(in);
            else {
                L->loop = in;
                pc_rs_init(&L->rs, pc_loop_rate(in), OUT_RATE);
                L->in_len = L->in_pos = 0;
                L->gain = 0.0f;
                L->seams = 0;
                ST(L->active, 1);
            }
        }
        if (!L->loop) continue;
        float target = LD(L->want) ? 1.0f : 0.0f;
        int got = 0;
        if (pc_loop_rate(L->loop) == OUT_RATE) {
            got = pc_loop_render(L->loop, e->tmp, frames);
        } else {
            while (got < frames) {
                if (L->in_pos >= L->in_len) {
                    L->in_len = pc_loop_render(L->loop, L->in, 512);
                    L->in_pos = 0;
                    if (L->in_len <= 0) break;
                }
                int used = 0;
                got += pc_rs_process(&L->rs, L->in + 2 * L->in_pos, L->in_len - L->in_pos, e->tmp + 2 * got, frames - got, &used);
                L->in_pos += used;
            }
        }
        if (got <= 0) {
            printf("APP noise layer=%d failed\n", i);
            pc_loop_destroy(L->loop);
            L->loop = NULL;
            ST(L->failed, 1);
            ST(L->active, 0);
            continue;
        }
        float vol = L->base_vol * s_user_vol[i], g = L->gain;
        for (int k = 0; k < got; k++) {
            g += g < target ? FADE_STEP : g > target ? -FADE_STEP : 0.0f;
            if (g > 1.0f) g = 1.0f;
            if (g < 0.0f) g = 0.0f;
            const float v = vol * g * (br0 + (br1 - br0) * (float)k / (float)frames);
            out[2 * k] += e->tmp[2 * k] * v;
            out[2 * k + 1] += e->tmp[2 * k + 1] * v;
        }
        L->gain = g;
        if (pc_loop_seams(L->loop) != L->seams) { L->seams = pc_loop_seams(L->loop); printf("APP loop layer=%d seam=%d\n", i, L->seams); }
        if (target == 0.0f && g <= 0.0f) {          /* faded out: close */
            pc_loop_destroy(L->loop);
            L->loop = NULL;
            ST(L->active, 0);
        }
    }
    return frames;                                   /* silence while layers load, or when all are off */
}

static void drop_engine(void) { engine_t *e = s_eng; s_eng = NULL; eng_unref(e); }
static void forget(void) { s_cur = -1; drop_engine(); }
static void title(char *out, size_t cap) { snprintf(out, cap, "%s", s_cur >= 0 ? pc_pick_name(s_scenes[s_cur].name, s_scenes[s_cur].en) : ""); }
static bool fav_id(char *out, size_t cap) { if (s_cur < 0) return false; snprintf(out, cap, "scene:%s", s_scenes[s_cur].id); return true; }
static bool playing(void) { return s_cur >= 0; }
static void stop_now(void) { noise_engine_stop(false); }
static void open_layers(void) { extern const ui_screen_t scr_scene; if (s_cur >= 0) ui_open_once(&scr_scene, (void *)(intptr_t)s_cur); }
static const char *layers_label(void) { return T(S_SOUNDS) /* 聲音 */; }
static const now_ops_t s_ops = { .src = NOW_SRC_NOISE, .title = title, .fav_id = fav_id, .stop = stop_now, .playing = playing,
                                 .open_list = open_layers, .list_label = layers_label, .no_art = true };

static void on_event(const aud_event_t *ev)
{
    if (ev->gen != s_gen || s_cur < 0) return;
    printf("APP noise stopped reason=%s\n", ev->type == AUD_ERROR ? "error" : ev->type == AUD_FADED ? "faded" : "end");
    s_cur = -1;
    drop_engine();
    player_ctl_release();
}

static void ls_for(int si)              /* the scene's on/off set; kept while the same scene is shown */
{
    if (s_ls_scene == si) return;
    pc_layerset_init(&s_ls, s_scenes[si].nlayers, NOISE_MAX_ON);
    s_ls_scene = si;
    for (int i = 0; i < PC_SCENE_MAX_LAYERS; i++) s_user_vol[i] = 1.0f;
}

static void layer_setup(engine_t *e, int i, const pc_layer_t *y)   /* LVGL; before the layer is published */
{
    layer_t *L = &e->L[i];
    uint32_t off = 0;
    const pc_zip_entry_t *z = y->entry;
    const char *ext = strrchr(z->name, '.');
    L->cfg = (layer_cfg_t){ .muk = &e->muk, .size = z->usize,
                            .type = ext && !strcasecmp(ext, ".mp3") ? L_MP3 : ext && !strcasecmp(ext, ".flac") ? L_FLAC : L_VORBIS };
    if (pc_zip_data_offset(&e->muk, z, &off)) { L->failed = 1; pc_layerset_set(&s_ls, i, false); }
    L->cfg.base = off;
    L->src = (pc_layer_src_t){ .open = ldec_open, .read = ldec_read, .close = ldec_close, .ctx = &L->cfg,
                               .rewind = L->cfg.type == L_MP3 ? NULL : ldec_rewind };
    L->base_vol = y->volume;
    L->want = s_ls.on[i];
}

/* the edited scene (spec 5.6 B): the playing scene is a working copy; switching to another scene drops the edits */
static int s_work_idx = -1;
static pc_scene_t s_work_orig;
static void restore_work(void)
{
    if (s_work_idx < 0) return;
    s_scenes[s_work_idx] = s_work_orig;
    if (s_ls_scene == s_work_idx) s_ls_scene = -1;
    s_work_idx = -1;
}
static void mark_modified(int si)
{
    if (s_work_idx == si) return;
    restore_work();
    s_work_orig = s_scenes[si];
    s_work_idx = si;
}
bool noise_scene_modified(int si) { return s_work_idx == si; }

static void redo_start(int a, int b) { (void)b; noise_engine_start(a); }
int noise_engine_start(int si)
{
    if (noise_scene(si) && spk_excl_gate(redo_start, si, 0)) return -EBUSY;   /* speaker mode is an exclusive source: asked first (player_ctl.h); nothing started */
    s_breath_cut = 0.0f;
    if (si != s_work_idx) restore_work();
    const pc_scene_t *sc = noise_scene(si);
    if (!sc) return -EINVAL;
    if (!s_jobs) {
        s_jobs = xQueueCreate(2 * PC_SCENE_MAX_LAYERS, sizeof(job_t));
        /* 32 KB: opening an mp3 layer decodes a frame, and minimp3 keeps its ~16 KB scratch on the stack */
        xTaskCreatePinnedToCoreWithCaps(worker, "noise_open", 32 * 1024, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);
    }
    engine_t *e = heap_caps_calloc(1, sizeof *e, MALLOC_CAP_SPIRAM);
    if (!e) return -ENOMEM;
    e->refs = 1;
    e->lock = xSemaphoreCreateMutex();
    e->f = fopen(NOISE_MUK, "rb");
    if (e->f) setvbuf(e->f, NULL, _IONBF, 0);       /* before any I/O */
    if (!e->lock || !e->f || pc_reader_file(&e->file, e->f)) { eng_destroy(e); return -ENOENT; }
    e->muk = (pc_reader_t){ .read = muk_read, .size = e->file.size, .u = e };
    ls_for(si);
    for (int i = 0; i < sc->nlayers; i++) layer_setup(e, i, &sc->layer[i]);
    e->n = sc->nlayers;
    s_cur = si;
    char id[96];
    snprintf(id, sizeof id, "scene:%s", sc->id);
    stats_svc_add_play(id);
    player_ctl_take(&s_ops, forget, on_event);
    drop_engine();                                   /* the previous scene's engine, if take() did not already */
    eng_ref(e);                                      /* LVGL's reference; the first one goes to the audio task */
    s_eng = e;
    for (int i = 0; i < e->n; i++) if (e->L[i].want) queue_open(e, i);
    s_gen = audio_svc_play_mixer(engine_render, e, OUT_RATE, engine_free, e);
    if (!s_gen) {                              /* not queued (engine_free already ran): stopped, not playing */
        printf("APP play not queued\n");
        s_cur = -1;
        drop_engine();
        player_ctl_release();
        player_ctl_changed();
        return -EIO;
    }
    printf("APP noise scene=%s layers=%d on=%d gen=%lu\n", sc->id, e->n, pc_layerset_count(&s_ls), (unsigned long)s_gen);
    player_ctl_changed();
    return 0;
}

int noise_engine_toggle_layer(int si, int layer)
{
    if (!noise_scene(si)) return -1;
    ls_for(si);
    if (s_eng && s_cur == si && layer >= 0 && layer < s_eng->n && LD(s_eng->L[layer].failed)) pc_layerset_set(&s_ls, layer, false);
    int r = pc_layerset_toggle(&s_ls, layer);
    if (r < 0 || r == PC_LS_FULL || !s_eng || s_cur != si) return r;
    layer_t *L = &s_eng->L[layer];
    ST(L->want, r == PC_LS_ON);
    if (r == PC_LS_ON) { ST(L->failed, 0); queue_open(s_eng, layer); }
    printf("APP noise layer=%d %s on=%d\n", layer, r == PC_LS_ON ? "on" : "off", pc_layerset_count(&s_ls));
    return r;
}

int noise_engine_add_sound(int si, int sound)
{
    if (!noise_scene(si) || sound < 0 || sound >= s_nsounds) return -EINVAL;
    const pc_sound_t *d = &s_sounds[sound];
    pc_scene_t *sc = &s_scenes[si];
    for (int i = 0; i < sc->nlayers; i++) if (!strcmp(sc->layer[i].sha, d->sha)) return -EEXIST;
    ls_for(si);
    if (sc->nlayers >= PC_SCENE_MAX_LAYERS) return -ENOSPC;
    mark_modified(si);
    int k = pc_layerset_add(&s_ls);
    if (k < 0 || k != sc->nlayers) return -ENOSPC;
    pc_layer_t *y = &sc->layer[k];
    memset(y, 0, sizeof *y);
    y->entry = d->entry;
    y->volume = 0.7f;
    snprintf(y->sha, sizeof y->sha, "%s", d->sha);
    sc->nlayers++;
    s_user_vol[k] = 1.0f;
    if (s_eng && s_cur == si && k == s_eng->n) {
        layer_setup(s_eng, k, y);
        __atomic_store_n(&s_eng->n, k + 1, __ATOMIC_RELEASE);   /* the audio task sees it complete */
        if (s_ls.on[k]) queue_open(s_eng, k);
    }
    printf("APP noise add sound=%s layer=%d on=%d\n", d->name, k, s_ls.on[k]);
    return s_ls.on[k] ? 0 : 1;
}

int noise_cat_count(void) { return s_cats.n + 1; }            /* + "uncategorised" */
const char *noise_cat_name(int c) { return c < s_cats.n ? pc_pick_name(s_cats.name[c], s_cats.en[c]) : T(S_OTHER) /* 未分類 */; }
int noise_cat_sounds(int c, int *out, int max)
{
    int k = 0;
    if (c < 0 || !out) return 0;
    for (int i = 0; i < s_nsounds && k < max; i++) {
        uint32_t m = s_sounds[i].cats;
        if (c < s_cats.n ? (m >> c) & 1 : m == 0) out[k++] = i;
    }
    return k;
}
const char *noise_sound_name(int i) { return i >= 0 && i < s_nsounds ? pc_pick_name(s_sounds[i].name, s_sounds[i].en) : ""; }

noise_layer_state_t noise_engine_layer_state(int si, int layer)
{
    ls_for(si);
    if (layer < 0 || layer >= s_ls.n) return NOISE_LAYER_OFF;
    if (s_eng && s_cur == si) {
        const layer_t *L = &s_eng->L[layer];
        if (LD(L->failed)) { pc_layerset_set(&s_ls, layer, false); return NOISE_LAYER_FAILED; }   /* frees its place */
        if (!s_ls.on[layer]) return NOISE_LAYER_OFF;
        return LD(L->active) ? NOISE_LAYER_ON : NOISE_LAYER_LOADING;
    }
    return s_ls.on[layer] ? NOISE_LAYER_ON : NOISE_LAYER_OFF;
}

const char *noise_layer_name(int si, int layer)
{
    const pc_scene_t *sc = noise_scene(si);
    if (!sc || layer < 0 || layer >= sc->nlayers) return "";
    const pc_sound_t *d = pc_sound_find(s_sounds, s_nsounds, sc->layer[layer].sha);
    return d ? pc_pick_name(d->name, d->en) : "";
}

void noise_engine_set_layer_volume(int layer, float v)
{
    if (layer < 0 || layer >= PC_SCENE_MAX_LAYERS) return;
    s_user_vol[layer] = v < 0 ? 0 : v > 1 ? 1 : v;
}
void noise_engine_set_breath(int permille)
{
    s_breath_cut = (float)(1000 - (permille < 500 ? 500 : permille > 1000 ? 1000 : permille)) / 1000.0f;
    s_breath_req++;
}
int noise_engine_breath(void) { return 1000 - (int)(s_breath_cut * 1000.0f + 0.5f); }
float noise_engine_layer_volume(int layer) { return layer >= 0 && layer < PC_SCENE_MAX_LAYERS ? s_user_vol[layer] : 0; }

void noise_engine_stop(bool fade)
{
    s_breath_cut = 0.0f;
    if (s_cur < 0) return;
    audio_svc_stop(fade);
    s_cur = -1;
    drop_engine();
    stats_svc_flush();
    player_ctl_release();
}

int noise_current(void) { return s_cur; }

int noise_save_custom(int si)
{
    const pc_scene_t *sc = noise_scene(si);
    if (!sc || s_nscenes >= s_cap || !sc->nlayers) return -1;
    ls_for(si);
    int ncust = s_nscenes - s_ndefault;
    pc_scene_t n = *sc;
    snprintf(n.id, sizeof n.id, "custom:%d", ncust + 1);
    snprintf(n.name, sizeof n.name, "%s %d", T(S_CUSTOM) /* 自訂 */, ncust + 1);
    n.custom = true;
    n.missing = 0;
    n.nlayers = 0;
    for (int pass = 0; pass < 2; pass++)            /* layers that are on first: they are the ones on next time */
        for (int i = 0; i < sc->nlayers; i++)
            if (s_ls.on[i] == (pass == 0)) {
                n.layer[n.nlayers] = sc->layer[i];
                n.layer[n.nlayers++].volume = sc->layer[i].volume * s_user_vol[i];
            }
    bool was_playing = s_cur == si;
    restore_work();                                  /* the edits now live in the custom scene */
    memmove(&s_scenes[ncust + 1], &s_scenes[ncust], (size_t)s_ndefault * sizeof *s_scenes);
    s_scenes[ncust] = n;
    s_nscenes++;
    if (s_cur >= ncust) s_cur++;                     /* everything after the custom ones moved down by one */
    if (s_ls_scene >= ncust) s_ls_scene++;
    pc_int_mkdir(PC_CFG_DIR);
    int rc = pc_scenes_save_custom(s_scenes, ncust + 1, NOISE_CUSTOM);
    printf("APP noise saved custom=%d layers=%d rc=%d\n", ncust + 1, n.nlayers, rc);
    if (was_playing) noise_engine_start(ncust);      /* play what was saved (layers reopen, fade in) */
    return ncust;
}

int noise_delete_custom(int si)
{
    const pc_scene_t *sc = noise_scene(si);
    int ncust = s_nscenes - s_ndefault;
    if (!sc || !sc->custom || si >= ncust) return -EINVAL;
    if (s_cur == si) noise_engine_stop(false);
    restore_work();
    memmove(&s_scenes[si], &s_scenes[si + 1], (size_t)(s_nscenes - si - 1) * sizeof *s_scenes);
    s_nscenes--;
    if (s_cur > si) s_cur--;
    s_ls_scene = -1;
    pc_int_mkdir(PC_CFG_DIR);
    int rc = pc_scenes_save_custom(s_scenes, ncust - 1, NOISE_CUSTOM);
    printf("APP noise deleted custom=%d left=%d rc=%d\n", si, ncust - 1, rc);
    return rc;
}
