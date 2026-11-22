// SPDX-License-Identifier: LGPL-2.1-or-later
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "id3_fixture.h"
#include "pc/pc_chipseq.h"
#include "pc/pc_io.h"
#include "pc/pc_library.h"
#include "pc/pc_paths.h"
#include "pc_test.h"

static char root[64];
static id3b_t T;

static void mk(const char *rel) { char p[256]; snprintf(p, sizeof p, "%s/%s", root, rel); CHECK(mkdir(p, 0777) == 0); }
static void song(const char *rel, const char *title, const char *date4, const char *ddmm)
{
    id3b_begin(&T, 3);
    if (title) id3b_text16(&T, "TIT2", title);
    if (date4) id3b_text0(&T, "TYER", date4);
    if (ddmm) id3b_text0(&T, "TDAT", ddmm);
    if (title && !strcmp(title, "one")) {               /* one tagged file: ReplayGain reaches the index */
        id3b_txxx(&T, "REPLAYGAIN_TRACK_GAIN", "-6.53 dB");
        id3b_txxx(&T, "REPLAYGAIN_TRACK_PEAK", "0.95");
        id3b_txxx(&T, "REPLAYGAIN_ALBUM_GAIN", "-7.00 dB");
    }
    id3b_end(&T, 0, 64);
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    CHECK(id3b_write(&T, p) == 0);
}
static void notag(const char *rel)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    FILE *f = fopen(p, "wb");
    fputs("not an mp3 tag", f);
    fclose(f);
}

static int cb_calls;
static void cb(int done, void *u) { (void)done; (void)u; cb_calls++; }

static void setup(void)
{
    strcpy(root, "/tmp/pc_lib_XXXXXX");
    CHECK(mkdtemp(root) != NULL);
    mk("B");
    mk("A");
    mk("早稻叽");
    song("A/2.mp3", "two", "2024", "0103");
    song("A/1.mp3", "one", "2023", "0501");
    song("B/x.MP3", "ex", "2024", "2010");
    notag("B/readme.txt");
    notag("B/notag.mp3");
    song("早稻叽/歌.mp3", "粤语", "2022", NULL);
}

static void test_scan_two_folders(void)
{
    pc_library_t lib;
    cb_calls = 0;
    CHECK_EQ_INT(pc_library_scan(root, &lib, cb, NULL), 0);
    CHECK_EQ_INT(lib.n, 5);
    CHECK_EQ_INT(lib.nfolders, 3);
    CHECK_STR(pc_str(&lib, lib.folders[0].name), "A");
    CHECK_EQ_INT(lib.folders[0].count, 2);
    CHECK_EQ_INT(lib.folders[1].count, 2);
    CHECK_STR(pc_str(&lib, lib.tracks[0].title), "one");      /* sorted by file name within the folder */
    CHECK_STR(pc_str(&lib, lib.tracks[1].title), "two");
    CHECK_EQ_INT(lib.tracks[0].folder, 0);
    char want[128];
    snprintf(want, sizeof want, "%s/A/1.mp3", root);
    CHECK_STR(pc_str(&lib, lib.tracks[0].path), want);
    pc_library_free(&lib);
}

static void test_scan_fallback_names(void)
{
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(root, &lib, NULL, NULL), 0);
    const pc_track_t *t = &lib.tracks[lib.folders[1].first];   /* B/notag.mp3 sorts before B/x.MP3 */
    CHECK_STR(pc_str(&lib, t->title), "notag");
    CHECK_STR(pc_str(&lib, t->artist), "B");
    CHECK_EQ_INT(t->date, 0);
    pc_library_free(&lib);
}

static void test_scan_flac(void)
{
    char r2[64], p[256], src[256];
    strcpy(r2, "/tmp/pc_libf_XXXXXX");
    CHECK(mkdtemp(r2) != NULL);
    snprintf(p, sizeof p, "%s/F", r2);
    CHECK(mkdir(p, 0755) == 0);
    snprintf(src, sizeof src, "%s/tone44.flac", FIXTURES);
    snprintf(p, sizeof p, "%s/F/a.FLAC", r2);
    FILE *in = fopen(src, "rb"), *o = fopen(p, "wb");
    CHECK(in && o);
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, in)) > 0) fwrite(b, 1, n, o);
    fclose(in);
    fclose(o);
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(r2, &lib, NULL, NULL), 0);
    CHECK_EQ_INT(lib.n, 1);
    CHECK_STR(pc_str(&lib, lib.tracks[0].title), "測試曲 Title");      /* from the FLAC Vorbis comment */
    CHECK_EQ_INT(lib.tracks[0].date, 20240517);
    CHECK(lib.tracks[0].apic_len > 0);
    pc_library_free(&lib);
    unlink(p);
    snprintf(p, sizeof p, "%s/F", r2);
    rmdir(p);
    rmdir(r2);
}

static void test_audio_extensions(void)
{
    CHECK(pc_library_is_audio("a.mp3") && pc_library_is_audio("B.MP3") && pc_library_is_audio("x.flac"));
    CHECK(pc_library_is_audio("x.ogg") && pc_library_is_audio("x.OPUS") && pc_library_is_audio("x.oga"));
    CHECK(pc_library_is_audio("x.m4a") && pc_library_is_audio("x.WAV"));
    CHECK(pc_library_is_audio("a.mod") && pc_library_is_audio("B.XM") && pc_library_is_audio("c.S3m") && pc_library_is_audio("d.it"));
    CHECK(pc_library_is_audio("a.aif") && pc_library_is_audio("b.AIFF") && pc_library_is_audio("c.aifc") && pc_library_is_audio("d.mpc") && pc_library_is_audio("e.TTA"));
    CHECK(!pc_library_is_audio("x.mid") && !pc_library_is_audio("x.mod.bak") && !pc_library_is_audio(".it"));
    CHECK(!pc_library_is_audio("readme.txt") && !pc_library_is_audio(".mp3") && !pc_library_is_audio("cover.jpg"));
    CHECK(!pc_library_is_audio("mp3") && !pc_library_is_audio("song.mp3.part"));
}

static void test_newest_date(void)
{
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(root, &lib, NULL, NULL), 0);
    CHECK_EQ_INT(lib.newest_date, 20241020);
    pc_library_free(&lib);
}

static void test_scan_utf8_names(void)
{
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(root, &lib, NULL, NULL), 0);
    CHECK_STR(pc_str(&lib, lib.folders[2].name), "早稻叽");
    CHECK_STR(pc_str(&lib, lib.tracks[lib.folders[2].first].title), "粤语");
    CHECK_EQ_INT(lib.tracks[lib.folders[2].first].date, 20220101);
    pc_library_free(&lib);
}

static void save_tmp(char *path, pc_library_t *lib)
{
    CHECK_EQ_INT(pc_library_scan(root, lib, NULL, NULL), 0);
    snprintf(path, 128, "%s/library.bin", root);
    CHECK_EQ_INT(pc_library_save(lib, path), 0);
}

static void test_index_roundtrip(void)
{
    pc_library_t a, b;
    char path[128];
    save_tmp(path, &a);
    CHECK_EQ_INT(pc_library_load(&b, path), 0);
    CHECK_EQ_INT(b.n, a.n);
    CHECK_EQ_INT(b.nfolders, a.nfolders);
    CHECK_EQ_INT(b.newest_date, a.newest_date);
    for (int i = 0; i < a.n; i++) {
        CHECK_STR(pc_str(&b, b.tracks[i].path), pc_str(&a, a.tracks[i].path));
        CHECK_STR(pc_str(&b, b.tracks[i].title), pc_str(&a, a.tracks[i].title));
        CHECK_EQ_INT(b.tracks[i].date, a.tracks[i].date);
        CHECK_EQ_INT(b.tracks[i].apic_off, a.tracks[i].apic_off);
        CHECK_EQ_INT(b.tracks[i].rg.track_cdb, a.tracks[i].rg.track_cdb);
        CHECK_EQ_INT(b.tracks[i].rg.track_peak, a.tracks[i].rg.track_peak);
        CHECK_EQ_INT(b.tracks[i].rg.album_cdb, a.tracks[i].rg.album_cdb);
    }
    pc_library_free(&a);
    pc_library_free(&b);
}

static void test_index_bad_magic(void)
{
    pc_library_t a, b;
    char path[128];
    save_tmp(path, &a);
    FILE *f = fopen(path, "r+b");
    fputc('X', f);
    fclose(f);
    CHECK_EQ_INT(pc_library_load(&b, path), -EINVAL);
    pc_library_free(&a);
}

static void test_scan_replaygain(void)
{
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(root, &lib, NULL, NULL), 0);
    CHECK_STR(pc_str(&lib, lib.tracks[0].title), "one");
    CHECK_EQ_INT(lib.tracks[0].rg.track_cdb, -653);
    CHECK_EQ_INT(lib.tracks[0].rg.track_peak, 9500);
    CHECK_EQ_INT(lib.tracks[0].rg.album_cdb, -700);
    CHECK_EQ_INT(lib.tracks[0].rg.album_peak, 0);
    CHECK_EQ_INT(lib.tracks[1].rg.track_cdb, PC_RG_NONE);   /* "two" has no ReplayGain */
    CHECK_EQ_INT(lib.tracks[1].rg.track_peak, 0);
    pc_library_free(&lib);
}

/* an index of the flat-folder layout (version 6) is not loaded: the library rescans */
static void test_index_old_version(void)
{
    pc_library_t a, b;
    char path[128];
    save_tmp(path, &a);
    FILE *f = fopen(path, "r+b");
    fseek(f, 4, SEEK_SET);
    uint32_t v;
    CHECK(fread(&v, 4, 1, f) == 1);
    CHECK_EQ_INT(v, 7);
    v = 6;
    fseek(f, 4, SEEK_SET);
    fwrite(&v, 4, 1, f);
    fclose(f);
    CHECK_EQ_INT(pc_library_load(&b, path), -EINVAL);
    pc_library_free(&a);
}

static void test_index_truncated(void)
{
    pc_library_t a, b;
    char path[128];
    save_tmp(path, &a);
    struct stat st;
    stat(path, &st);
    CHECK(truncate(path, st.st_size / 2) == 0);
    CHECK_EQ_INT(pc_library_load(&b, path), -EINVAL);
    CHECK_EQ_INT(pc_library_load(&b, "/nonexistent/library.bin"), -ENOENT);
    pc_library_free(&a);
}

static void test_index_save_over_existing(void)
{
    pc_library_t a, b;
    char path[128], bak[140];
    save_tmp(path, &a);
    CHECK_EQ_INT(pc_library_save(&a, path), 0);         /* replaces the file in place */
    snprintf(bak, sizeof bak, "%s.bak", path);
    CHECK(access(bak, F_OK) != 0);                      /* the backup is gone once the new file is in */
    CHECK_EQ_INT(pc_library_load(&b, path), 0);
    CHECK_EQ_INT(b.n, a.n);
    pc_library_free(&a);
    pc_library_free(&b);
}

static void test_scan_missing_root(void)
{
    pc_library_t lib;
    CHECK(pc_library_scan("/nonexistent/export", &lib, NULL, NULL) < 0);
    CHECK_EQ_INT(lib.n, 0);
    pc_library_free(&lib);
}

/* a card with no folders, and a folder with nothing playable: the name lists stay empty (v == NULL), which qsort must not be given */
static void test_scan_empty_dirs(void)
{
    char r2[64], p[256];
    strcpy(r2, "/tmp/pc_libe_XXXXXX");
    CHECK(mkdtemp(r2) != NULL);
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(r2, &lib, NULL, NULL), 0);
    CHECK_EQ_INT(lib.n, 0);
    CHECK_EQ_INT(lib.nfolders, 0);
    pc_library_free(&lib);
    snprintf(p, sizeof p, "%s/Docs", r2);
    CHECK(mkdir(p, 0755) == 0);
    CHECK_EQ_INT(pc_library_scan(r2, &lib, NULL, NULL), 0);
    CHECK_EQ_INT(lib.n, 0);
    pc_library_free(&lib);
    rmdir(p);
    rmdir(r2);
}

/* Files of 2 GiB or more are hidden: sparse files, skipped where the file system refuses them. */
static int sparse(const char *rel, off_t size)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    int fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    int bad = fd < 0 || ftruncate(fd, size) != 0;
    if (fd >= 0) close(fd);
    return bad;
}
static void test_lib_big_skipped(void)
{
    if (sizeof(off_t) < 8) { printf("skip test_lib_big_skipped (32-bit off_t)\n"); return; }
    mk("Big");
    song("Big/ok.mp3", "ok", "2024", NULL);
    if (sparse("Big/big.wav", (off_t)PC_FILE_MAX + 1)) { printf("skip test_lib_big_skipped (cannot make a sparse 2 GiB file here)\n"); return; }
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(root, &lib, NULL, NULL), 0);
    int in_big = 0;
    for (int i = 0; i < lib.n; i++) {
        const char *path = pc_str(&lib, lib.tracks[i].path);
        CHECK(!strstr(path, "big.wav"));
        if (strstr(path, "/Big/ok.mp3")) in_big++;
    }
    CHECK_EQ_INT(in_big, 1);
    CHECK_EQ_INT(lib.n, 6);                      /* the five of setup() plus Big/ok.mp3 */
    CHECK_EQ_INT(lib.skipped_big, 1);
    pc_library_free(&lib);
}
static void test_lib_big_edge(void)
{
    if (sizeof(off_t) < 8) { printf("skip test_lib_big_edge (32-bit off_t)\n"); return; }
    if (sparse("Big/edge.wav", (off_t)PC_FILE_MAX)) { printf("skip test_lib_big_edge (cannot make a sparse 2 GiB file here)\n"); return; }
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(root, &lib, NULL, NULL), 0);
    int edge = 0;
    for (int i = 0; i < lib.n; i++) if (strstr(pc_str(&lib, lib.tracks[i].path), "edge.wav")) edge++;
    CHECK_EQ_INT(edge, 1);                       /* exactly PC_FILE_MAX is listed */
    CHECK_EQ_INT(lib.skipped_big, 1);            /* big.wav still counted, edge.wav not */
    pc_library_free(&lib);
}

/* hires T8e: after a rescan the playing track, the queued one, the history and the folder are found again by path / name in the new library */
static void test_find_path(void)
{
    static char pool[] = "\0/e/a/1.mp3\0/e/a/2.mp3\0/e/b/1.mp3\0a\0b\0/e/a\0/e/b\0";
    pc_track_t t[3] = { { .path = 1 }, { .path = 12 }, { .path = 23 } };
    pc_folder_t f[2] = { { .name = 34, .path = 38, .first = 0, .count = 2 }, { .name = 36, .path = 43, .first = 2, .count = 1 } };
    pc_library_t lib = { .tracks = t, .n = 3, .folders = f, .nfolders = 2, .pool = pool, .pool_len = sizeof pool };
    CHECK_EQ_INT(pc_library_find(&lib, "/e/a/1.mp3"), 0);
    CHECK_EQ_INT(pc_library_find(&lib, "/e/b/1.mp3"), 2);
    CHECK_EQ_INT(pc_library_find(&lib, "/e/a/3.mp3"), -1);
    CHECK_EQ_INT(pc_library_find(&lib, "/E/A/1.MP3"), -1);    /* exact bytes (the card's own spelling) */
    CHECK_EQ_INT(pc_library_find(&lib, ""), -1);
    CHECK_EQ_INT(pc_library_find(&lib, NULL), -1);
    CHECK_EQ_INT(pc_library_find_folder(&lib, "/e/b"), 1);   /* by path: two folders may share a name at different levels */
    CHECK_EQ_INT(pc_library_find_folder(&lib, "b"), -1);
    CHECK_EQ_INT(pc_library_find_folder(&lib, "/e/c"), -1);
    int ix[3];                                               /* fix 1 (review M2): the indexed lookup agrees */
    pc_library_path_order(&lib, ix);
    for (int i = 0; i < 3; i++) CHECK_EQ_INT(pc_library_find_sorted(&lib, ix, pc_str(&lib, t[i].path)), i);
    CHECK_EQ_INT(pc_library_find_sorted(&lib, ix, "/e/a/3.mp3"), -1);
    CHECK_EQ_INT(pc_library_find_sorted(&lib, ix, ""), -1);
    /* a bigger one, out of order: every path found, every miss missed */
    enum { N = 500 };
    static char big[N * 16 + 1];
    static pc_track_t bt[N];
    static int bx[N];
    for (int i = 0; i < N; i++) { bt[i].path = (uint32_t)(i * 16); snprintf(big + i * 16, 16, "/x/%05d.mp3", (i * 7919) % N); }
    pc_library_t bl = { .tracks = bt, .n = N, .pool = big, .pool_len = sizeof big };
    pc_library_path_order(&bl, bx);
    for (int i = 1; i < N; i++) CHECK(strcmp(pc_str(&bl, bt[bx[i - 1]].path), pc_str(&bl, bt[bx[i]].path)) < 0);
    for (int i = 0; i < N; i++) CHECK_EQ_INT(pc_library_find_sorted(&bl, bx, pc_str(&bl, bt[i].path)), i);
    CHECK_EQ_INT(pc_library_find_sorted(&bl, bx, "/x/99999.mp3"), -1);
    pc_library_t none = {0};
    CHECK_EQ_INT(pc_library_find(&none, "/e/a/1.mp3"), -1);
    pc_library_path_order(&none, bx);
    CHECK_EQ_INT(pc_library_find_sorted(&none, bx, "/e/a/1.mp3"), -1);
    CHECK_EQ_INT(pc_library_find_folder(&none, "/e/a"), -1);
}

/* the device's own folder is never music: PC_INT_NAME at the top of the scanned root is skipped (an mp3 inside is not indexed) */
static void test_scan_skips_player_dir(void)
{
    char r2[64], p[256];
    strcpy(r2, "/tmp/pc_libp_XXXXXX");
    CHECK(mkdtemp(r2) != NULL);
    snprintf(p, sizeof p, "%s/%s", r2, PC_INT_NAME); CHECK(mkdir(p, 0777) == 0);
    snprintf(p, sizeof p, "%s/%s/x.mp3", r2, PC_INT_NAME); { FILE *f = fopen(p, "wb"); CHECK(f != NULL); fputs("x", f); fclose(f); }
    snprintf(p, sizeof p, "%s/A", r2); CHECK(mkdir(p, 0777) == 0);
    snprintf(p, sizeof p, "%s/A/1.mp3", r2); { FILE *f = fopen(p, "wb"); CHECK(f != NULL); fputs("x", f); fclose(f); }
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(r2, &lib, NULL, NULL), 0);
    CHECK_EQ_INT(lib.n, 1);
    CHECK_EQ_INT(lib.nfolders, 1);
    pc_library_free(&lib);
    snprintf(p, sizeof p, "rm -rf %s", r2); CHECK(system(p) == 0);
}


/* the folder tree: songs at every level, depth first order, one contiguous range per sub-tree, empty branches left out, the depth limit, save / load */
static void put(const char *r, const char *rel)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", r, rel);
    FILE *f = fopen(p, "wb");
    CHECK(f != NULL);
    fputs("x", f);
    fclose(f);
}
static void dir(const char *r, const char *rel) { char p[256]; snprintf(p, sizeof p, "%s/%s", r, rel); CHECK(mkdir(p, 0777) == 0); }
static void test_scan_tree(void)
{
    char r[64], cmd[96];
    strcpy(r, "/tmp/pc_libt_XXXXXX");
    CHECK(mkdtemp(r) != NULL);
    put(r, "r2.mp3"); put(r, "r1.mp3");                       /* songs directly in the music root */
    dir(r, "Rock"); dir(r, "Rock/Live"); dir(r, "Rock/Live/Paris"); dir(r, "Rock/Empty"); dir(r, "Rock/Empty/Deeper");
    put(r, "Rock/a.mp3"); put(r, "Rock/Live/b.mp3"); put(r, "Rock/Live/Paris/c.mp3"); put(r, "Rock/Live/Paris/d.mp3");
    dir(r, "Jazz"); dir(r, "Jazz/Cool"); put(r, "Jazz/Cool/e.mp3");   /* a folder with nothing of its own but one sub-folder */
    dir(r, ".hidden"); put(r, ".hidden/x.mp3");
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(r, &lib, NULL, NULL), 0);
    CHECK_EQ_INT(lib.n, 7);
    CHECK_EQ_INT(lib.root_own, 2);
    CHECK_EQ_INT(lib.root_nchild, 2);
    CHECK_EQ_INT(lib.nfolders, 5);                            /* Jazz, Jazz/Cool, Rock, Rock/Live, Rock/Live/Paris (Rock/Empty is not a folder) */
    CHECK_STR(pc_str(&lib, lib.folders[0].name), "Jazz");     /* pre-order, names sorted */
    CHECK_STR(pc_str(&lib, lib.folders[1].name), "Cool");
    CHECK_STR(pc_str(&lib, lib.folders[2].name), "Rock");
    CHECK_STR(pc_str(&lib, lib.tracks[0].title), "r1");        /* the root's own songs first */
    CHECK_EQ_INT(lib.tracks[0].folder, PC_NO_FOLDER);
    CHECK_EQ_INT(lib.tracks[2].folder, 1);                     /* Jazz/Cool/e comes before Rock's songs; Jazz itself has none */
    CHECK_EQ_INT(lib.folders[0].count, 0);
    CHECK_EQ_INT(lib.folders[0].sub_count, 1);
    CHECK_EQ_INT(lib.folders[0].nsub, 2);
    CHECK_EQ_INT(lib.folders[0].nchild, 1);
    CHECK_EQ_INT(lib.folders[0].parent, -1);
    CHECK_EQ_INT(lib.folders[1].parent, 0);
    CHECK_EQ_INT(lib.folders[2].count, 1);                     /* Rock: a */
    CHECK_EQ_INT(lib.folders[2].sub_count, 4);                 /* + Live/b + Paris/c, d */
    CHECK_EQ_INT(lib.folders[2].nsub, 3);
    CHECK_EQ_INT(lib.folders[2].nchild, 1);
    CHECK_EQ_INT(lib.folders[4].parent, 3);
    CHECK_EQ_INT(lib.folders[2].first, 3);
    for (int f = 0; f < lib.nfolders; f++) {                   /* every song of a sub-tree is in its range, and only those */
        int in = 0;
        for (int t = 0; t < lib.n; t++) {
            const int g = lib.tracks[t].folder == PC_NO_FOLDER ? -1 : lib.tracks[t].folder;
            const bool inside = g >= 0 && pc_library_folder_in(&lib, f, g);
            in += inside;
            CHECK(inside == (t >= lib.folders[f].first && t < lib.folders[f].first + lib.folders[f].sub_count));
        }
        CHECK_EQ_INT(in, lib.folders[f].sub_count);
    }
    int first, count, nchild;
    pc_library_level(&lib, -1, &first, &count, &nchild);
    CHECK_EQ_INT(first, 0); CHECK_EQ_INT(count, 2); CHECK_EQ_INT(nchild, 2);
    CHECK_EQ_INT(pc_library_child(&lib, -1, 0), 0);            /* Jazz */
    CHECK_EQ_INT(pc_library_child(&lib, -1, 1), 2);            /* Rock: Jazz's sub-tree is skipped */
    CHECK_EQ_INT(pc_library_child(&lib, -1, 2), -1);
    CHECK_EQ_INT(pc_library_child(&lib, 2, 0), 3);
    CHECK_EQ_INT(pc_library_child(&lib, 2, 1), -1);
    CHECK_EQ_INT(pc_library_next_sibling(&lib, 0, -1), 2);
    CHECK_EQ_INT(pc_library_next_sibling(&lib, 2, -1), -1);
    pc_library_scope(&lib, 2, &first, &count);
    CHECK_EQ_INT(first, 3); CHECK_EQ_INT(count, 4);
    pc_library_scope(&lib, -1, &first, &count);
    CHECK_EQ_INT(first, 0); CHECK_EQ_INT(count, 7);
    char p[128], want[128];
    snprintf(want, sizeof want, "%s/Rock/Live", r);
    CHECK_EQ_INT(pc_library_find_folder(&lib, want), 3);
    snprintf(p, sizeof p, "%s/lib.bin", r);
    CHECK_EQ_INT(pc_library_save(&lib, p), 0);
    pc_library_t b;
    CHECK_EQ_INT(pc_library_load(&b, p), 0);
    CHECK_EQ_INT(b.n, 7); CHECK_EQ_INT(b.nfolders, 5); CHECK_EQ_INT(b.root_own, 2); CHECK_EQ_INT(b.root_nchild, 2);
    CHECK(!memcmp(b.folders, lib.folders, 5 * sizeof *b.folders));
    pc_library_free(&b);
    pc_library_free(&lib);
    snprintf(cmd, sizeof cmd, "rm -rf %s", r);
    CHECK(system(cmd) == 0);
}

/* the depth limit: PC_LIB_DEPTH sub-folder levels below the root are read, a deeper one is not */
static void test_scan_depth_limit(void)
{
    char r[64], rel[256], cmd[96];
    strcpy(r, "/tmp/pc_libd_XXXXXX");
    CHECK(mkdtemp(r) != NULL);
    rel[0] = 0;
    for (int d = 1; d <= PC_LIB_DEPTH + 2; d++) {
        strcat(rel, d > 1 ? "/d" : "d");
        char n[8]; snprintf(n, sizeof n, "%d", d);
        strcat(rel, n);
        dir(r, rel);
        char f[300]; snprintf(f, sizeof f, "%s/s.mp3", rel);
        put(r, f);
    }
    pc_library_t lib;
    CHECK_EQ_INT(pc_library_scan(r, &lib, NULL, NULL), 0);
    CHECK_EQ_INT(lib.nfolders, PC_LIB_DEPTH);
    CHECK_EQ_INT(lib.n, PC_LIB_DEPTH);
    CHECK_EQ_INT(lib.folders[0].nsub, PC_LIB_DEPTH);
    CHECK_EQ_INT(lib.folders[0].sub_count, PC_LIB_DEPTH);
    pc_library_free(&lib);
    snprintf(cmd, sizeof cmd, "rm -rf %s", r);
    CHECK(system(cmd) == 0);
}

/* a damaged tree index is refused, not followed */
static void test_index_bad_tree(void)
{
    pc_library_t a, b;
    char path[128];
    save_tmp(path, &a);
    FILE *f = fopen(path, "r+b");
    pc_folder_t d;
    const long at = (long)(4 + 7 * 4 + (size_t)a.n * sizeof(pc_track_t));   /* the first folder entry */
    fseek(f, at, SEEK_SET);
    CHECK(fread(&d, sizeof d, 1, f) == 1);
    d.nsub = 99;                                              /* a sub-tree bigger than the folder list */
    fseek(f, at, SEEK_SET);
    fwrite(&d, sizeof d, 1, f);
    fclose(f);
    CHECK_EQ_INT(pc_library_load(&b, path), -EINVAL);
    pc_library_free(&a);
}

/* D1 (wave 5): an index whose folder child counts disagree with the depth-first hop (or whose child does not point back at its parent) is refused -> rescan */
static void test_index_bad_child_counts(void)
{
    char r[64], cmd[96], p[128];
    strcpy(r, "/tmp/pc_libc_XXXXXX");
    CHECK(mkdtemp(r) != NULL);
    dir(r, "Jazz"); dir(r, "Jazz/Cool"); put(r, "Jazz/Cool/e.mp3"); dir(r, "Jazz/Warm"); put(r, "Jazz/Warm/f.mp3");
    dir(r, "Rock"); dir(r, "Rock/Live"); put(r, "Rock/Live/b.mp3"); put(r, "Rock/a.mp3"); put(r, "r1.mp3");
    pc_library_t lib, b;
    CHECK_EQ_INT(pc_library_scan(r, &lib, NULL, NULL), 0);
    CHECK_EQ_INT(lib.nfolders, 5);                            /* Jazz, Cool, Warm, Rock, Live */
    snprintf(p, sizeof p, "%s/lib.bin", r);
    CHECK_EQ_INT(pc_library_save(&lib, p), 0);
    CHECK_EQ_INT(pc_library_load(&b, p), 0);                  /* the untouched index loads */
    pc_library_free(&b);
    enum { NCASE = 7 };
    for (int k = 0; k < NCASE; k++) {
        pc_folder_t *d = lib.folders;
        const int32_t o0 = d[0].nsub, o1 = d[0].nchild, o2 = d[1].parent, o3 = d[3].nsub, o4 = d[3].nchild;
        const int rn = lib.root_nchild;
        switch (k) {
        case 0: d[0].nsub = 2; break;                         /* the reviewer's case: still < nfolders, but the hop finds 1 child, not 2 */
        case 1: d[0].nchild = 1; break;                       /* Jazz has 2 children, says 1 */
        case 2: d[0].nchild = 0; break;                       /* a sub-tree with children that says it has none */
        case 3: lib.root_nchild = 3; break;                   /* the root has 2 */
        case 4: lib.root_nchild = 1; break;
        case 5: d[1].parent = -1; break;                      /* a child that does not point back at its parent */
        case 6: d[3].nchild = 0; break;                       /* Rock has Live */
        }
        CHECK_EQ_INT(pc_library_save(&lib, p), 0);
        CHECK_EQ_INT(pc_library_load(&b, p), -EINVAL);
        d[0].nsub = o0; d[0].nchild = o1; d[1].parent = o2; d[3].nsub = o3; d[3].nchild = o4; lib.root_nchild = rn;
    }
    CHECK_EQ_INT(pc_library_save(&lib, p), 0);
    CHECK_EQ_INT(pc_library_load(&b, p), 0);
    pc_library_free(&b);
    pc_library_free(&lib);
    snprintf(cmd, sizeof cmd, "rm -rf %s", r);
    CHECK(system(cmd) == 0);
}


/* format tiers (2026-11-18): an experimental format with the setting off is listed by its file name only, the scan never opens it; with the setting on it is read like any file */
static bool s_exp;
static bool exp_src(void) { return s_exp; }
static void tagged(const char *dirp, const char *name, const char *title)
{
    id3b_t t;
    id3b_begin(&t, 3);
    id3b_text16(&t, "TIT2", title);
    id3b_end(&t, 0, 64);
    char p[256];
    snprintf(p, sizeof p, "%s/%s", dirp, name);
    CHECK(id3b_write(&t, p) == 0);
}
static const char *title_of(const pc_library_t *lib, const char *base)
{
    for (int i = 0; i < lib->n; i++) {
        const char *path = pc_str(lib, lib->tracks[i].path);
        if (strstr(path, base)) return pc_str(lib, lib->tracks[i].title);
    }
    return "?";
}
static void test_scan_experimental_blind(void)
{
    char r2[64], cmd[96];
    strcpy(r2, "/tmp/pc_libx_XXXXXX");
    CHECK(mkdtemp(r2) != NULL);
    tagged(r2, "slow.a2m", "TaggedA2M");        /* AdPlug, experimental */
    tagged(r2, "calm.rad", "TaggedRAD");        /* AdPlug, stable */
    tagged(r2, "both.dtm", "TaggedDTM");        /* AdPlug experimental, libxmp stable: one reader is gated, so the scan stays blind */
    tagged(r2, "both.imf", "TaggedIMF");        /* both readers stable */
    pc_tier_set_source(exp_src);
    for (int on = 0; on < 2; on++) {
        s_exp = on;
        pc_library_t lib;
        CHECK_EQ_INT(pc_library_scan(r2, &lib, NULL, NULL), 0);
        CHECK_EQ_INT(lib.n, 4);                                    /* every one stays listed */
        CHECK_STR(title_of(&lib, "calm.rad"), "TaggedRAD");        /* a stable format is read either way */
        CHECK_STR(title_of(&lib, "both.imf"), "TaggedIMF");
        CHECK_STR(title_of(&lib, "slow.a2m"), on ? "TaggedA2M" : "slow");   /* off: the file name without its suffix, no tag read */
        CHECK_STR(title_of(&lib, "both.dtm"), on ? "TaggedDTM" : "both");
        pc_library_free(&lib);
    }
    pc_tier_set_source(NULL);
    snprintf(cmd, sizeof cmd, "rm -rf %s", r2);
    CHECK(system(cmd) == 0);
}
int main(void)
{
    RUN(test_scan_experimental_blind);
    setup();
    RUN(test_scan_skips_player_dir);
    RUN(test_scan_two_folders);
    RUN(test_scan_tree);
    RUN(test_scan_depth_limit);
    RUN(test_index_bad_tree);
    RUN(test_index_bad_child_counts);
    RUN(test_scan_flac);
    RUN(test_audio_extensions);
    RUN(test_scan_fallback_names);
    RUN(test_newest_date);
    RUN(test_scan_utf8_names);
    RUN(test_scan_replaygain);
    RUN(test_index_roundtrip);
    RUN(test_index_old_version);
    RUN(test_index_bad_magic);
    RUN(test_index_truncated);
    RUN(test_index_save_over_existing);
    RUN(test_scan_missing_root);
    RUN(test_scan_empty_dirs);
    RUN(test_lib_big_skipped);
    RUN(test_lib_big_edge);
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    CHECK(system(cmd) == 0);
    RUN(test_find_path);
    return 0;
}
