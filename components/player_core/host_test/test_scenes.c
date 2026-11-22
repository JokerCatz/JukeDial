// SPDX-License-Identifier: LGPL-2.1-or-later
#include <unistd.h>
#include "pc/pc_io.h"
#include "pc/pc_scenes.h"
#include "pc_test.h"

static char sha[8][65];
static pc_zip_entry_t ents[8];
static int nents;

static void mkents(void)
{
    /* sha i = 64 hex chars of digit i; entries "<sha>.ogg" except #3 = .flac and #4 = .m4a; #7 absent */
    const char *ext[] = { "ogg", "mp3", "ogg", "flac", "m4a", "ogg", "ogg" };
    static char names[8][80];
    for (int i = 0; i < 8; i++) { memset(sha[i], '0' + i, 64); sha[i][64] = 0; }
    for (int i = 0; i < 7; i++) {
        snprintf(names[i], sizeof names[i], "%s.%s", sha[i], ext[i]);
        ents[i].name = names[i];
    }
    nents = 7;   /* already sorted: '0'.. < '6'.. */
}

static char *scene(const char *id, const char *zh, const int *layers, int n, int unresolved)
{
    static char buf[4096];
    int k = snprintf(buf, sizeof buf, "{\"id\":\"%s\",\"en_name\":\"EN %s\",\"zh_tw_name\":\"%s\",\"resolved_sounds\":[", id, id, zh);
    for (int i = 0; i < n; i++) k += snprintf(buf + k, sizeof buf - k, "%s{\"sha256\":\"%s\",\"volume\":0.%d}", i ? "," : "", sha[layers[i]], layers[i] + 1);
    k += snprintf(buf + k, sizeof buf - k, "],\"unresolved_sound_ids\":[");
    for (int i = 0; i < unresolved; i++) k += snprintf(buf + k, sizeof buf - k, "%s\"u%d\"", i ? "," : "", i);
    snprintf(buf + k, sizeof buf - k, "],\"fully_playable\":%s}", unresolved ? "false" : "true");
    return buf;
}

static int parse_one(const char *sc, pc_scene_t **out)
{
    char json[8192];
    snprintf(json, sizeof json, "{\"scenes\":[%s]}", sc);
    int n = -1;
    CHECK_EQ_INT(pc_scenes_parse(json, ents, nents, false, out, &n), 0);
    return n;
}

static void test_scene_basic(void)
{
    pc_scene_t *s;
    int L[] = { 0, 1 };
    CHECK_EQ_INT(parse_one(scene("a", "雨聲", L, 2, 0), &s), 1);
    CHECK_STR(s[0].id, "a");
    CHECK_STR(s[0].name, "雨聲");
    CHECK_EQ_INT(s[0].nlayers, 2);
    CHECK_EQ_INT(s[0].missing, 0);
    CHECK_NEAR(s[0].layer[0].volume, 0.1, 1e-6);
    CHECK_NEAR(s[0].layer[1].volume, 0.2, 1e-6);
    CHECK(s[0].layer[1].entry == &ents[1]);
    CHECK_STR(s[0].layer[0].sha, sha[0]);
    free(s);
    int M[] = { 2 };
    CHECK_EQ_INT(parse_one(scene("b", "", M, 1, 0), &s), 1);
    CHECK_STR(s[0].name, "EN b");               /* empty zh name -> en */
    free(s);
}

static void test_scene_more_than_five_kept(void)
{
    pc_scene_t *s;
    int L[] = { 0, 1, 2, 3, 5, 6 };            /* six playable layers: shown since 2026-11-09 (at most 3 play at once) */
    CHECK_EQ_INT(parse_one(scene("six", "x", L, 6, 0), &s), 1);
    CHECK_EQ_INT(s[0].nlayers, 6);
    free(s);
}

static void test_scene_too_many(void)
{
    pc_scene_t *s;
    int L[PC_SCENE_MAX_LAYERS + 1];
    for (int i = 0; i <= PC_SCENE_MAX_LAYERS; i++) L[i] = i % 2 ? 2 : 0;
    CHECK_EQ_INT(PC_SCENE_MAX_LAYERS, 16);
    CHECK_EQ_INT(parse_one(scene("big", "x", L, PC_SCENE_MAX_LAYERS + 1, 0), &s), 0);
    free(s);
}

static void test_scene_unresolved_kept(void)
{
    pc_scene_t *s;
    int L[] = { 0 };
    CHECK_EQ_INT(parse_one(scene("u", "x", L, 1, 2), &s), 1);
    CHECK_EQ_INT(s[0].nlayers, 1);
    CHECK_EQ_INT(s[0].missing, 2);
    free(s);
}

static void test_scene_m4a_and_absent_layer(void)
{
    pc_scene_t *s;
    int L[] = { 3, 4, 7 };   /* flac ok, m4a skipped, absent skipped */
    CHECK_EQ_INT(parse_one(scene("m", "x", L, 3, 0), &s), 1);
    CHECK_EQ_INT(s[0].nlayers, 1);
    CHECK(s[0].layer[0].entry == &ents[3]);
    CHECK_EQ_INT(s[0].missing, 2);
    free(s);
}

static void test_scene_all_missing(void)
{
    pc_scene_t *s;
    int L[] = { 4, 7 };
    CHECK_EQ_INT(parse_one(scene("gone", "x", L, 2, 1), &s), 0);
    free(s);
}

static void test_scene_bad_json(void)
{
    pc_scene_t *s = NULL;
    int n = 5;
    CHECK(pc_scenes_parse("{not json", ents, nents, false, &s, &n) < 0);
    CHECK(pc_scenes_parse("{\"scenes\":3}", ents, nents, false, &s, &n) < 0);
}

static void test_custom_roundtrip(void)
{
    pc_scene_t *s;
    int L[] = { 0, 2, 5 };
    CHECK_EQ_INT(parse_one(scene("c1", "我的", L, 3, 0), &s), 1);
    s[0].layer[1].volume = 0.75f;
    snprintf(s[0].id, sizeof s[0].id, "custom:1");
    char path[] = "/tmp/pc_scenes_XXXXXX";
    close(mkstemp(path));
    CHECK_EQ_INT(pc_scenes_save_custom(s, 1, path), 0);
    free(s);
    FILE *f = fopen(path, "rb");
    char json[8192];
    size_t n = fread(json, 1, sizeof json - 1, f);
    fclose(f);
    json[n] = 0;
    int k;
    CHECK_EQ_INT(pc_scenes_parse(json, ents, nents, true, &s, &k), 0);
    CHECK_EQ_INT(k, 1);
    CHECK_STR(s[0].id, "custom:1");
    CHECK_STR(s[0].name, "我的");
    CHECK(s[0].custom);
    CHECK_EQ_INT(s[0].nlayers, 3);
    CHECK_NEAR(s[0].layer[1].volume, 0.75, 1e-6);
    free(s);
    unlink(path);
}

static void test_real_scenes(void)
{
    const char *p = FIXTURES "/../../../../../sleepmixer/dist_build/library.muk";
    FILE *f = fopen(p, "rb");
    if (!f) { printf("skip test_real_scenes\n"); return; }
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    pc_zip_entry_t *e;
    int n;
    CHECK_EQ_INT(pc_zip_open(&r, &e, &n), 0);
    uint8_t *json;
    uint32_t len;
    CHECK_EQ_INT(pc_zip_extract(&r, pc_zip_find(e, n, "default_scenes.json"), &json, &len), 0);
    pc_scene_t *s;
    int k;
    CHECK_EQ_INT(pc_scenes_parse((const char *)json, e, n, false, &s, &k), 0);
    printf("real scenes kept=%d\n", k);
    CHECK_EQ_INT(k, 99);   /* 104 scenes, none above 16 sounds; 5 have no resolved sound at all */
    free(s);
    free(json);
    pc_zip_free(e, n);
    fclose(f);
}

static void test_sounds_parse(void)
{
    mkents();
    char json[2048];
    snprintf(json, sizeof json, "{\"tracks\":["
             "{\"sha256\":\"%s\",\"ext\":\".ogg\",\"en_name\":\"Rain\",\"zh_tw_name\":\"雨\",\"category\":[\"水\",\"自然\"]},"
             "{\"sha256\":\"%s\",\"ext\":\".mp3\",\"en_name\":\"Cat\",\"zh_tw_name\":\"\",\"category\":[\"動物\",\"水\"]},"
             "{\"sha256\":\"%s\",\"ext\":\".m4a\",\"en_name\":\"M4A\",\"zh_tw_name\":\"x\",\"category\":[\"x\"]},"
             "{\"sha256\":\"%s\",\"ext\":\".ogg\",\"en_name\":\"Absent\",\"zh_tw_name\":\"y\",\"category\":[]},"
             "{\"sha256\":\"%s\",\"ext\":\".flac\",\"en_name\":\"Hum\",\"zh_tw_name\":\"嗡\",\"category\":[]}]}",
             sha[5], sha[1], sha[4], sha[7], sha[3]);
    pc_sound_t *s;
    int n;
    pc_sound_cats_t c;
    CHECK_EQ_INT(pc_sounds_parse(json, ents, nents, &s, &n, &c), 0);
    CHECK_EQ_INT(n, 3);                         /* m4a and the sound missing from the zip are left out */
    CHECK_EQ_INT(c.n, 3);
    CHECK_STR(c.name[0], "水");
    CHECK_STR(c.name[1], "自然");
    CHECK_STR(c.name[2], "動物");
    const pc_sound_t *r = pc_sound_find(s, n, sha[5]);
    CHECK(r != NULL);
    CHECK_STR(r->name, "雨");
    CHECK_EQ_INT((int)r->cats, 0x3);
    CHECK(r->entry == &ents[5]);
    r = pc_sound_find(s, n, sha[1]);
    CHECK_STR(r->name, "Cat");                  /* empty zh name -> en */
    CHECK_EQ_INT((int)r->cats, 0x5);
    CHECK_EQ_INT((int)pc_sound_find(s, n, sha[3])->cats, 0);
    CHECK(pc_sound_find(s, n, sha[4]) == NULL);
    free(s);
    CHECK(pc_sounds_parse("{nope", ents, nents, &s, &n, &c) < 0);
}

static void test_real_sounds(void)
{
    FILE *f = fopen(FIXTURES "/../../../../../sleepmixer/dist_build/library.muk", "rb");
    if (!f) { printf("skip test_real_sounds\n"); return; }
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    pc_zip_entry_t *e;
    int n;
    CHECK_EQ_INT(pc_zip_open(&r, &e, &n), 0);
    uint8_t *json;
    uint32_t len;
    CHECK_EQ_INT(pc_zip_extract(&r, pc_zip_find(e, n, "seed.json"), &json, &len), 0);
    pc_sound_t *s;
    int k;
    pc_sound_cats_t c;
    CHECK_EQ_INT(pc_sounds_parse((const char *)json, e, n, &s, &k, &c), 0);
    printf("real sounds=%d cats=%d\n", k, c.n);
    CHECK_EQ_INT(k, 759);
    CHECK_EQ_INT(c.n, 18);
    free(s);
    free(json);
    pc_zip_free(e, n);
    fclose(f);
}

int main(void)
{
    RUN(test_sounds_parse);
    RUN(test_real_sounds);
    mkents();
    RUN(test_scene_basic);
    RUN(test_scene_more_than_five_kept);
    RUN(test_scene_too_many);
    RUN(test_scene_unresolved_kept);
    RUN(test_scene_m4a_and_absent_layer);
    RUN(test_scene_all_missing);
    RUN(test_scene_bad_json);
    RUN(test_custom_roundtrip);
    RUN(test_real_scenes);
    return 0;
}
