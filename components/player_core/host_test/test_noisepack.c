// SPDX-License-Identifier: LGPL-2.1-or-later
/* The generated noise scene pack (tools/gen/make_noise_pack.py -> .local/library_free.muk): the engine's own readers take it (zip, scenes, sounds)
 * and every scene's layers decode (dr_flac, as noise_engine.c) and loop through pc_loop past a seam: finite, not silent, not clipped.
 * NOISEPACK=<file> overrides the path; a missing file is a SKIP (the pack is generated, never in git: `make noise-pack` first). */
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#include "pc/pc_io.h"
#include "pc/pc_loop.h"
#include "pc/pc_scenes.h"
#include "pc/pc_zip.h"
#include "pc/pc_i18n.h"
#include "pc_test.h"

static pc_lang_t s_lang = PC_LANG_TW;
pc_lang_t pc_i18n_cur(void) { return s_lang; }   /* the app's override, here the test's */

typedef struct { const uint8_t *d; uint32_t n; drflac *f; } dec_t;
static int d_open(void *ctx, void **dec, uint32_t *rate)
{
    dec_t *c = ctx; dec_t *d = malloc(sizeof *d); *d = *c;
    d->f = drflac_open_memory(c->d, c->n, NULL);
    if (!d->f) { free(d); return -1; }
    *dec = d; *rate = d->f->sampleRate; return 0;
}
static int d_read(void *h, float *st, int frames)
{
    dec_t *d = h; static float b[512];
    if (frames > 512) frames = 512;
    int n = (int)drflac_read_pcm_frames_f32(d->f, (drflac_uint64)frames, b);
    for (int i = 0; i < n; i++) { st[2 * i] = b[i * d->f->channels]; st[2 * i + 1] = st[2 * i]; }
    return n;
}
static void d_close(void *h) { dec_t *d = h; drflac_close(d->f); free(d); }
static int d_rewind(void *h) { dec_t *d = h; return drflac_seek_to_pcm_frame(d->f, 0) ? 0 : -1; }

static const char *path(void) { const char *p = getenv("NOISEPACK"); return p ? p : NOISEPACK_DEFAULT; }

int main(void)
{
    FILE *fp = fopen(path(), "rb");
    if (!fp) { printf("SKIP test_noisepack: %s absent (make noise-pack)\n", path()); return 0; }
    pc_reader_t r; CHECK_EQ_INT(pc_reader_file(&r, fp), 0);
    pc_zip_entry_t *ents; int ne;
    CHECK_EQ_INT(pc_zip_open(&r, &ents, &ne), 0);
    uint8_t *js; uint32_t jl;
    const pc_zip_entry_t *e = pc_zip_find(ents, ne, "default_scenes.json"); CHECK(e);
    CHECK_EQ_INT(pc_zip_extract(&r, e, &js, &jl), 0);
    char *t = malloc(jl + 1); memcpy(t, js, jl); t[jl] = 0; free(js);
    pc_scene_t *sc; int ns = 0;
    CHECK_EQ_INT(pc_scenes_parse(t, ents, ne, false, &sc, &ns), 0); free(t);
    e = pc_zip_find(ents, ne, "seed.json"); CHECK(e);
    CHECK_EQ_INT(pc_zip_extract(&r, e, &js, &jl), 0);
    t = malloc(jl + 1); memcpy(t, js, jl); t[jl] = 0; free(js);
    pc_sound_t *so; int nso = 0; pc_sound_cats_t cats;
    CHECK_EQ_INT(pc_sounds_parse(t, ents, ne, &so, &nso, &cats), 0); free(t);
    CHECK_EQ_INT(ns, 12); CHECK(nso >= 6 && nso <= 16); CHECK_EQ_INT(cats.n, 2);
    /* names per language: scene (en_name / data.tsv 簡中 / zh_tw) and category (category_en / data row / zh_tw) */
    {
        const pc_scene_t *fan = NULL;
        for (int s = 0; s < ns; s++) if (!strcmp(sc[s].id, "n-fan")) fan = &sc[s];
        CHECK(fan);
        s_lang = PC_LANG_TW; CHECK_STR(pc_pick_name(fan->name, fan->en), "電風扇"); CHECK_STR(pc_pick_name(cats.name[0], cats.en[0]), cats.name[0]);
        s_lang = PC_LANG_CN; CHECK_STR(pc_pick_name(fan->name, fan->en), "电风扇");
        s_lang = PC_LANG_EN; CHECK_STR(pc_pick_name(fan->name, fan->en), "Electric fan");
        for (int c = 0; c < cats.n; c++) {
            s_lang = PC_LANG_TW; CHECK(!strcmp(pc_pick_name(cats.name[c], cats.en[c]), "噪音") || !strcmp(cats.name[c], "自然"));
            s_lang = PC_LANG_CN; CHECK(!strcmp(cats.name[c], "噪音") ? !strcmp(pc_pick_name(cats.name[c], cats.en[c]), "噪声") : 1);
            s_lang = PC_LANG_EN; CHECK(!strcmp(pc_pick_name(cats.name[c], cats.en[c]), !strcmp(cats.name[c], "噪音") ? "Noise" : "Nature"));
        }
        for (int s = 0; s < ns; s++) { s_lang = PC_LANG_EN; CHECK(sc[s].en[0] && !strcmp(pc_pick_name(sc[s].name, sc[s].en), sc[s].en)); }
        for (int i = 0; i < nso; i++) { s_lang = PC_LANG_EN; CHECK(so[i].en[0]); }
        s_lang = PC_LANG_TW;
    }
    for (int s = 0; s < ns; s++) {
        CHECK(sc[s].nlayers >= 1 && sc[s].nlayers <= 3); CHECK_EQ_INT(sc[s].missing, 0); CHECK(sc[s].name[0]);
        for (int l = 0; l < sc[s].nlayers; l++) {
            const pc_zip_entry_t *z = sc[s].layer[l].entry; CHECK(z);
            CHECK(sc[s].layer[l].volume > 0 && sc[s].layer[l].volume <= 1.0f);
            uint8_t *fl; uint32_t fn; CHECK_EQ_INT(pc_zip_extract(&r, z, &fl, &fn), 0);
            dec_t ctx = { fl, fn, NULL };
            pc_layer_src_t src = { .open = d_open, .read = d_read, .close = d_close, .rewind = d_rewind, .ctx = &ctx };
            pc_loop_t *lp = pc_loop_create(&src, 300); CHECK(lp);
            uint32_t rate = pc_loop_rate(lp);
            CHECK(rate >= 8000 && rate <= 48000);
            int frames = (int)rate * 22;            /* > two loops of the longest sound: crosses at least two seams */
            float *buf = malloc(sizeof(float) * 2 * 1024); double e2 = 0, pk = 0; long cnt = 0;
            for (int done = 0; done < frames; done += 1024) {
                CHECK_EQ_INT(pc_loop_render(lp, buf, 1024), 1024);
                for (int i = 0; i < 2048; i++) { CHECK(isfinite(buf[i])); e2 += buf[i] * buf[i]; if (fabs(buf[i]) > pk) pk = fabs(buf[i]); cnt++; }
            }
            double rms = sqrt(e2 / cnt);
            CHECK(rms > 0.01 && rms < 0.4); CHECK(pk < 1.0);
            CHECK(pc_loop_seams(lp) >= 2);
            free(buf); pc_loop_destroy(lp); free(fl);
        }
        printf("ok scene %s (%d layers)\n", sc[s].id, sc[s].nlayers);
    }
    free(sc); free(so); pc_zip_free(ents, ne); fclose(fp);
    printf("ok test_noisepack\n");
    return 0;
}
