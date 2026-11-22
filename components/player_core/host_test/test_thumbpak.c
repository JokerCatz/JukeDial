// SPDX-License-Identifier: LGPL-2.1-or-later
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pc/pc_thumbpak.h"
#include "pc_test.h"

/* I/O seams (pc_thumbpak.c is built with PC_TPK_FOPEN / PC_TPK_FSYNC = these): fail the n-th call from now with the given errno */
static int fail_fopen_in, fail_fopen_err, fail_fsync_in, fail_rename_in;
FILE *test_fopen(const char *p, const char *m)
{
    if (fail_fopen_in > 0 && --fail_fopen_in == 0) { errno = fail_fopen_err; return NULL; }
    return fopen(p, m);
}
int test_rename(const char *a, const char *b)
{
    if (fail_rename_in > 0 && --fail_rename_in == 0) { errno = EIO; return -1; }
    return rename(a, b);
}
int test_fsync(int fd)
{
    if (fail_fsync_in > 0 && --fail_fsync_in == 0) { errno = EIO; return -1; }
    return fsync(fd);
}

static char P[64], Q[64];
static pc_tpk_ent_t mem[128];
static uint16_t px[8 * 8], got[8 * 8];
static void fill(uint16_t v) { for (int i = 0; i < 64; i++) px[i] = (uint16_t)(v + i); }
static long fsize(const char *p) { FILE *f = fopen(p, "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); return n; }
static long slurp(const char *p, uint8_t *b, long cap) { FILE *f = fopen(p, "rb"); long n = (long)fread(b, 1, (size_t)cap, f); fclose(f); return n; }
static pc_tpk_ent_t *alloc_mem(int cap, void *u) { (void)u; return cap <= 128 ? mem : NULL; }

static void test_tpk_roundtrip(void)
{
    pc_tpk_t p;
    CHECK_EQ_INT(pc_tpk_create(P, 4, 8, 7), 0);
    int cap = 0;
    CHECK_EQ_INT(pc_tpk_probe(P, &cap), 0);
    CHECK_EQ_INT(cap, 4);
    CHECK_EQ_INT(fsize(P), PC_TPK_HDR + 4 * PC_TPK_SLOT);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.n, 0); CHECK_EQ_INT(p.gen, 7);
    fill(200); CHECK_EQ_INT(pc_tpk_append(&p, 0x2222, 8, 8, px), 0);
    fill(100); CHECK_EQ_INT(pc_tpk_append(&p, 0x1111, 8, 8, px), 0);   /* the table stays sorted by key */
    CHECK(p.ent[0].key == 0x1111 && p.ent[1].key == 0x2222);
    CHECK_EQ_INT(pc_tpk_append(&p, 0x2222, 8, 8, px), -EEXIST);
    pc_tpk_close(&p);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.n, 2); CHECK_EQ_INT(p.next, 2);
    CHECK_EQ_INT(p.end, PC_TPK_HDR + 4 * PC_TPK_SLOT + 2 * 128);
    const pc_tpk_ent_t *e = pc_tpk_find(&p, 0x2222);
    CHECK(e && e->w == 8 && e->h == 8 && e->size == 128);
    CHECK_EQ_INT(pc_tpk_read(&p, e, got), 0);
    CHECK_EQ_INT(got[0], 200); CHECK_EQ_INT(got[63], 263);
    CHECK(pc_tpk_find(&p, 0x3333) == NULL);
    fill(300); CHECK_EQ_INT(pc_tpk_append(&p, 0x3333, 8, 8, px), 0);
    fill(400); CHECK_EQ_INT(pc_tpk_append(&p, 0x4444, 8, 8, px), 0);
    CHECK_EQ_INT(pc_tpk_append(&p, 0x5555, 8, 8, px), -ENOSPC);   /* the table is full: compaction's job */
    pc_tpk_close(&p);
}

/* a pulled card between the pixels and the slot: the pixels are a tail nobody names; the next append writes over them */
static void test_tpk_crash_before_slot(void)
{
    pc_tpk_t p;
    CHECK_EQ_INT(pc_tpk_create(P, 4, 8, 0), 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    fill(1); CHECK_EQ_INT(pc_tpk_append(&p, 0xA, 8, 8, px), 0);
    pc_tpk_close(&p);
    FILE *f = fopen(P, "ab"); for (int i = 0; i < 100; i++) fputc(0x5A, f); fclose(f);   /* half an entry's pixels, no slot */
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.n, 1);
    CHECK_EQ_INT(p.end, PC_TPK_HDR + 4 * PC_TPK_SLOT + 128);
    fill(7); CHECK_EQ_INT(pc_tpk_append(&p, 0xB, 8, 8, px), 0);
    pc_tpk_close(&p);
    CHECK_EQ_INT(fsize(P), PC_TPK_HDR + 4 * PC_TPK_SLOT + 2 * 128);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.n, 2);
    CHECK_EQ_INT(pc_tpk_read(&p, pc_tpk_find(&p, 0xB), got), 0);
    CHECK_EQ_INT(got[0], 7);
    pc_tpk_close(&p);
}

/* the order on the card: when the pixels' sync fails no slot is written (the pixels come first); when the slot's own sync fails the
 * in-memory table is re-read from the card (review M1), never left behind it */
static void test_tpk_sync_failures(void)
{
    pc_tpk_t p, q;
    CHECK_EQ_INT(pc_tpk_create(P, 4, 8, 0), 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    fail_fsync_in = 1;                                        /* the pixels' sync */
    fill(1); CHECK_EQ_INT(pc_tpk_append(&p, 0xA, 8, 8, px), -EIO);
    CHECK(pc_tpk_find(&p, 0xA) == NULL);
    static pc_tpk_ent_t m2[128];
    CHECK_EQ_INT(pc_tpk_open(&q, P, m2, 128), 0);
    CHECK_EQ_INT(q.n, 0);                                     /* no slot on the card */
    fail_fsync_in = 2;                                        /* the slot's sync (the slot itself is written) */
    fill(2); CHECK_EQ_INT(pc_tpk_append(&p, 0xB, 8, 8, px), -EIO);
    CHECK_EQ_INT(pc_tpk_open(&q, P, m2, 128), 0);
    CHECK_EQ_INT(p.n, q.n); CHECK_EQ_INT(p.next, q.next); CHECK_EQ_INT(p.end, q.end);   /* memory = card */
    CHECK(p.open);
    fill(3); CHECK_EQ_INT(pc_tpk_append(&p, 0xC, 8, 8, px), 0);
    CHECK_EQ_INT(pc_tpk_open(&q, P, m2, 128), 0);
    CHECK_EQ_INT(q.n, p.n);
    CHECK_EQ_INT(pc_tpk_read(&q, pc_tpk_find(&q, 0xC), got), 0);
    CHECK_EQ_INT(got[0], 3);
    CHECK(pc_tpk_find(&q, 0xB) != NULL);                     /* the fake fails the sync, the slot itself reached the file: the card has B */
    CHECK_EQ_INT(pc_tpk_read(&q, pc_tpk_find(&q, 0xB), got), 0);
    CHECK_EQ_INT(got[0], 2);                                  /* its own pixels, never another cover's */
    pc_tpk_close(&p); pc_tpk_close(&q);
}

/* a torn slot (its check does not match) is free; an entry whose pixels run past the end of the file (cut card write) is not valid */
static void test_tpk_torn_slot_and_cut_data(void)
{
    pc_tpk_t p;
    CHECK_EQ_INT(pc_tpk_create(P, 4, 8, 0), 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    fill(1); pc_tpk_append(&p, 0xA, 8, 8, px);
    fill(2); pc_tpk_append(&p, 0xB, 8, 8, px);
    pc_tpk_close(&p);
    FILE *f = fopen(P, "r+b"); fseek(f, PC_TPK_HDR + PC_TPK_SLOT + 8, SEEK_SET); fputc(0x77, f); fclose(f);   /* slot 1's offset byte */
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.n, 1); CHECK_EQ_INT(p.next, 1);
    CHECK(pc_tpk_find(&p, 0xB) == NULL);
    fill(3); CHECK_EQ_INT(pc_tpk_append(&p, 0xC, 8, 8, px), 0);   /* reuses slot 1 and the space after A */
    pc_tpk_close(&p);
    CHECK_EQ_INT(truncate(P, PC_TPK_HDR + 4 * PC_TPK_SLOT + 128 + 60), 0);   /* C's pixels cut */
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.n, 1);
    CHECK(pc_tpk_find(&p, 0xA) != NULL && pc_tpk_find(&p, 0xC) == NULL);
    pc_tpk_close(&p);
}

/* review I1 / M2: a pack is made new only when there is no file (stat ENOENT) or the file is not a pack after a full read (bad magic or
 * version, a cut table); an open that fails for another reason (EMFILE, an I/O error) leaves the file's bytes exactly as they were */
static void test_tpk_probe_and_attach(void)
{
    pc_tpk_t p;
    static uint8_t before[4096], after[4096];
    int cap = 0;
    unlink(P);
    CHECK_EQ_INT(pc_tpk_probe(P, &cap), -ENOENT);
    CHECK_EQ_INT(pc_tpk_attach(&p, P, alloc_mem, NULL, 4, 8, false), -ENOENT);   /* a read never makes a pack */
    CHECK(access(P, F_OK) != 0);
    CHECK_EQ_INT(pc_tpk_attach(&p, P, alloc_mem, NULL, 4, 8, true), 1);         /* made */
    fill(9); CHECK_EQ_INT(pc_tpk_append(&p, 0xA, 8, 8, px), 0);
    pc_tpk_close(&p);
    long n0 = slurp(P, before, sizeof before);
    fail_fopen_in = 1; fail_fopen_err = EMFILE;
    CHECK_EQ_INT(pc_tpk_probe(P, &cap), -EMFILE);
    fail_fopen_in = 1; fail_fopen_err = EMFILE;
    CHECK_EQ_INT(pc_tpk_attach(&p, P, alloc_mem, NULL, 4, 8, true), -EMFILE);   /* transient: not recreated */
    fail_fopen_in = 2; fail_fopen_err = EIO;                  /* the probe's open works, the table read's open fails */
    CHECK_EQ_INT(pc_tpk_attach(&p, P, alloc_mem, NULL, 4, 8, true), -EIO);
    CHECK(!p.open);
    long n1 = slurp(P, after, sizeof after);
    CHECK_EQ_INT(n1, n0);
    CHECK(!memcmp(before, after, (size_t)n0));                 /* untouched */
    CHECK_EQ_INT(pc_tpk_attach(&p, P, alloc_mem, NULL, 4, 8, true), 0);         /* and it is still the pack */
    CHECK(pc_tpk_find(&p, 0xA) != NULL);
    pc_tpk_close(&p);
    FILE *f = fopen(P, "wb"); fputs("not a pack at all, really not one", f); fclose(f);
    CHECK_EQ_INT(pc_tpk_probe(P, &cap), -EBADMSG);
    CHECK_EQ_INT(pc_tpk_attach(&p, P, alloc_mem, NULL, 4, 8, false), -EBADMSG);
    CHECK_EQ_INT(pc_tpk_attach(&p, P, alloc_mem, NULL, 4, 8, true), 1);         /* the firmware's own file: made new */
    pc_tpk_close(&p);
    f = fopen(P, "wb"); fputs("JTP", f); fclose(f);            /* shorter than a header */
    CHECK_EQ_INT(pc_tpk_probe(P, &cap), -EBADMSG);
    CHECK_EQ_INT(pc_tpk_create(P, 10, 8, 0), 0);
    CHECK_EQ_INT(truncate(P, PC_TPK_HDR + 5 * PC_TPK_SLOT), 0);  /* a cut table (a crash while it was made) */
    CHECK_EQ_INT(pc_tpk_probe(P, &cap), -EBADMSG);
    CHECK_EQ_INT(pc_tpk_create(P, 200, 8, 0), 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), -ENOMEM);         /* the caller sizes its memory from the probe */
    CHECK_EQ_INT(pc_tpk_attach(&p, P, alloc_mem, NULL, 4, 8, true), -ENOMEM);  /* no memory: not recreated either */
    CHECK_EQ_INT(pc_tpk_probe(P, &cap), 0);
    CHECK_EQ_INT(pc_tpk_create(P, 0, 8, 0), -EINVAL);
    CHECK_EQ_INT(pc_tpk_create(P, PC_TPK_CAP_MAX + 1, 8, 0), -EINVAL);
    CHECK_EQ_INT(pc_tpk_cap_for(0), 64);
    CHECK_EQ_INT(pc_tpk_cap_for(100), 214);
    CHECK_EQ_INT(pc_tpk_cap_for(1000000), PC_TPK_CAP_MAX);
    CHECK(pc_tpk_check(0, 0, 0, 0, 0) != 0);
}

/* review I3: the pack's bytes stay under PC_TPK_BYTES_MAX (1 GiB); the arithmetic is 32-bit and overflow-checked (values a 64-bit long would hide) */
static void test_tpk_bounds(void)
{
    uint32_t lim = PC_TPK_BYTES_MAX;
    CHECK(pc_tpk_span_ok(0, 100, 100));
    CHECK(!pc_tpk_span_ok(1, 100, 100));
    CHECK(!pc_tpk_span_ok(0xFFFFFF00u, 0x200u, 0xFFFFFFFFu));   /* a naive u32 sum wraps to 0x100 */
    CHECK(!pc_tpk_span_ok(0x200u, 0xFFFFFF00u, 0xFFFFFFFFu));
    CHECK(pc_tpk_span_ok(lim - 115200u, 115200u, lim));
    CHECK(!pc_tpk_span_ok(lim - 115199u, 115200u, lim));
    CHECK((uint64_t)PC_TPK_BYTES_MAX <= (uint64_t)INT32_MAX);   /* every offset fits a 32-bit long (fseek on the S31) */
    CHECK((uint64_t)PC_TPK_HDR + (uint64_t)PC_TPK_CAP_MAX * PC_TPK_SLOT < (uint64_t)PC_TPK_BYTES_MAX);
    pc_tpk_t p;
    CHECK_EQ_INT(pc_tpk_create(P, 4, 8, 0), 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    p.end = PC_TPK_BYTES_MAX - 100;                           /* pretend the pack is full to the cap */
    fill(1); CHECK_EQ_INT(pc_tpk_append(&p, 0xA, 8, 8, px), -EFBIG);
    CHECK_EQ_INT(p.n, 0);
    pc_tpk_close(&p);
    CHECK_EQ_INT(fsize(P), PC_TPK_HDR + 4 * PC_TPK_SLOT);       /* nothing written */
}

/* dedupe (controller ruling): identical embedded pictures share one entry; each track has an alias slot to it; a cover that cannot be made
 * is marked (alias to 0) so it is not decoded again */
static void test_tpk_shared(void)
{
    pc_tpk_t p;
    CHECK_EQ_INT(pc_tpk_create(P, 8, 8, 0), 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    fill(50);
    CHECK_EQ_INT(pc_tpk_add_shared(&p, 0x71, 0xC0, 8, 8, px), 0);   /* track 1: entry + alias */
    CHECK_EQ_INT(pc_tpk_alias(&p, 0x72, 0xC0), 0);                  /* track 2: the same picture: only an alias (no pixels written) */
    CHECK_EQ_INT(p.end, PC_TPK_HDR + 8 * PC_TPK_SLOT + 128);
    CHECK_EQ_INT(pc_tpk_add_shared(&p, 0x73, 0xC0, 8, 8, px), 0);   /* add_shared with the entry there: alias only */
    CHECK_EQ_INT(p.end, PC_TPK_HDR + 8 * PC_TPK_SLOT + 128);
    CHECK_EQ_INT(pc_tpk_alias(&p, 0x74, 0), 0);                     /* a bad cover */
    pc_tpk_close(&p);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.n, 5);
    const pc_tpk_ent_t *a = pc_tpk_lookup(&p, 0x71), *b = pc_tpk_lookup(&p, 0x72);
    CHECK(a && a == b && a->key == 0xC0);
    CHECK_EQ_INT(pc_tpk_read(&p, b, got), 0);
    CHECK_EQ_INT(got[0], 50);
    CHECK(pc_tpk_lookup(&p, 0xC0) == a);                           /* a data key is its own entry */
    CHECK(pc_tpk_lookup(&p, 0x74) == NULL && pc_tpk_is_bad(&p, 0x74) && !pc_tpk_is_bad(&p, 0x71) && !pc_tpk_is_bad(&p, 0x99));
    CHECK(pc_tpk_lookup(&p, 0x99) == NULL);
    CHECK(pc_tpk_is_alias(pc_tpk_find(&p, 0x72)) && pc_tpk_alias_target(pc_tpk_find(&p, 0x72)) == 0xC0);
    pc_tpk_close(&p);
}

/* fix round 2 (re-review I-3): a bad mark is forgotten (its slot zeroed on the card, then free), so `thumbcache build` retries the cover */
static void test_tpk_forget(void)
{
    pc_tpk_t p, q;
    static pc_tpk_ent_t m2[128];
    CHECK_EQ_INT(pc_tpk_create(P, 8, 8, 0), 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    fill(1); CHECK_EQ_INT(pc_tpk_append(&p, 0xA, 8, 8, px), 0);
    CHECK_EQ_INT(pc_tpk_alias(&p, 0xB, 0), 0);
    CHECK_EQ_INT(pc_tpk_alias(&p, 0xC, 0xA), 0);
    CHECK(pc_tpk_is_bad(&p, 0xB));
    CHECK_EQ_INT(pc_tpk_forget(&p, 0xB), 0);
    CHECK(!pc_tpk_is_bad(&p, 0xB) && pc_tpk_find(&p, 0xB) == NULL);
    CHECK_EQ_INT(pc_tpk_forget(&p, 0xB), -ENOENT);
    CHECK_EQ_INT(pc_tpk_open(&q, P, m2, 128), 0);              /* the card agrees */
    CHECK_EQ_INT(q.n, 2);
    CHECK(pc_tpk_find(&q, 0xB) == NULL && pc_tpk_lookup(&q, 0xC) != NULL);
    CHECK_EQ_INT(pc_tpk_alias(&p, 0xD, 0), 0);                 /* appends go on after the last valid slot */
    CHECK_EQ_INT(pc_tpk_open(&q, P, m2, 128), 0);
    CHECK(pc_tpk_is_bad(&q, 0xD) && pc_tpk_lookup(&q, 0xC) != NULL);
    pc_tpk_close(&p); pc_tpk_close(&q);
}

/* fix round 2 (re-review I-2): the boot-time order. Only .new (a compaction finished, the old pack unlinked): it becomes the pack; a failed
 * rename keeps .new for the next try (never an empty pack); both there with a valid pack: .new (an unfinished copy) goes; a pack that is
 * not a pack with a valid .new: .new replaces it; a .new that is not a pack goes */
static void test_tpk_recover(void)
{
    pc_tpk_t p;
    unlink(P); unlink(Q);
    CHECK_EQ_INT(pc_tpk_recover(P, Q), 0);                     /* nothing there: nothing to do */
    CHECK_EQ_INT(pc_tpk_create(Q, 8, 8, 3), 0);
    CHECK_EQ_INT(pc_tpk_open(&p, Q, mem, 128), 0);
    fill(5); CHECK_EQ_INT(pc_tpk_append(&p, 0xA, 8, 8, px), 0);
    pc_tpk_close(&p);
    fail_rename_in = 1;
    CHECK_EQ_INT(pc_tpk_recover(P, Q), -EIO);                  /* the rename fails once */
    CHECK(access(Q, F_OK) == 0 && access(P, F_OK) != 0);       /* the complete pack is still there, nothing was made */
    CHECK_EQ_INT(pc_tpk_recover(P, Q), 0);                     /* the next boot */
    CHECK(access(Q, F_OK) != 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.gen, 3);
    CHECK(pc_tpk_find(&p, 0xA) != NULL);
    pc_tpk_close(&p);
    CHECK_EQ_INT(pc_tpk_create(Q, 8, 8, 9), 0);                /* both there, the pack valid: an unfinished compaction */
    CHECK_EQ_INT(pc_tpk_recover(P, Q), 0);
    CHECK(access(Q, F_OK) != 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK(pc_tpk_find(&p, 0xA) != NULL);                       /* the old pack, intact */
    pc_tpk_close(&p);
    CHECK_EQ_INT(pc_tpk_create(Q, 8, 8, 9), 0);
    FILE *f = fopen(P, "wb"); fputs("garbage, not a pack at all here", f); fclose(f);
    CHECK_EQ_INT(pc_tpk_recover(P, Q), 0);                     /* the pack is not a pack: .new takes its place */
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    CHECK_EQ_INT(p.gen, 9);
    pc_tpk_close(&p);
    f = fopen(Q, "wb"); fputs("JTP", f); fclose(f);            /* a .new that is not a pack */
    CHECK_EQ_INT(pc_tpk_recover(P, Q), 0);
    CHECK(access(Q, F_OK) != 0);
    CHECK_EQ_INT(pc_tpk_open(&p, P, mem, 128), 0);
    pc_tpk_close(&p);
    CHECK_EQ_INT(pc_tpk_create(Q, 8, 8, 9), 0);
    fail_fopen_in = 1; fail_fopen_err = EMFILE;                /* the pack cannot be read now: nothing is touched */
    CHECK_EQ_INT(pc_tpk_recover(P, Q), -EMFILE);
    CHECK(access(Q, F_OK) == 0 && access(P, F_OK) == 0);
    fail_fopen_in = 2; fail_fopen_err = EIO;                  /* .new reads, the pack cannot be read now (re-review 2 m7): nothing touched */
    CHECK_EQ_INT(pc_tpk_recover(P, Q), -EIO);
    CHECK(access(Q, F_OK) == 0 && access(P, F_OK) == 0);
    unlink(Q);
}

/* compaction as the service does it: a new pack with the live entries (pixels copied, aliases re-made), then it replaces the old one */
static void test_tpk_compact(void)
{
    pc_tpk_t a, b;
    static pc_tpk_ent_t mem2[128];
    CHECK_EQ_INT(pc_tpk_create(P, 8, 8, 0), 0);
    CHECK_EQ_INT(pc_tpk_open(&a, P, mem, 128), 0);
    for (int k = 1; k <= 4; k++) { fill((uint16_t)(k * 10)); pc_tpk_append(&a, (uint64_t)k, 8, 8, px); }
    pc_tpk_alias(&a, 0x20, 4);
    CHECK_EQ_INT(pc_tpk_create(Q, pc_tpk_cap_for(3), 8, 5), 0);
    CHECK_EQ_INT(pc_tpk_open(&b, Q, mem2, 128), 0);
    for (int i = 0; i < a.n; i++) {
        const pc_tpk_ent_t *e = &a.ent[i];
        if (e->key % 2 && !pc_tpk_is_alias(e)) continue;         /* 2, 4 and the alias are live */
        if (pc_tpk_is_alias(e)) { CHECK_EQ_INT(pc_tpk_alias(&b, e->key, pc_tpk_alias_target(e)), 0); continue; }
        CHECK_EQ_INT(pc_tpk_read(&a, e, got), 0);
        CHECK_EQ_INT(pc_tpk_append(&b, e->key, e->w, e->h, got), 0);
    }
    pc_tpk_close(&a); pc_tpk_close(&b);
    CHECK_EQ_INT(rename(Q, P), 0);
    CHECK_EQ_INT(pc_tpk_open(&a, P, mem, 128), 0);
    CHECK_EQ_INT(a.n, 3); CHECK_EQ_INT(a.cap, 68); CHECK_EQ_INT(a.gen, 5);
    CHECK_EQ_INT(pc_tpk_read(&a, pc_tpk_lookup(&a, 0x20), got), 0);
    CHECK_EQ_INT(got[0], 40);
    pc_tpk_close(&a);
}

int main(void)
{
    snprintf(P, sizeof P, "/tmp/pc_tpk_%d.pak", (int)getpid());
    snprintf(Q, sizeof Q, "/tmp/pc_tpk_%d.tmp", (int)getpid());
    RUN(test_tpk_roundtrip);
    RUN(test_tpk_crash_before_slot);
    RUN(test_tpk_sync_failures);
    RUN(test_tpk_torn_slot_and_cut_data);
    RUN(test_tpk_probe_and_attach);
    RUN(test_tpk_bounds);
    RUN(test_tpk_shared);
    RUN(test_tpk_compact);
    RUN(test_tpk_forget);
    RUN(test_tpk_recover);
    unlink(P); unlink(Q);
    return 0;
}
