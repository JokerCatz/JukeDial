// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_library.h"
#include "pc/pc_paths.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "pc/pc_chipseq.h"
#include "pc/pc_id3.h"
#include "pc/pc_io.h"
#include "pc/pc_utf.h"

#define MAGIC "JCLB"
#define TAIL "BLCJ"
#define VERSION 7u   /* 2: FLAC, 3: Ogg Vorbis / Opus, 4: M4A / WAV, 5: MOD / XM / S3M / IT, 6: ReplayGain, 7: folder tree (an older index -> rescan) */

static bool ends_with(const char *n, const char *ext)
{
    size_t l = strlen(n), e = strlen(ext);
    return l > e && !strcasecmp(n + l - e, ext);
}
bool pc_library_is_audio(const char *name)
{
    return ends_with(name, ".mp3") || ends_with(name, ".flac") || ends_with(name, ".ogg") || ends_with(name, ".oga") || ends_with(name, ".opus") ||
           ends_with(name, ".m4a") || ends_with(name, ".wav") ||
           ends_with(name, ".aif") || ends_with(name, ".aiff") || ends_with(name, ".aifc") || ends_with(name, ".mpc") || ends_with(name, ".tta") || ends_with(name, ".ahx") || ends_with(name, ".hvl") || ends_with(name, ".p8") || ends_with(name, ".p8.png") || ends_with(name, ".ym") || ends_with(name, ".sid") || pc_opl_ext(strrchr(name, '.')) || ends_with(name, ".adx") || ends_with(name, ".vag") || ends_with(name, ".brstm") ||
           (strrchr(name, '.') > name && pc_xmp_ext(strrchr(name, '.')));
}

/* ---- memory for the big blocks (name lists, weights): PSRAM on the board, set by the app; realloc / free elsewhere ---- */
static void *(*s_big_realloc)(void *, size_t);
static void (*s_big_free)(void *);
void pc_bigmem_set(void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *)) { s_big_realloc = realloc_fn; s_big_free = free_fn; }
void *pc_bigmem_realloc(void *p, size_t n) { return s_big_realloc ? s_big_realloc(p, n) : realloc(p, n); }
void pc_bigmem_free(void *p) { if (s_big_free) s_big_free(p); else free(p); }

/* the names of one directory: ONE growing text block and an offset table, not one allocation per name (5,000 files in a folder must not
 * put 5,000 small blocks into internal RAM). Both start at or above the 1 KiB where the target's allocator leaves internal RAM. */
typedef struct { char *blob; uint32_t *off; int n, cap; size_t len, bcap; } names_t;
typedef struct { names_t dirs, files; uint32_t big; } listing_t;   /* big: files left out for their size */

static int names_add(names_t *s, const char *name)
{
    const size_t l = strlen(name) + 1;
    if (s->n == s->cap) {
        int cap = s->cap ? s->cap * 2 : 256;
        uint32_t *o = pc_bigmem_realloc(s->off, (size_t)cap * sizeof *o);
        if (!o) return -ENOMEM;
        s->off = o;
        s->cap = cap;
    }
    if (s->len + l > s->bcap) {
        size_t c = s->bcap ? s->bcap : 4096;
        while (s->len + l > c) c *= 2;
        char *b = pc_bigmem_realloc(s->blob, c);
        if (!b) return -ENOMEM;
        s->blob = b;
        s->bcap = c;
    }
    memcpy(s->blob + s->len, name, l);
    s->off[s->n++] = (uint32_t)s->len;
    s->len += l;
    return 0;
}
static const char *names_at(const names_t *s, int i) { return s->blob + s->off[i]; }
static void names_free(names_t *s) { pc_bigmem_free(s->blob); pc_bigmem_free(s->off); memset(s, 0, sizeof *s); }
/* heap sort of the offsets by strcmp of what they point at (qsort has no context argument; no recursion, no allocation) */
static void names_sift(const names_t *s, uint32_t *v, int root, int n)
{
    for (int c; (c = 2 * root + 1) < n; root = c) {
        if (c + 1 < n && strcmp(s->blob + v[c], s->blob + v[c + 1]) < 0) c++;
        if (strcmp(s->blob + v[root], s->blob + v[c]) >= 0) return;
        uint32_t t = v[root]; v[root] = v[c]; v[c] = t;
    }
}
static void names_sort(names_t *s)
{
    uint32_t *v = s->off;
    for (int i = s->n / 2 - 1; i >= 0; i--) names_sift(s, v, i, s->n);
    for (int e = s->n - 1; e > 0; e--) { uint32_t t = v[0]; v[0] = v[e]; v[e] = t; names_sift(s, v, 0, e); }
}

/* the entries of dir, one readdir pass: sub-directories into ls->dirs, audio regular files into ls->files (both sorted). A file bigger than PC_FILE_MAX is not listed and
 * counts in ls->big. The size is read as unsigned 64-bit from the stat that is done anyway: on the target off_t is 32-bit
 * signed, so 2 GiB up to 4 GiB arrive negative and the sign extension makes them huge, and the patched FatFs VFS (tools/fatfs_exfat)
 * saturates anything bigger to -1; on the host off_t is 64-bit and the value is exact. */
static int list(const char *dir, listing_t *ls)
{
    DIR *d = opendir(dir);
    if (!d) return -errno;
    struct dirent *e;
    int rc = 0;
    char p[512];
    while (!rc && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (!strcmp(e->d_name, PC_INT_NAME)) continue;   /* the device's own folder is never music */
        if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
        struct stat st;
        if (stat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) rc = names_add(&ls->dirs, e->d_name);
        else if (S_ISREG(st.st_mode) && pc_library_is_audio(e->d_name)) {
            if ((uint64_t)st.st_size > PC_FILE_MAX) ls->big++;
            else rc = names_add(&ls->files, e->d_name);
        }
    }
    closedir(d);
    if (!rc) { names_sort(&ls->dirs); names_sort(&ls->files); }
    return rc;
}

static int pool_add(pc_library_t *l, uint32_t *cap, const char *s, uint32_t *off)
{
    size_t n = strlen(s) + 1;
    if (l->pool_len + n > *cap) {
        uint32_t c = *cap ? *cap : 4096;
        while (l->pool_len + n > c) c *= 2;
        char *p = realloc(l->pool, c);
        if (!p) return -ENOMEM;
        l->pool = p;
        *cap = c;
    }
    memcpy(l->pool + l->pool_len, s, n);
    *off = l->pool_len;
    l->pool_len += (uint32_t)n;
    return 0;
}

void pc_library_free(pc_library_t *lib)
{
    free(lib->tracks);
    free(lib->folders);
    free(lib->pool);
    memset(lib, 0, sizeof *lib);
}

typedef struct {
    pc_library_t *lib;
    uint32_t pool_cap;
    int tcap, fcap, done;
    pc_scan_cb cb;
    void *u;
    char path[512];                        /* the directory being read; a file's path is made in place (then cut back) */
} scan_t;

/* one song: s->path is the file's path. folder: its folder number or PC_NO_FOLDER; fname: the folder's name, the artist of a song without a tag */
static int add_song(scan_t *s, const char *file, unsigned folder, const char *fname)
{
    pc_library_t *lib = s->lib;
    if (lib->n == s->tcap) {
        int c = s->tcap ? s->tcap * 2 : 256;
        pc_track_t *t = realloc(lib->tracks, (size_t)c * sizeof *t);
        if (!t) return -ENOMEM;
        lib->tracks = t;
        s->tcap = c;
    }
    pc_id3_t tag;
    memset(&tag, 0, sizeof tag);
    pc_rg_clear(&tag.rg);                   /* also when the file cannot be opened */
    const char *dot = strrchr(file, '.');
    FILE *fp = dot && pc_ext_scan_blind(dot) ? NULL : fopen(s->path, "rb");   /* an experimental format with the setting off: the name only, the file is not opened (tier 2026-11-18) */
    pc_reader_t r;
    if (fp && !pc_reader_file(&r, fp)) pc_id3_parse(&r, &tag);
    if (fp) fclose(fp);
    if (!tag.title[0]) {                    /* file name without extension */
        char name[256];
        snprintf(name, sizeof name, "%s", file);
        char *dot = strrchr(name, '.');
        if (dot) *dot = 0;
        pc_utf8_copy(tag.title, sizeof tag.title, name);
    }
    if (!tag.artist[0]) pc_utf8_copy(tag.artist, sizeof tag.artist, fname);
    pc_track_t *t = &lib->tracks[lib->n];
    memset(t, 0, sizeof *t);
    int rc = pool_add(lib, &s->pool_cap, s->path, &t->path);
    if (!rc) rc = pool_add(lib, &s->pool_cap, tag.title, &t->title);
    if (!rc) rc = pool_add(lib, &s->pool_cap, tag.artist, &t->artist);
    if (!rc) rc = pool_add(lib, &s->pool_cap, tag.album, &t->album);
    t->date = tag.date;
    t->track = tag.track;
    t->folder = (uint16_t)folder;
    t->apic_off = tag.apic_off;
    t->apic_len = tag.apic_len;
    t->rg = tag.rg;
    pc_id3_free(&tag);
    if (rc) return rc;
    if (t->date > lib->newest_date) lib->newest_date = t->date;
    lib->n++;
    if (s->cb && ++s->done % 16 == 0) s->cb(s->done, s->u);
    return 0;
}

static void listing_free(listing_t *ls) { names_free(&ls->dirs); names_free(&ls->files); }

/* The directory in s->path[0..dirlen), depth 0 = the music root. Its folder entry is made BEFORE its songs (pre-order) and taken back when nothing playable
 * lives under it (*kept false). An unreadable sub-folder is left out; the root's failure is the scan's. */
static int scan_dir(scan_t *s, size_t dirlen, int depth, int parent, const char *name, bool *kept)
{
    pc_library_t *lib = s->lib;
    int rc, fidx = -1;
    const uint32_t pool0 = lib->pool_len;
    *kept = true;
    listing_t ls;
    memset(&ls, 0, sizeof ls);
    rc = list(s->path, &ls);
    lib->skipped_big += ls.big;
    if (rc) { listing_free(&ls); if (depth) { *kept = false; return rc == -ENOMEM ? rc : 0; } return rc; }
    const int first = lib->n;
    if (depth) {
        if (lib->nfolders >= (int)PC_NO_FOLDER) { listing_free(&ls); *kept = false; return 0; }
        if (lib->nfolders == s->fcap) {
            int c = s->fcap ? s->fcap * 2 : 64;
            pc_folder_t *f = realloc(lib->folders, (size_t)c * sizeof *f);
            if (!f) { listing_free(&ls); return -ENOMEM; }
            lib->folders = f;
            s->fcap = c;
        }
        fidx = lib->nfolders++;
        pc_folder_t *f = &lib->folders[fidx];
        memset(f, 0, sizeof *f);
        f->parent = parent;
        f->first = first;
        if ((rc = pool_add(lib, &s->pool_cap, name, &f->name)) || (rc = pool_add(lib, &s->pool_cap, s->path, &f->path))) { listing_free(&ls); return rc; }
    }
    for (int i = 0; !rc && i < ls.files.n; i++) {                     /* own songs first */
        const char *fn = names_at(&ls.files, i);
        const size_t l = strlen(fn);
        if (dirlen + 1 + l >= sizeof s->path) continue;               /* a path the buffer cannot hold */
        s->path[dirlen] = '/';
        memcpy(s->path + dirlen + 1, fn, l + 1);
        rc = add_song(s, fn, depth ? (unsigned)fidx : PC_NO_FOLDER, depth ? name : "");
        s->path[dirlen] = 0;
    }
    names_free(&ls.files);                                            /* before the children: the file list is not needed below */
    const int own = lib->n - first;
    int nchild = 0;
    if (depth < PC_LIB_DEPTH) {
        for (int i = 0; !rc && i < ls.dirs.n; i++) {
            const char *dn = names_at(&ls.dirs, i);
            const size_t l = strlen(dn);
            if (dirlen + 1 + l >= sizeof s->path - 2) continue;
            s->path[dirlen] = '/';
            memcpy(s->path + dirlen + 1, dn, l + 1);
            bool k;
            rc = scan_dir(s, dirlen + 1 + l, depth + 1, fidx, dn, &k);
            s->path[dirlen] = 0;
            if (k) nchild++;
        }
    }
    names_free(&ls.dirs);
    if (rc) return rc;
    if (depth) {
        pc_folder_t *f = &lib->folders[fidx];
        f->count = own;
        f->sub_count = lib->n - first;
        f->nsub = lib->nfolders - fidx;
        f->nchild = nchild;
        if (!f->sub_count) { lib->nfolders = fidx; lib->pool_len = pool0; *kept = false; }   /* nothing playable below: not a folder (as before) */
    } else {
        lib->root_own = own;
        lib->root_nchild = nchild;
    }
    return 0;
}

int pc_library_scan(const char *root, pc_library_t *lib, pc_scan_cb cb, void *u)
{
    memset(lib, 0, sizeof *lib);
    scan_t s;
    memset(&s, 0, sizeof s);
    s.lib = lib;
    s.cb = cb;
    s.u = u;
    size_t rl = strlen(root);
    while (rl > 1 && root[rl - 1] == '/') rl--;
    if (rl >= sizeof s.path - 2) return -ENAMETOOLONG;
    memcpy(s.path, root, rl);
    bool kept;
    int rc = scan_dir(&s, rl, 0, -1, "", &kept);
    if (rc) pc_library_free(lib);
    else if (cb) cb(s.done, u);
    return rc;
}

typedef struct { char magic[4]; uint32_t version, n, nfolders, pool_len, newest, root_own, root_nchild; } hdr_t;

int pc_library_save(const pc_library_t *lib, const char *path)
{
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -errno;
    hdr_t h = { .version = VERSION, .n = (uint32_t)lib->n, .nfolders = (uint32_t)lib->nfolders, .pool_len = lib->pool_len, .newest = lib->newest_date,
                .root_own = (uint32_t)lib->root_own, .root_nchild = (uint32_t)lib->root_nchild };
    memcpy(h.magic, MAGIC, 4);
    int ok = fwrite(&h, sizeof h, 1, f) == 1;
    if (lib->n) ok &= fwrite(lib->tracks, sizeof *lib->tracks, (size_t)lib->n, f) == (size_t)lib->n;
    if (lib->nfolders) ok &= fwrite(lib->folders, sizeof *lib->folders, (size_t)lib->nfolders, f) == (size_t)lib->nfolders;
    if (lib->pool_len) ok &= fwrite(lib->pool, 1, lib->pool_len, f) == lib->pool_len;
    ok &= fwrite(TAIL, 1, 4, f) == 4;
    ok &= fclose(f) == 0;
    if (!ok) { remove(tmp); return -EIO; }
    /* FATFS rename does not replace: move the old index aside first so a power cut leaves one of them */
    char bak[310];
    snprintf(bak, sizeof bak, "%s.bak", path);
    remove(bak);
    bool had = rename(path, bak) == 0;
    if (rename(tmp, path)) { int e = errno; if (had) rename(bak, path); return -e; }
    remove(bak);
    return 0;
}

int pc_library_load(pc_library_t *lib, const char *path)
{
    memset(lib, 0, sizeof *lib);
    FILE *f = fopen(path, "rb");
    if (!f) {                                      /* interrupted save: the previous index is still there */
        char bak[310];
        snprintf(bak, sizeof bak, "%s.bak", path);
        if (!(f = fopen(bak, "rb"))) return -ENOENT;
    }
    hdr_t h;
    int rc = -EINVAL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fread(&h, sizeof h, 1, f) != 1 || memcmp(h.magic, MAGIC, 4) || h.version != VERSION || h.n > 1000000 || h.nfolders > 65535 ||
        h.pool_len > 64u << 20)
        goto out;
    long want = (long)(sizeof h + h.n * sizeof(pc_track_t) + h.nfolders * sizeof(pc_folder_t) + h.pool_len + 4);
    if (size != want) goto out;
    lib->tracks = malloc(h.n * sizeof(pc_track_t) + 1);
    lib->folders = malloc(h.nfolders * sizeof(pc_folder_t) + 1);
    lib->pool = malloc(h.pool_len + 1);
    if (!lib->tracks || !lib->folders || !lib->pool) { rc = -ENOMEM; goto out; }
    char tail[4];
    if (fread(lib->tracks, sizeof(pc_track_t), h.n, f) != h.n || fread(lib->folders, sizeof(pc_folder_t), h.nfolders, f) != h.nfolders ||
        fread(lib->pool, 1, h.pool_len, f) != h.pool_len || fread(tail, 1, 4, f) != 4 || memcmp(tail, TAIL, 4))
        goto out;
    lib->pool[h.pool_len] = 0;
    for (uint32_t i = 0; i < h.n; i++) {            /* every offset must land inside the pool */
        const pc_track_t *t = &lib->tracks[i];
        if (t->path >= h.pool_len || t->title >= h.pool_len || t->artist >= h.pool_len || t->album >= h.pool_len || (t->folder >= h.nfolders && t->folder != PC_NO_FOLDER)) goto out;
    }
    for (uint32_t i = 0; i < h.nfolders; i++) {
        const pc_folder_t *d = &lib->folders[i];
        if (d->name >= h.pool_len || d->path >= h.pool_len || d->first < 0 || d->count < 0 || d->sub_count < d->count || (uint64_t)d->first + (uint64_t)d->sub_count > h.n ||
            d->nsub < 1 || (uint32_t)d->nsub > h.nfolders - i || d->nchild < 0 || d->nchild >= d->nsub || d->parent < -1 || d->parent >= (int32_t)i) goto out;
    }
    if (h.root_own > h.n || h.root_nchild > h.nfolders) goto out;
    /* the child counts must be what the depth-first hop finds, each child pointing back at its parent (the tree page walks by nchild) */
    for (int64_t f = -1; f < (int64_t)h.nfolders; f++) {
        const int64_t end = f < 0 ? (int64_t)h.nfolders : f + lib->folders[f].nsub;
        const int64_t want = f < 0 ? (int64_t)h.root_nchild : lib->folders[f].nchild;
        int64_t k = 0, c = f + 1;
        for (; c < end; c += lib->folders[c].nsub, k++) if (lib->folders[c].parent != (int32_t)f) goto out;
        if (c != end || k != want) goto out;
    }
    lib->n = (int)h.n;
    lib->nfolders = (int)h.nfolders;
    lib->pool_len = h.pool_len;
    lib->newest_date = h.newest;
    lib->root_own = (int)h.root_own;
    lib->root_nchild = (int)h.root_nchild;
    rc = 0;
out:
    fclose(f);
    if (rc) pc_library_free(lib);
    return rc;
}

int pc_library_find(const pc_library_t *lib, const char *path)
{
    if (!path || !*path) return -1;
    for (int i = 0; i < lib->n; i++) if (!strcmp(pc_str(lib, lib->tracks[i].path), path)) return i;
    return -1;
}

int pc_library_find_folder(const pc_library_t *lib, const char *path)
{
    if (!path) return -1;
    for (int i = 0; i < lib->nfolders; i++) if (!strcmp(pc_str(lib, lib->folders[i].path), path)) return i;
    return -1;
}

void pc_library_scope(const pc_library_t *lib, int f, int *first, int *count)
{
    if (f < 0 || f >= lib->nfolders) { *first = 0; *count = lib->n; return; }
    *first = lib->folders[f].first;
    *count = lib->folders[f].sub_count;
}

void pc_library_level(const pc_library_t *lib, int f, int *first, int *count, int *nchild)
{
    if (f < 0 || f >= lib->nfolders) { *first = 0; *count = lib->root_own; *nchild = lib->root_nchild; return; }
    *first = lib->folders[f].first;
    *count = lib->folders[f].count;
    *nchild = lib->folders[f].nchild;
}

int pc_library_first_child(const pc_library_t *lib, int f)
{
    if (f < 0 || f >= lib->nfolders) return lib->root_nchild > 0 && lib->nfolders > 0 ? 0 : -1;
    return lib->folders[f].nchild > 0 && f + 1 < lib->nfolders ? f + 1 : -1;
}

int pc_library_next_sibling(const pc_library_t *lib, int c, int parent)
{
    if (c < 0 || c >= lib->nfolders) return -1;
    const int next = c + lib->folders[c].nsub, end = parent < 0 || parent >= lib->nfolders ? lib->nfolders : parent + lib->folders[parent].nsub;
    return next < end ? next : -1;
}

int pc_library_child(const pc_library_t *lib, int f, int k)
{
    int c = pc_library_first_child(lib, f);
    for (; c >= 0 && k > 0; k--) c = pc_library_next_sibling(lib, c, f);
    return k > 0 ? -1 : c;
}

static int path_cmp(const pc_library_t *l, int a, int b) { return strcmp(pc_str(l, l->tracks[a].path), pc_str(l, l->tracks[b].path)); }
static void sift(const pc_library_t *l, int *v, int root, int n)
{
    for (int c; (c = 2 * root + 1) < n; root = c) {
        if (c + 1 < n && path_cmp(l, v[c], v[c + 1]) < 0) c++;
        if (path_cmp(l, v[root], v[c]) >= 0) return;
        int t = v[root]; v[root] = v[c]; v[c] = t;
    }
}
void pc_library_path_order(const pc_library_t *lib, int *idx)
{
    int n = lib->n;
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = n / 2 - 1; i >= 0; i--) sift(lib, idx, i, n);
    for (int e = n - 1; e > 0; e--) { int t = idx[0]; idx[0] = idx[e]; idx[e] = t; sift(lib, idx, 0, e); }
}

int pc_library_find_sorted(const pc_library_t *lib, const int *idx, const char *path)
{
    if (!path || !*path) return -1;
    int lo = 0, hi = lib->n - 1;
    while (lo <= hi) {
        int m = lo + (hi - lo) / 2, c = strcmp(pc_str(lib, lib->tracks[idx[m]].path), path);
        if (!c) return idx[m];
        if (c < 0) lo = m + 1; else hi = m - 1;
    }
    return -1;
}
