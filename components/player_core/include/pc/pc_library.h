// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pc/pc_rg.h"
#ifdef __cplusplus
extern "C" {
#endif

/* MP3 library: a tree under <export_root> (sub-folders to depth PC_LIB_DEPTH, songs at any level, the music root's own songs too).
 * Index file /_player/index/library.bin: "JCLB", version, counts, tracks, folders, string pool, "BLCJ".
 * Order: tracks and folders are stored depth first. A folder's own songs come first, then the songs of its sub-folders in name order, so the songs of a
 * folder including its whole sub-tree are ONE contiguous range [first, first + sub_count) and its own songs the head of it [first, first + count).
 * Folders are in pre-order: the sub-tree of folder f is the folders [f, f + nsub); its children are f + 1, then every c += folders[c].nsub.
 * The music root is not a folder entry: its own songs are tracks [0, root_own), its children start at folder 0 (the same hop) and number root_nchild. */
#define PC_LIB_DEPTH 8                    /* sub-folder levels below the music root that the scan descends */
#define PC_NO_FOLDER 0xFFFFu              /* pc_track_t.folder of a song directly in the music root */
typedef struct {
    uint32_t path, title, artist, album;   /* offsets into the string pool */
    uint32_t date;                         /* yyyymmdd, 0 unknown */
    uint16_t track, folder;
    uint32_t apic_off, apic_len;
    pc_rg_t rg;                            /* ReplayGain from the tags (index version 6) */
} pc_track_t;

typedef struct {
    uint32_t name, path;                   /* offsets into the string pool: the folder's own name, its full path */
    int32_t first, count;                  /* own songs: tracks [first, first + count) */
    int32_t sub_count;                     /* songs of the whole sub-tree (>= count): tracks [first, first + sub_count) */
    int32_t parent;                        /* folder index, -1 = child of the music root */
    int32_t nsub;                          /* folders in the sub-tree including this one (>= 1) */
    int32_t nchild;                        /* direct sub-folders */
} pc_folder_t;

typedef struct {
    pc_track_t *tracks; int n;
    pc_folder_t *folders; int nfolders;
    char *pool; uint32_t pool_len;
    int root_own, root_nchild;             /* the music root's own songs (tracks [0, root_own)) and its direct sub-folders */
    uint32_t newest_date;
    uint32_t skipped_big;                  /* audio files above PC_FILE_MAX (2 GiB - 1) that the last scan left out; 0 for a loaded index */
} pc_library_t;

typedef void (*pc_scan_cb)(int done, void *u);   /* every 16 files */

/* memory for the scan's name lists and the weights of a shuffle scope (big blocks): the board points this at PSRAM (heap_caps SPIRAM), the default is realloc / free.
 * pc_bigmem_realloc(NULL, n) allocates; (p, 0) is not used, free with pc_bigmem_free. */
void  pc_bigmem_set(void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *));
void *pc_bigmem_realloc(void *p, size_t n);
void  pc_bigmem_free(void *p);

int  pc_library_scan(const char *export_root, pc_library_t *lib, pc_scan_cb cb, void *u);  /* 0, or -errno (lib left empty) */
int  pc_library_save(const pc_library_t *lib, const char *path);   /* tmp + rename */
int  pc_library_load(pc_library_t *lib, const char *path);         /* -ENOENT, -EINVAL (bad/old/short), -ENOMEM */
void pc_library_free(pc_library_t *lib);
static inline const char *pc_str(const pc_library_t *l, uint32_t off) { return l->pool + off; }

/* file names the library lists: .mp3 .flac .ogg .oga .opus .m4a .wav, the tracker modules of pc_xmp_ext (.mod .xm .s3m .it .stm .okt .med ...) and the other chip / OPL / stream suffixes (any case) */
bool pc_library_is_audio(const char *name);
/* hires T8e (a rescan keeps playing): the track at exactly this path, the folder at exactly this path; -1 none */
int pc_library_find(const pc_library_t *lib, const char *path);
int pc_library_find_folder(const pc_library_t *lib, const char *path);

/* the tree. Folder numbers: -1 = the music root wherever a folder is taken (the whole library as a scope). */
static inline int pc_track_folder(const pc_library_t *l, int track) { const unsigned f = l->tracks[track].folder; return f == PC_NO_FOLDER || f >= (unsigned)l->nfolders ? -1 : (int)f; }
/* the songs of folder f including its sub-folders (f < 0 or out of range: all songs) as the range [*first, *first + *count) */
void pc_library_scope(const pc_library_t *lib, int f, int *first, int *count);
/* one level: the own songs [*first, *first + *count) and the number of sub-folders (f < 0: the music root) */
void pc_library_level(const pc_library_t *lib, int f, int *first, int *count, int *nchild);
/* the first sub-folder of f (-1: none), and the next sibling of sub-folder c (-1: c was the last one under `parent`; the parent -1 = the music root) */
int pc_library_first_child(const pc_library_t *lib, int f);
int pc_library_next_sibling(const pc_library_t *lib, int c, int parent);
/* the k-th sub-folder of f (0-based; -1 when there is none); a walk of k hops */
int pc_library_child(const pc_library_t *lib, int f, int k);
/* is folder g inside the sub-tree of f (f == g counts; f < 0 = everything) */
static inline bool pc_library_folder_in(const pc_library_t *lib, int f, int g) { return f < 0 || (g >= f && f < lib->nfolders && g < f + lib->folders[f].nsub); }
/* fix round 1 (review M2): the same lookup through an index - idx[lib->n] (the caller's memory) gets the track numbers in path order
 * (strcmp; a heap sort, no allocation), then pc_library_find_sorted is a binary search */
void pc_library_path_order(const pc_library_t *lib, int *idx);
int pc_library_find_sorted(const pc_library_t *lib, const int *idx, const char *path);
#ifdef __cplusplus
}
#endif
