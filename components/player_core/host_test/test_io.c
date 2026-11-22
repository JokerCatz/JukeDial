// SPDX-License-Identifier: LGPL-2.1-or-later
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "../src/pc_io.c"      /* the static size function is reached by including the source */
#include "pc_test.h"

#define LEN 100000u
static uint8_t pat(uint32_t i) { return (uint8_t)(i * 31u + (i >> 8) * 7u); }

/* A pattern file in the temp directory, unlinked at once (the open FILE keeps it): a failed CHECK leaves nothing behind. */
static FILE *open_pattern(uint32_t len)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/pc_io_test_%d.bin", (int)getpid());
    FILE *w = fopen(path, "wb");
    CHECK(w != NULL);
    for (uint32_t i = 0; i < len; i++) fputc(pat(i), w);
    CHECK(fclose(w) == 0);
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    remove(path);
    return f;
}
static void check_bytes(const uint8_t *b, uint32_t off, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) CHECK_EQ_INT(b[i], pat(off + i));
}

static void test_io_sequential_and_random(void)
{
    FILE *f = open_pattern(LEN);
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    CHECK_EQ_INT(r.size, LEN);
    uint8_t b[4096];
    for (int i = 0; i < 10; i++) {
        CHECK_EQ_INT(r.read(r.u, (uint32_t)i * 4096, b, 4096), 4096);
        check_bytes(b, (uint32_t)i * 4096, 4096);
    }
    uint32_t s = 12345;
    for (int i = 0; i < 200; i++) {
        s = s * 1664525u + 1013904223u;
        uint32_t off = (s >> 8) % (LEN - 3000), n = 1 + (s >> 3) % 3000;
        CHECK_EQ_INT(r.read(r.u, off, b, n), n);
        check_bytes(b, off, n);
    }
    fclose(f);
}

static void test_io_eof(void)
{
    FILE *f = open_pattern(LEN);
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    uint8_t b[4096];
    CHECK_EQ_INT(r.read(r.u, LEN - 100, b, 4096), 100);
    check_bytes(b, LEN - 100, 100);
    CHECK_EQ_INT(r.read(r.u, LEN, b, 4096), 0);
    CHECK_EQ_INT(r.read(r.u, LEN + 1, b, 4096), 0);
    CHECK_EQ_INT(r.read(r.u, 0xFFFFFFFFu, b, 4096), 0);
    CHECK_EQ_INT(r.read(r.u, 5, b, 0), 0);
    CHECK_EQ_INT(r.read(r.u, 7, b, 16), 16);       /* and the reader still works afterwards */
    check_bytes(b, 7, 16);
    fclose(f);
}

/* Two by-value copies of one file reader share the descriptor: interleaved reads at different offsets stay right. */
static void test_io_copies_share_file(void)
{
    FILE *f = open_pattern(LEN);
    pc_reader_t r, a, b;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    a = r;
    b = r;
    uint8_t x[512];
    for (int i = 0; i < 40; i++) {
        uint32_t oa = (uint32_t)i * 512, ob = LEN - 512 - (uint32_t)i * 300;
        CHECK_EQ_INT(a.read(a.u, oa, x, 512), 512);
        check_bytes(x, oa, 512);
        CHECK_EQ_INT(b.read(b.u, ob, x, 512), 512);
        check_bytes(x, ob, 512);
        CHECK_EQ_INT(r.read(r.u, oa + 512, x, 100), 100);
        check_bytes(x, oa + 512, 100);
        CHECK_EQ_INT(a.read(a.u, oa + 512, x, 100), 100);
        check_bytes(x, oa + 512, 100);
    }
    fclose(f);
}

/* A read error is -1 (never a short count); afterwards the next read is right again. The descriptor is turned into a write-only one. */
static void test_io_fail_recovers(void)
{
    FILE *f = open_pattern(LEN);
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    int fd = fileno(f), keep = dup(fd), wo = open("/dev/null", O_WRONLY);
    CHECK(keep >= 0 && wo >= 0);
    uint8_t b[256];
    CHECK_EQ_INT(r.read(r.u, 0, b, 256), 256);
    CHECK(dup2(wo, fd) == fd);
    CHECK_EQ_INT(r.read(r.u, 5000, b, 256), -1);
    CHECK(dup2(keep, fd) == fd);
    CHECK_EQ_INT(r.read(r.u, 5000, b, 256), 256);
    check_bytes(b, 5000, 256);
    close(keep);
    close(wo);
    fclose(f);
}

static void test_io_size_limit(void)
{
    uint32_t v = 0;
    CHECK_EQ_INT(size_from_stat(2147483647LL, &v), 0);
    CHECK_EQ_INT(v, 2147483647LL);
    CHECK_EQ_INT(size_from_stat(2147483648LL, &v), -1);
    CHECK_EQ_INT(size_from_stat(-2147483648LL, &v), -1);   /* 2 GiB as a 32-bit off_t arrives negative */
    CHECK_EQ_INT(size_from_stat(-1, &v), -1);              /* 4294967295 as a 32-bit pattern */
    CHECK_EQ_INT(size_from_stat(4294967296LL, &v), -1);
    CHECK_EQ_INT(size_from_stat(0, &v), 0);
    CHECK_EQ_INT(v, 0);
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, NULL), -1);
}

/* Real sizes on a sparse file (host off_t is 64-bit): skipped where the file system refuses it. */
static void test_io_sparse_big(void)
{
    if (sizeof(off_t) < 8) { printf("skip test_io_sparse_big (32-bit off_t)\n"); return; }
    char p[64];
    snprintf(p, sizeof p, "/tmp/pc_io_sparse_%d.bin", (int)getpid());
    int fd = open(p, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd >= 0) remove(p);
    uint8_t mark[16];
    for (int i = 0; i < 16; i++) mark[i] = (uint8_t)(0xA0 + i);
    if (fd < 0 || ftruncate(fd, PC_FILE_MAX) != 0 || pwrite(fd, mark, 16, PC_FILE_MAX - 16) != 16) {
        printf("skip test_io_sparse_big (cannot make a sparse 2 GiB file here)\n");
        if (fd >= 0) close(fd);
        return;
    }
    FILE *f = fdopen(fd, "rb");
    CHECK(f != NULL);
    pc_reader_t r;
    CHECK_EQ_INT(pc_reader_file(&r, f), 0);
    CHECK_EQ_INT(r.size, PC_FILE_MAX);
    uint8_t b[32];
    CHECK_EQ_INT(r.read(r.u, PC_FILE_MAX - 16, b, 32), 16);   /* the last bytes, just below 2^31 */
    CHECK(memcmp(b, mark, 16) == 0);
    CHECK_EQ_INT(r.read(r.u, PC_FILE_MAX, b, 32), 0);
    CHECK(ftruncate(fd, (off_t)PC_FILE_MAX + 1) == 0);        /* one byte more: refused */
    CHECK_EQ_INT(pc_reader_file(&r, f), -1);
    fclose(f);
}

int main(void)
{
    RUN(test_io_sequential_and_random);
    RUN(test_io_eof);
    RUN(test_io_copies_share_file);
    RUN(test_io_fail_recovers);
    RUN(test_io_size_limit);
    RUN(test_io_sparse_big);
    return 0;
}
