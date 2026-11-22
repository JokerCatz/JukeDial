// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The cover thumbnail pack (hires T8c, one compact file, not a file per cover): /sdcard/_player/index/thumbs.pak.
 * Layout (little endian): a 32-byte header ("JTPK", version u16 = 1, side u16, cap u32, gen u32, zeros), then cap 32-byte slots, then
 * the thumbnails' raw RGB565 appended. Slot: key u64, off u32, size u32, w u16, h u16, check u32 (pc_tpk_check), 8 zero bytes.
 * A data slot (w, h > 0) names size = w * h * 2 bytes at off, inside the file. An alias slot (w = h = 0) says "key shows the entry
 * target" with target = off << 32 | size (fix round 1: tracks whose embedded pictures are identical share one data entry); target 0 =
 * this cover cannot be made (not decoded again). A zero / torn / out-of-file slot is empty.
 * Append-only: an entry's pixels are written at the end of the valid data and synced first, its slot last (synced): a crash or a pulled
 * card at any point leaves a valid pack (the half-written tail is overwritten by the next append). A failed write re-reads the table, so
 * memory never runs ahead of or behind the card. A read is one seek + one read; the file is opened per operation.
 * Bound: the file stays under PC_TPK_BYTES_MAX (1 GiB: every offset fits a 32-bit long; all arithmetic is u32 with overflow checks).
 * gen: the library generation of the last compaction (the service compacts at most once per library). */
#define PC_TPK_MAGIC "JTPK"
#define PC_TPK_VERSION 1
#define PC_TPK_HDR 32
#define PC_TPK_SLOT 32
#define PC_TPK_CAP_MAX 16384
#define PC_TPK_BYTES_MAX (1u << 30)
typedef struct { uint64_t key; uint32_t off, size; uint16_t w, h, slot; } pc_tpk_ent_t;   /* slot: its index in the table (forget) */
typedef struct {
    char path[64];          /* the file is opened per read / append and closed again (no handle, no stdio buffer held between) */
    bool open;
    pc_tpk_ent_t *ent;      /* the valid entries, sorted by key (caller's memory, mem_cap of them) */
    int cap, n, next, mem_cap;   /* table slots, valid entries, the next free slot (after the last valid one) */
    uint32_t end, gen;      /* end: where the next entry's pixels go (the end of the valid data) */
    uint16_t side;
    void (*yield)(void);    /* called between 16 KB pieces of a write (NULL: none) */
} pc_tpk_t;
uint32_t pc_tpk_check(uint64_t key, uint32_t off, uint32_t size, uint16_t w, uint16_t h);
bool pc_tpk_span_ok(uint32_t off, uint32_t size, uint32_t limit);   /* off + size <= limit, without u32 overflow */
int pc_tpk_cap_for(int entries);                        /* a table with room to grow: entries * 1.5 + 64, <= PC_TPK_CAP_MAX */
int pc_tpk_create(const char *path, int cap, uint16_t side, uint32_t gen);   /* an empty pack (overwrites path); 0 or -errno */
/* what is at path: 0 = a pack (*cap = its table size); -ENOENT = no file (stat says so); -EBADMSG = a file that is not a pack after a
 * full read (short, bad magic / version / cap, or a cut table); any other -errno = it could not be read now (a transient error: never
 * a reason to replace the file) */
int pc_tpk_probe(const char *path, int *cap);
/* reads the header and the table into mem; 0, or -errno (-EBADMSG: not a pack or a cut table, -ENOMEM: mem_cap < cap) */
int pc_tpk_open(pc_tpk_t *p, const char *path, pc_tpk_ent_t *mem, int mem_cap);
/* probe + open; the pack is (re)made with new_cap only when create and the probe said -ENOENT or -EBADMSG; alloc gives memory for cap
 * entries (NULL: -ENOMEM). 1 = made new, 0 = opened, -errno = not open (the file untouched) */
typedef pc_tpk_ent_t *(*pc_tpk_alloc_fn)(int cap, void *u);
int pc_tpk_attach(pc_tpk_t *p, const char *path, pc_tpk_alloc_fn alloc, void *u, int new_cap, uint16_t side, bool create);
const pc_tpk_ent_t *pc_tpk_find(const pc_tpk_t *p, uint64_t key);      /* the slot of key as stored (data or alias) */
const pc_tpk_ent_t *pc_tpk_lookup(const pc_tpk_t *p, uint64_t key);    /* the data entry key shows (through one alias); NULL: none or bad */
static inline bool pc_tpk_is_alias(const pc_tpk_ent_t *e) { return e && !e->w && !e->h; }
static inline uint64_t pc_tpk_alias_target(const pc_tpk_ent_t *e) { return (uint64_t)e->off << 32 | e->size; }
bool pc_tpk_is_bad(const pc_tpk_t *p, uint64_t key);    /* an alias to 0: this cover was tried and cannot be made */
int pc_tpk_read(pc_tpk_t *p, const pc_tpk_ent_t *e, void *dst);   /* a data entry's e->size bytes; 0 or -EIO */
/* the pixels (w * h * 2 bytes) then the slot; 0, -EEXIST (the key is there), -ENOSPC (the table is full: compact), -EFBIG (the pack
 * would pass PC_TPK_BYTES_MAX), -EIO (a write failed: errno says why, e.g. ENOSPC for a full card; the table was re-read) */
int pc_tpk_append(pc_tpk_t *p, uint64_t key, uint16_t w, uint16_t h, const void *px);
int pc_tpk_alias(pc_tpk_t *p, uint64_t key, uint64_t target);       /* an alias slot only (32 bytes); same errors */
int pc_tpk_add_shared(pc_tpk_t *p, uint64_t key, uint64_t content, uint16_t w, uint16_t h, const void *px);   /* content's entry if missing, then key -> content */
/* fix round 2: a slot zeroed on the card (synced) and dropped from the table - used for bad marks only (`thumbcache build` retries them);
 * 0, -ENOENT (no such key), -EIO (the table was re-read) */
int pc_tpk_forget(pc_tpk_t *p, uint64_t key);
/* fix round 2 (re-review I-2), the boot-time order, before any attach (the compaction leaves neu, then unlinks pak, then renames neu -> pak):
 *  no neu: nothing to do; pak a valid pack: neu is an unfinished copy and goes; pak missing or not a pack (after a full read) and neu a
 *  valid pack: neu becomes pak (a failed rename returns -errno and keeps neu for the next try: never an empty pack in its place); neu not
 *  a pack: it goes. Anything that cannot be read now (EMFILE, I/O): -errno, nothing touched. 0 = pak (if any) is the pack to attach. */
int pc_tpk_recover(const char *pak, const char *neu);
void pc_tpk_close(pc_tpk_t *p);   /* forgets the table (no file is open between calls) */
#ifdef __cplusplus
}
#endif
