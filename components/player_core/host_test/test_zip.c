// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include "pc/pc_io.h"
#include "pc/pc_zip.h"
#include "pc_test.h"

static uint8_t *load(const char *name, uint32_t *len)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    *len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len + 1);
    CHECK(fread(b, 1, *len, f) == *len);
    fclose(f);
    return b;
}

static void test_stored(void)
{
    uint32_t len;
    uint8_t *z = load("stored.zip", &len);
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_zip_entry_t *e;
    int n;
    CHECK_EQ_INT(pc_zip_open(&r, &e, &n), 0);
    CHECK_EQ_INT(n, 3);
    CHECK_STR(e[0].name, "a/hello.txt");   /* sorted by name */
    CHECK_STR(e[1].name, "b.txt");
    CHECK_STR(e[2].name, "ゲーム/曲.nsf");
    uint8_t *out;
    uint32_t ol;
    CHECK_EQ_INT(pc_zip_extract(&r, &e[0], &out, &ol), 0);
    CHECK_EQ_INT(ol, 11);
    CHECK(memcmp(out, "hello world", 11) == 0);
    free(out);
    uint32_t off;
    CHECK_EQ_INT(pc_zip_data_offset(&r, &e[1], &off), 0);
    CHECK(memcmp(z + off, "bee", 3) == 0);
    pc_zip_free(e, n);
    free(z);
}

static void test_deflate(void)
{
    uint32_t len, xl;
    uint8_t *z = load("deflate.zip", &len), *want = load("text.txt.expected", &xl);
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_zip_entry_t *e;
    int n;
    CHECK_EQ_INT(pc_zip_open(&r, &e, &n), 0);
    const pc_zip_entry_t *t = pc_zip_find(e, n, "text.txt");
    CHECK(t != NULL);
    CHECK_EQ_INT(t->method, 8);
    CHECK(t->csize < t->usize);
    uint8_t *out;
    uint32_t ol;
    CHECK_EQ_INT(pc_zip_extract(&r, t, &out, &ol), 0);
    CHECK_EQ_INT(ol, xl);
    CHECK(memcmp(out, want, xl) == 0);
    free(out);
    const pc_zip_entry_t *em = pc_zip_find(e, n, "empty.txt");
    CHECK(em != NULL);
    CHECK_EQ_INT(pc_zip_extract(&r, em, &out, &ol), 0);
    CHECK_EQ_INT(ol, 0);
    free(out);
    CHECK(pc_zip_find(e, n, "missing") == NULL);
    pc_zip_free(e, n);
    free(z);
    free(want);
}

static void test_badcrc(void)
{
    uint32_t len;
    uint8_t *z = load("badcrc.zip", &len);
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_zip_entry_t *e;
    int n;
    CHECK_EQ_INT(pc_zip_open(&r, &e, &n), 0);
    uint8_t *out = NULL;
    uint32_t ol;
    int rc = pc_zip_extract(&r, pc_zip_find(e, n, "text.txt"), &out, &ol);
    CHECK(rc == -EBADMSG);
    CHECK(out == NULL);
    pc_zip_free(e, n);
    free(z);
}

static void test_too_big(void)
{
    uint32_t len;
    uint8_t *z = load("deflate.zip", &len);
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    pc_zip_entry_t *e;
    int n;
    CHECK_EQ_INT(pc_zip_open(&r, &e, &n), 0);
    pc_zip_entry_t big = *pc_zip_find(e, n, "text.txt");
    big.usize = 5u << 20;
    uint8_t *out = NULL;
    uint32_t ol;
    CHECK_EQ_INT(pc_zip_extract(&r, &big, &out, &ol), -E2BIG);
    CHECK(out == NULL);
    pc_zip_free(e, n);
    free(z);
}

static void test_gunzip(void)
{
    uint32_t len;
    uint8_t *g = load("tune.vgz", &len), *out;
    uint32_t ol;
    CHECK_EQ_INT(pc_gunzip(g, len, &out, &ol), 0);
    CHECK_EQ_INT(ol, 4 + 1024);
    CHECK(memcmp(out, "Vgm ", 4) == 0);
    CHECK_EQ_INT(out[4 + 255], 255);
    free(out);
    g[len - 5] ^= 1;   /* CRC32 trailer */
    CHECK(pc_gunzip(g, len, &out, &ol) < 0);
    CHECK(pc_gunzip((const uint8_t *)"not gzip", 8, &out, &ol) < 0);
    free(g);
}

static void test_not_zip(void)
{
    uint8_t junk[3000];
    for (size_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 131 + 5);
    pc_reader_t r;
    pc_reader_mem(&r, junk, sizeof junk);
    pc_zip_entry_t *e;
    int n;
    CHECK(pc_zip_open(&r, &e, &n) < 0);
    pc_reader_mem(&r, junk, 10);
    CHECK(pc_zip_open(&r, &e, &n) < 0);
}

static void test_real_muk(void)
{
    const char *p = FIXTURES "/../../../../../sleepmixer/dist_build/library.muk";
    FILE *f = fopen(p, "rb");
    if (!f) { printf("skip test_real_muk (no %s)\n", p); return; }
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    pc_zip_entry_t *e;
    int n;
    CHECK_EQ_INT(pc_zip_open(&r, &e, &n), 0);
    CHECK(n > 700);
    const pc_zip_entry_t *s = pc_zip_find(e, n, "default_scenes.json");
    CHECK(s != NULL && s->method == 0);
    pc_zip_free(e, n);
    fclose(f);
}


static void test_lookup(void)   /* no heap: one pass over the central directory */
{
    uint32_t len;
    uint8_t *z = load("stored.zip", &len);
    pc_reader_t r;
    pc_reader_mem(&r, z, len);
    const char *names[] = { "b.txt", "missing", "a/hello.txt" };
    pc_zip_entry_t e[3];
    CHECK_EQ_INT(pc_zip_lookup(&r, names, 3, e), 2);
    CHECK(e[0].name == names[0]);
    CHECK(e[1].name == NULL);
    CHECK(e[2].name == names[2]);
    uint32_t off;
    CHECK_EQ_INT(pc_zip_data_offset(&r, &e[0], &off), 0);
    CHECK(memcmp(z + off, "bee", 3) == 0);
    CHECK_EQ_INT(e[2].usize, 11);
    CHECK_EQ_INT(e[2].method, 0);
    for (uint32_t cut = 0; cut < len; cut++) {        /* every truncation: an error or fewer entries, never a crash */
        pc_reader_mem(&r, z, cut);
        int rc = pc_zip_lookup(&r, names, 3, e);
        CHECK(rc < 0 || rc <= 2);
    }
    uint8_t *c = malloc(len + 40);                     /* a zip comment after the end record */
    memcpy(c, z, len);
    memset(c + len, 'x', 40);
    c[len - 2] = 40;
    pc_reader_mem(&r, c, len + 40);
    CHECK_EQ_INT(pc_zip_lookup(&r, names, 3, e), 2);
    for (uint32_t i = 0; i < len; i++) {               /* every byte flipped */
        c[i] ^= 0xFF;
        (void)pc_zip_lookup(&r, names, 3, e);
        c[i] ^= 0xFF;
    }
    free(c);
    uint8_t junk[300] = { 0 };
    pc_reader_mem(&r, junk, sizeof junk);
    CHECK(pc_zip_lookup(&r, names, 3, e) < 0);
    free(z);
}

/* ---- untrusted zips: the card's zip may be anything (a bad file is an error line, never a crash) ---- */
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

/* a one-entry stored zip "a.bin" = "hello": local header, data, central directory, end record */
static uint32_t craft(uint8_t *z)
{
    uint32_t p = 0;
    memset(z, 0, 256);
    put32(z, 0x04034b50); put16(z + 26, 5); memcpy(z + 30, "a.bin", 5); memcpy(z + 35, "hello", 5);
    p = 40;
    uint32_t cd = p;
    put32(z + p, 0x02014b50); put16(z + p + 28, 5); put32(z + p + 20, 5); put32(z + p + 24, 5);
    put32(z + p + 16, pc_crc32(0, (const uint8_t *)"hello", 5)); put32(z + p + 42, 0); memcpy(z + p + 46, "a.bin", 5);
    p += 51;
    put32(z + p, 0x06054b50); put16(z + p + 8, 1); put16(z + p + 10, 1); put32(z + p + 12, 51); put32(z + p + 16, cd);
    return p + 22;
}

static void test_untrusted(void)
{
    uint8_t z[256], *out;
    uint32_t len = craft(z), ol, off;
    pc_reader_t r;
    pc_zip_entry_t *e;
    int n;
    pc_reader_mem(&r, z, len);
    CHECK_EQ_INT(pc_zip_open(&r, &e, &n), 0);
    CHECK_EQ_INT(n, 1);
    CHECK(pc_zip_at(e, n, -1) == NULL && pc_zip_at(e, n, 1) == NULL && pc_zip_at(e, n, 10) == NULL && pc_zip_at(NULL, n, 0) == NULL);   /* the console crash: index 10 of a 10-entry zip */
    CHECK(pc_zip_at(e, n, 0) == &e[0]);
    CHECK_EQ_INT(pc_zip_extract(&r, &e[0], &out, &ol), 0);
    free(out);
    /* NULL arguments */
    CHECK(pc_zip_extract(&r, NULL, &out, &ol) < 0 && out == NULL);
    CHECK(pc_zip_extract(NULL, &e[0], &out, &ol) < 0);
    CHECK(pc_zip_extract(&r, &e[0], NULL, &ol) < 0);
    CHECK(pc_zip_data_offset(&r, NULL, &off) < 0 && pc_zip_data_offset(&r, &e[0], NULL) < 0);
    pc_zip_entry_t *junk = e;
    CHECK(pc_zip_open(&r, NULL, &n) < 0 && pc_zip_open(NULL, &junk, &n) < 0 && junk == NULL);
    CHECK(pc_zip_find(NULL, 0, "a.bin") == NULL && pc_zip_find(e, 1, NULL) == NULL && pc_zip_find(e, -3, "a.bin") == NULL);
    CHECK(pc_gunzip(NULL, 100, &out, &ol) < 0 && pc_gunzip((const uint8_t *)"x", 1, NULL, &ol) < 0);
    const char *nm[1] = { "a.bin" };
    pc_zip_entry_t le[1];
    CHECK(pc_zip_lookup(&r, NULL, 1, le) < 0 && pc_zip_lookup(&r, nm, 0, le) < 0 && pc_zip_lookup(&r, nm, 1, NULL) < 0);
    /* hostile entries handed in by hand: offsets past the end, huge sizes, method, local header mismatch */
    pc_zip_entry_t b = e[0];
    b.local_off = len; CHECK(pc_zip_extract(&r, &b, &out, &ol) < 0 && !out);
    b.local_off = 0xffffffffu; CHECK(pc_zip_extract(&r, &b, &out, &ol) < 0);
    CHECK(pc_zip_data_offset(&r, &b, &off) < 0);
    b = e[0]; b.csize = 0xffffffffu; CHECK(pc_zip_extract(&r, &b, &out, &ol) < 0);
    b = e[0]; b.usize = 0xffffffffu; CHECK_EQ_INT(pc_zip_extract(&r, &b, &out, &ol), -E2BIG);
    b = e[0]; b.method = 8; b.csize = 0x7fffffffu; CHECK(pc_zip_extract(&r, &b, &out, &ol) < 0);
    b = e[0]; b.method = 99; CHECK_EQ_INT(pc_zip_extract(&r, &b, &out, &ol), -ENOTSUP);
    b = e[0]; b.csize = 4; CHECK(pc_zip_extract(&r, &b, &out, &ol) < 0);   /* stored: csize != usize */
    b = e[0]; b.local_off = 3; CHECK(pc_zip_extract(&r, &b, &out, &ol) < 0);   /* not a local header */
    pc_zip_free(e, n);
    /* crafted directories */
    uint8_t c[256];
    memcpy(c, z, len);
    put16(c + 40 + 51 + 10, 60000); put16(c + 40 + 51 + 8, 60000);   /* count far beyond the directory */
    pc_reader_mem(&r, c, len);
    CHECK(pc_zip_open(&r, &e, &n) < 0);
    CHECK(pc_zip_lookup(&r, nm, 1, le) < 0);
    memcpy(c, z, len);
    put32(c + 40 + 51 + 16, len);                                      /* directory offset past the end */
    CHECK(pc_zip_open(&r, &e, &n) < 0);
    memcpy(c, z, len);
    put32(c + 40 + 42, 0x7ffffff0u);                                   /* entry local offset past the end: refused at open */
    CHECK(pc_zip_open(&r, &e, &n) < 0);
    memcpy(c, z, len);
    put32(c + 40 + 20, 0xfffffff0u);                                   /* csize past the end */
    CHECK(pc_zip_open(&r, &e, &n) < 0);
    memcpy(c, z, len);
    put16(c + 40 + 28, 0xffff);                                        /* name length past the directory */
    CHECK(pc_zip_open(&r, &e, &n) < 0);
    /* every truncation */
    for (uint32_t cut = 0; cut <= len; cut++) {
        pc_reader_mem(&r, z, cut);
        if (!pc_zip_open(&r, &e, &n)) { for (int i = 0; i < n; i++) if (!pc_zip_extract(&r, &e[i], &out, &ol)) free(out); pc_zip_free(e, n); }
    }
}

/* a mutation loop over valid zips (the fixtures + the crafted one): flip, set and splice bytes, open + extract every entry */
static uint32_t rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }
static void fuzz_one(const uint8_t *buf, uint32_t len)
{
    pc_reader_t r;
    pc_zip_entry_t *e;
    int n;
    uint8_t *out;
    uint32_t ol, off;
    pc_reader_mem(&r, buf, len);
    if (!pc_zip_open(&r, &e, &n)) {
        for (int i = 0; i < n; i++) {
            if (!pc_zip_extract(&r, &e[i], &out, &ol)) free(out);
            (void)pc_zip_data_offset(&r, &e[i], &off);
        }
        (void)pc_zip_at(e, n, n);
        pc_zip_free(e, n);
    }
    const char *nm[2] = { "b.txt", "text.txt" };
    pc_zip_entry_t le[2];
    (void)pc_zip_lookup(&r, nm, 2, le);
    if (!pc_gunzip(buf, len, &out, &ol)) free(out);
}

static void test_fuzz(void)
{
    const char *files[] = { "stored.zip", "deflate.zip", "badcrc.zip" };
    const char *env = getenv("PC_ZIP_FUZZ_ITERS");
    long iters = env ? atol(env) : 3000;
    uint8_t cz[256];
    uint32_t seed = 12345;
    for (int f = 0; f < 4; f++) {
        uint32_t len;
        uint8_t *base;
        if (f < 3) base = load(files[f], &len);
        else { len = craft(cz); base = malloc(len); memcpy(base, cz, len); }
        uint8_t *m = malloc(len + 16);
        for (long it = 0; it < iters; it++) {
            memcpy(m, base, len);
            uint32_t ml = len;
            int muts = 1 + (int)(rnd(&seed) % 4);
            for (int k = 0; k < muts; k++) {
                uint32_t at = rnd(&seed) % len;
                switch (rnd(&seed) % 4) {
                case 0: m[at] ^= (uint8_t)(1u << (rnd(&seed) % 8)); break;
                case 1: m[at] = (uint8_t)rnd(&seed); break;
                case 2: m[at] = rnd(&seed) & 1 ? 0xff : 0; if (at + 1 < len) m[at + 1] = m[at]; break;
                default: ml = at + 1 > 22 ? at + 1 : 22; if (ml > len) ml = len; break;
                }
            }
            fuzz_one(m, ml);
        }
        free(m);
        free(base);
    }
}

int main(void)
{
    RUN(test_lookup);
    RUN(test_stored);
    RUN(test_deflate);
    RUN(test_badcrc);
    RUN(test_too_big);
    RUN(test_gunzip);
    RUN(test_not_zip);
    RUN(test_real_muk);
    RUN(test_untrusted);
    RUN(test_fuzz);
    return 0;
}
