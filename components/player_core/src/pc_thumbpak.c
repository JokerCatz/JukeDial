// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_thumbpak.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* I/O seams: the host test fails an open or a sync on purpose (fix round 1); the firmware uses the C library's */
#ifdef PC_TPK_FOPEN
FILE *PC_TPK_FOPEN(const char *path, const char *mode);
int PC_TPK_FSYNC(int fd);
int PC_TPK_RENAME(const char *from, const char *to);
#else
#define PC_TPK_FOPEN fopen
#define PC_TPK_FSYNC fsync
#define PC_TPK_RENAME rename
#endif

_Static_assert(PC_TPK_BYTES_MAX <= 0x7FFFFFFFu, "every pack offset must fit a 32-bit long (fseek)");
_Static_assert(PC_TPK_HDR + (uint64_t)PC_TPK_CAP_MAX * PC_TPK_SLOT < PC_TPK_BYTES_MAX, "the table fits the bound");

static void put16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *b, uint32_t v) { put16(b, (uint16_t)v); put16(b + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static uint32_t get32(const uint8_t *b) { return get16(b) | (uint32_t)get16(b + 2) << 16; }

uint32_t pc_tpk_check(uint64_t key, uint32_t off, uint32_t size, uint16_t w, uint16_t h)
{
    uint32_t v[5] = { (uint32_t)key, (uint32_t)(key >> 32), off, size, (uint32_t)w << 16 | h }, x = 0x811C9DC5u;
    for (int i = 0; i < 5; i++) x = (x ^ v[i]) * 16777619u;
    return x ? x : 1;                              /* never 0: an all-zero slot is never valid */
}

bool pc_tpk_span_ok(uint32_t off, uint32_t size, uint32_t limit) { return size <= limit && off <= limit - size; }

int pc_tpk_cap_for(int n)
{
    long c = (long)(n < 0 ? 0 : n) * 3 / 2 + 64;
    return c > PC_TPK_CAP_MAX ? PC_TPK_CAP_MAX : (int)c;
}

static uint32_t data_start(int cap) { return PC_TPK_HDR + (uint32_t)cap * PC_TPK_SLOT; }

static int sync_file(FILE *f) { return fflush(f) == 0 && PC_TPK_FSYNC(fileno(f)) == 0 ? 0 : -EIO; }

int pc_tpk_create(const char *path, int cap, uint16_t side, uint32_t gen)
{
    if (cap <= 0 || cap > PC_TPK_CAP_MAX || !side) return -EINVAL;
    FILE *f = PC_TPK_FOPEN(path, "wb");
    if (!f) return errno ? -errno : -EIO;
    uint8_t h[PC_TPK_HDR] = { 0 }, z[PC_TPK_SLOT] = { 0 };
    memcpy(h, PC_TPK_MAGIC, 4);
    put16(h + 4, PC_TPK_VERSION);
    put16(h + 6, side);
    put32(h + 8, (uint32_t)cap);
    put32(h + 12, gen);
    bool ok = fwrite(h, 1, sizeof h, f) == sizeof h;
    for (int i = 0; ok && i < cap; i++) ok = fwrite(z, 1, sizeof z, f) == sizeof z;
    int err = ok ? 0 : (errno ? -errno : -EIO);
    if (ok && sync_file(f)) err = -EIO;
    if (fclose(f) && !err) err = -EIO;
    return err;
}

/* the header of an open file of fsz bytes: 0, -EBADMSG (not a pack, after a full read), -EIO (the read failed) */
static int read_hdr(FILE *f, uint32_t fsz, int *cap, uint16_t *side, uint32_t *gen)
{
    uint8_t h[PC_TPK_HDR];
    if (fsz < PC_TPK_HDR) return -EBADMSG;
    if (fseek(f, 0, SEEK_SET) || fread(h, 1, sizeof h, f) != sizeof h) return -EIO;
    uint32_t c = get32(h + 8);
    if (memcmp(h, PC_TPK_MAGIC, 4) || get16(h + 4) != PC_TPK_VERSION || !c || c > PC_TPK_CAP_MAX || !get16(h + 6)) return -EBADMSG;
    if (fsz < data_start((int)c)) return -EBADMSG;            /* a cut table: a crash while it was made */
    *cap = (int)c;
    *side = get16(h + 6);
    *gen = get32(h + 12);
    return 0;
}

/* the file's size from the directory (stat): 0 or -errno; sizes past the bound are clamped (entries beyond it are not valid) */
static int file_size(const char *path, uint32_t *fsz)
{
    struct stat st;
    if (stat(path, &st)) return errno == ENOENT ? -ENOENT : (errno ? -errno : -EIO);
    *fsz = st.st_size < 0 ? 0 : (uint64_t)st.st_size > PC_TPK_BYTES_MAX ? PC_TPK_BYTES_MAX : (uint32_t)st.st_size;
    return 0;
}

int pc_tpk_probe(const char *path, int *cap)
{
    uint32_t fsz, gen;
    int r = file_size(path, &fsz);
    if (r) return r;
    errno = 0;
    FILE *f = PC_TPK_FOPEN(path, "rb");
    if (!f) return errno ? -errno : -EIO;                     /* EMFILE, EIO...: transient, never "not a pack" */
    uint16_t side;
    r = read_hdr(f, fsz, cap, &side, &gen);
    fclose(f);
    return r;
}

static int cmp_ent(const void *a, const void *b)
{
    uint64_t x = ((const pc_tpk_ent_t *)a)->key, y = ((const pc_tpk_ent_t *)b)->key;
    return x < y ? -1 : x > y;
}

/* the table from the card into p (p->path, p->ent, p->mem_cap set): 0 or -errno */
static int load(pc_tpk_t *p)
{
    uint32_t fsz;
    int r = file_size(p->path, &fsz);
    if (r) return r;
    errno = 0;
    FILE *f = PC_TPK_FOPEN(p->path, "rb");
    if (!f) return errno ? -errno : -EIO;
    int cap;
    uint16_t side;
    uint32_t gen;
    r = read_hdr(f, fsz, &cap, &side, &gen);
    if (!r && cap > p->mem_cap) r = -ENOMEM;
    if (r) { fclose(f); return r; }
    p->cap = cap; p->side = side; p->gen = gen; p->end = data_start(cap); p->n = p->next = 0;
    for (int i = 0; i < cap && !r; i++) {
        uint8_t s[PC_TPK_SLOT];
        if (fread(s, 1, sizeof s, f) != sizeof s) { r = -EIO; break; }
        pc_tpk_ent_t e = { .key = get32(s) | (uint64_t)get32(s + 4) << 32, .off = get32(s + 8), .size = get32(s + 12), .w = get16(s + 16), .h = get16(s + 18),
                           .slot = (uint16_t)i };
        if (get32(s + 20) != pc_tpk_check(e.key, e.off, e.size, e.w, e.h)) continue;   /* empty or torn: the slot is free */
        bool alias = !e.w && !e.h;
        bool ok = alias || (e.w && e.h && e.size == (uint32_t)e.w * e.h * 2 && e.off >= data_start(cap) && pc_tpk_span_ok(e.off, e.size, fsz));
        if (!ok) continue;
        p->ent[p->n++] = e;
        p->next = i + 1;
        if (!alias && e.off + e.size > p->end) p->end = e.off + e.size;
    }
    fclose(f);
    if (r) return r;
    qsort(p->ent, (size_t)p->n, sizeof *p->ent, cmp_ent);
    return 0;
}

int pc_tpk_open(pc_tpk_t *p, const char *path, pc_tpk_ent_t *mem, int mem_cap)
{
    memset(p, 0, sizeof *p);
    if (strlen(path) >= sizeof p->path) return -ENAMETOOLONG;
    strcpy(p->path, path);
    p->ent = mem;
    p->mem_cap = mem_cap;
    int r = load(p);
    p->open = r == 0;
    return r;
}

int pc_tpk_attach(pc_tpk_t *p, const char *path, pc_tpk_alloc_fn alloc, void *u, int new_cap, uint16_t side, bool create)
{
    memset(p, 0, sizeof *p);
    int cap = 0, r = pc_tpk_probe(path, &cap), made = 0;
    if (r == -ENOENT || r == -EBADMSG) {
        if (!create) return r;
        cap = new_cap < 1 ? 1 : new_cap > PC_TPK_CAP_MAX ? PC_TPK_CAP_MAX : new_cap;
        pc_tpk_ent_t *m = alloc(cap, u);                     /* memory first: no memory = the file stays as it is */
        if (!m) return -ENOMEM;
        if ((r = pc_tpk_create(path, cap, side, 0))) return r;
        made = 1;
        r = pc_tpk_open(p, path, m, cap);
        return r ? r : made;
    }
    if (r) return r;                                          /* transient: the file stays as it is */
    pc_tpk_ent_t *m = alloc(cap, u);
    if (!m) return -ENOMEM;
    r = pc_tpk_open(p, path, m, cap);
    return r;
}

const pc_tpk_ent_t *pc_tpk_find(const pc_tpk_t *p, uint64_t key)
{
    if (!p->open || !p->n) return NULL;                       /* p is never NULL (callers own it) */
    pc_tpk_ent_t k = { .key = key };
    return bsearch(&k, p->ent, (size_t)p->n, sizeof *p->ent, cmp_ent);
}

const pc_tpk_ent_t *pc_tpk_lookup(const pc_tpk_t *p, uint64_t key)
{
    const pc_tpk_ent_t *e = pc_tpk_find(p, key);
    if (pc_tpk_is_alias(e)) {
        uint64_t t = pc_tpk_alias_target(e);
        e = t ? pc_tpk_find(p, t) : NULL;
        if (pc_tpk_is_alias(e)) e = NULL;                    /* one level only */
    }
    return e;
}

bool pc_tpk_is_bad(const pc_tpk_t *p, uint64_t key)
{
    const pc_tpk_ent_t *e = pc_tpk_find(p, key);
    return pc_tpk_is_alias(e) && !pc_tpk_alias_target(e);
}

int pc_tpk_read(pc_tpk_t *p, const pc_tpk_ent_t *e, void *dst)
{
    FILE *f = p->open && e && !pc_tpk_is_alias(e) ? PC_TPK_FOPEN(p->path, "rb") : NULL;
    if (!f) return -EIO;
    bool ok = fseek(f, (long)e->off, SEEK_SET) == 0 && fread(dst, 1, e->size, f) == e->size;
    fclose(f);
    return ok ? 0 : -EIO;
}

static void insert(pc_tpk_t *p, pc_tpk_ent_t e)
{
    int i = p->n;
    while (i > 0 && p->ent[i - 1].key > e.key) i--;
    memmove(&p->ent[i + 1], &p->ent[i], sizeof *p->ent * (size_t)(p->n - i));
    p->ent[i] = e;
    p->n++;
}

/* pixels (px != NULL: data slot at p->end) then the slot; the table follows the card */
static int put(pc_tpk_t *p, pc_tpk_ent_t e, const void *px)
{
    if (!p->open) return -EIO;
    if (pc_tpk_find(p, e.key)) return -EEXIST;
    if (p->next >= p->cap || p->n >= p->mem_cap) return -ENOSPC;
    if (px) {
        e.off = p->end;
        if (!pc_tpk_span_ok(e.off, e.size, PC_TPK_BYTES_MAX)) return -EFBIG;
    }
    errno = 0;
    FILE *f = PC_TPK_FOPEN(p->path, "r+b");
    bool ok = f != NULL, slot_tried = false;
    if (ok && px) {
        ok = fseek(f, (long)e.off, SEEK_SET) == 0;
        for (uint32_t o = 0; ok && o < e.size; o += 16384) {    /* in pieces: the audio reads the card too */
            size_t n = e.size - o < 16384 ? e.size - o : 16384;
            ok = fwrite((const uint8_t *)px + o, 1, n, f) == n;
            if (p->yield) p->yield();
        }
        ok = ok && sync_file(f) == 0;                         /* the pixels are on the card before the slot names them */
    }
    if (ok) {
        uint8_t s[PC_TPK_SLOT] = { 0 };
        put32(s, (uint32_t)e.key); put32(s + 4, (uint32_t)(e.key >> 32)); put32(s + 8, e.off); put32(s + 12, e.size);
        put16(s + 16, e.w); put16(s + 18, e.h); put32(s + 20, pc_tpk_check(e.key, e.off, e.size, e.w, e.h));
        slot_tried = true;
        ok = fseek(f, (long)(PC_TPK_HDR + (uint32_t)p->next * PC_TPK_SLOT), SEEK_SET) == 0 && fwrite(s, 1, sizeof s, f) == sizeof s
             && sync_file(f) == 0;
    }
    int err = errno;
    if (f && fclose(f)) ok = false;
    if (ok) {
        e.slot = (uint16_t)p->next;
        insert(p, e);
        p->next++;
        if (px) p->end = e.off + e.size;
        return 0;
    }
    if (slot_tried && load(p)) p->open = false;               /* the slot may be on the card: memory follows the card (review M1) */
    errno = err;
    return -EIO;                                              /* errno is the cause; the pack on the card is still valid */
}

int pc_tpk_append(pc_tpk_t *p, uint64_t key, uint16_t w, uint16_t h, const void *px)
{
    if (!w || !h || !px) return -EINVAL;
    return put(p, (pc_tpk_ent_t){ .key = key, .size = (uint32_t)w * h * 2, .w = w, .h = h }, px);
}

int pc_tpk_alias(pc_tpk_t *p, uint64_t key, uint64_t target)
{
    return put(p, (pc_tpk_ent_t){ .key = key, .off = (uint32_t)(target >> 32), .size = (uint32_t)target }, NULL);
}

int pc_tpk_add_shared(pc_tpk_t *p, uint64_t key, uint64_t content, uint16_t w, uint16_t h, const void *px)
{
    const pc_tpk_ent_t *c = pc_tpk_find(p, content);
    if (!c) {
        int r = pc_tpk_append(p, content, w, h, px);
        if (r) return r;
    } else if (pc_tpk_is_alias(c)) return -EINVAL;
    return pc_tpk_alias(p, key, content);
}

int pc_tpk_forget(pc_tpk_t *p, uint64_t key)
{
    const pc_tpk_ent_t *e = pc_tpk_find(p, key);
    if (!e) return -ENOENT;
    int i = (int)(e - p->ent);
    uint8_t z[PC_TPK_SLOT] = { 0 };
    errno = 0;
    FILE *f = PC_TPK_FOPEN(p->path, "r+b");
    bool ok = f && fseek(f, (long)(PC_TPK_HDR + (uint32_t)e->slot * PC_TPK_SLOT), SEEK_SET) == 0 && fwrite(z, 1, sizeof z, f) == sizeof z && sync_file(f) == 0;
    int err = errno;
    if (f && fclose(f)) ok = false;
    if (!ok) { if (load(p)) p->open = false; errno = err; return -EIO; }
    memmove(&p->ent[i], &p->ent[i + 1], sizeof *p->ent * (size_t)(p->n - i - 1));
    p->n--;
    return 0;                                                 /* next stays: appends go on after the last slot ever used */
}

int pc_tpk_recover(const char *pak, const char *neu)
{
    int cap, rn = pc_tpk_probe(neu, &cap);
    if (rn == -ENOENT) return 0;                              /* no compaction left anything */
    if (rn != 0 && rn != -EBADMSG) return rn;                 /* cannot be read now: touch nothing */
    int rp = pc_tpk_probe(pak, &cap);
    if (rp != 0 && rp != -ENOENT && rp != -EBADMSG) return rp;
    if (rp == 0 || rn == -EBADMSG) {                          /* a valid pack stays (.new was an unfinished copy); a .new that is not a pack goes */
        unlink(neu);
        return 0;
    }
    if (rp == -EBADMSG && unlink(pak)) return errno ? -errno : -EIO;   /* the firmware's own file that is not a pack */
    if (PC_TPK_RENAME(neu, pak)) return errno ? -errno : -EIO;        /* kept for the next try */
    return 0;
}

void pc_tpk_close(pc_tpk_t *p)
{
    p->open = false;
    p->n = p->next = 0;
}
