// SPDX-License-Identifier: LGPL-2.1-or-later
#include "lists_ctl.h"
#include "pc/pc_paths.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "pc/pc_glyphpack.h"
#include "pc/pc_upload.h"
#include "psram_alloc.h"
#include "ui/glyph_text.h"

/* Glyph-pack lists over the settings protocol (spec 2026-11-11-glyphpack-design.md section 7). LVGL task (ctl_run).
 *   list_ls                                  -> lists: [{name, size}]
 *   list_put_begin {name, size}              -> writes /sdcard/lists/<name>.new
 *   list_put {off, data: base64 <= 2048 B}   -> {got}: in order; a chunk already written is acknowledged again without
 *                                               writing (a resend after a lost reply); any refusal also carries got
 *   list_put_stat                            -> {got} (after a lost reply: go on from there)
 *   list_put_end {crc}                       -> CRC-32 of the whole file, then it must open as a glyph pack, then rename
 *   list_get {name, off, len <= 2048}        -> {size, data} (the page reloads a list to edit it)
 *   list_rm {name}
 * Names: pc_list_path (no "..", no '/', only /sdcard/lists/). A list open on the screen is not replaced or deleted. */

static struct {
    pc_upload_t u;
    FILE *f;
    char path[PC_LIST_PATH_MAX], tmp[PC_LIST_PATH_MAX];
} s_up;                                          /* main's .bss: PSRAM (linker.lf) */
static uint8_t s_bin[PC_LIST_CHUNK];
static char s_b64[(PC_LIST_CHUNK + 2) / 3 * 4 + 1];

static cJSON *fail(cJSON *r, const char *why) { cJSON_ReplaceItemInObject(r, "ok", cJSON_CreateFalse()); cJSON_AddStringToObject(r, "error", why); return r; }
static const char *str(const cJSON *q, const char *k) { const cJSON *v = cJSON_GetObjectItem(q, k); return cJSON_IsString(v) ? v->valuestring : NULL; }
static bool num(const cJSON *q, const char *k, double lo, double hi, uint32_t *out)
{
    const cJSON *v = cJSON_GetObjectItem(q, k);
    if (!cJSON_IsNumber(v) || v->valuedouble < lo || v->valuedouble > hi || v->valuedouble != (double)(uint32_t)v->valuedouble) return false;
    *out = (uint32_t)v->valuedouble;
    return true;
}

static void abandon(void)
{
    if (s_up.f) { fclose(s_up.f); s_up.f = NULL; remove(s_up.tmp); printf("APP lists upload abandoned %s\n", s_up.path); }
    s_up.u.active = 0;
}

static cJSON *ls(cJSON *r)
{
    cJSON *a = cJSON_AddArrayToObject(r, "lists");
    DIR *d = opendir(PC_LISTS_DIR);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        char p[PC_LIST_PATH_MAX];
        struct stat st;
        if (pc_list_path(e->d_name, p, sizeof p) || stat(p, &st)) continue;   /* only names we could have written */
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", e->d_name);
        cJSON_AddNumberToObject(o, "size", (double)st.st_size);
        cJSON_AddItemToArray(a, o);
    }
    if (d) closedir(d);
    return r;
}

static cJSON *begin(cJSON *r, const cJSON *q)
{
    abandon();
    uint32_t size;
    char path[PC_LIST_PATH_MAX];
    if (pc_list_path(str(q, "name"), path, sizeof path)) return fail(r, "bad name (letters, no / or .., ends in .zip, 60 bytes max)");
    if (!num(q, "size", 1, PC_LIST_MAX_BYTES, &size)) return fail(r, "size 1..8 MB (bigger: copy it in USB drive mode)");
    if (gt_pack_in_use(path)) return fail(r, "the list is open on the device: leave that screen first");
    mkdir(PC_LISTS_DIR, 0777);
    snprintf(s_up.path, sizeof s_up.path, "%s", path);
    snprintf(s_up.tmp, sizeof s_up.tmp, "%s.new", path);
    pc_upload_begin(&s_up.u, size);
    s_up.f = fopen(s_up.tmp, "wb");
    if (!s_up.f) { s_up.u.active = 0; return fail(r, "cannot write the TF card"); }
    setvbuf(s_up.f, NULL, _IONBF, 0);            /* 2 KB writes; no stdio buffer in internal RAM */
    printf("APP lists upload %s %u B\n", path, (unsigned)size);
    cJSON_AddNumberToObject(r, "chunk", PC_LIST_CHUNK);
    return r;
}

static cJSON *chunk(cJSON *r, const cJSON *q)
{
    uint32_t off;
    const char *d = str(q, "data");
    if (!s_up.f || !s_up.u.active) return fail(r, "no upload (list_put_begin first)");
    if (!d || !num(q, "off", 0, PC_LIST_MAX_BYTES, &off)) return fail(r, "off and data?");
    int n = pc_b64_decode(d, s_bin, sizeof s_bin);
    if (n <= 0) return fail(r, "bad base64 or chunk over 2048 B");
    int rc = pc_upload_chunk(&s_up.u, off, s_bin, (uint32_t)n);
    if (rc == PC_UPLOAD_WRITE && fwrite(s_bin, 1, (size_t)n, s_up.f) != (size_t)n) { abandon(); return fail(r, "TF card write failed"); }
    cJSON_AddNumberToObject(r, "got", s_up.u.got);
    if (rc < 0) return fail(r, rc == -E2BIG ? "past the announced size" : "out of order");
    return r;
}

static cJSON *end(cJSON *r, const cJSON *q)
{
    uint32_t crc;
    if (!s_up.f || !s_up.u.active) return fail(r, "no upload");
    if (!num(q, "crc", 0, 4294967295.0, &crc)) return fail(r, "crc?");
    int rc = pc_upload_end(&s_up.u, crc);
    if (rc) { abandon(); return fail(r, rc == -EBADMSG ? "CRC mismatch: send it again" : "incomplete"); }
    bool ok = !fclose(s_up.f);
    s_up.f = NULL;
    FILE *f = ok ? fopen(s_up.tmp, "rb") : NULL;       /* it must open as a pack before it replaces anything */
    pc_reader_t rd;
    pc_gp_t *gp = NULL;
    static const pc_gp_alloc_t A = { psram_malloc, psram_free };
    int grc = !f ? -EIO : pc_reader_file(&rd, f) ? -EIO : pc_gp_open(&rd, &A, &gp);
    uint32_t glyphs = gp ? pc_gp_glyph_count(gp) : 0, nodes = gp ? pc_gp_node_count(gp) : 0, index = gp ? pc_gp_block_bytes(gp) : 0;
    pc_gp_close(gp);
    if (f) fclose(f);
    if (grc) {
        remove(s_up.tmp);
        printf("APP lists upload %s not a pack rc=%d\n", s_up.path, grc);
        return fail(r, grc == -EPROTONOSUPPORT ? "pack version not supported by this firmware" : "not a valid list pack");
    }
    if (gt_pack_in_use(s_up.path)) { remove(s_up.tmp); return fail(r, "the list is open on the device: leave that screen first"); }
    remove(s_up.path);                                 /* FAT rename does not replace */
    if (rename(s_up.tmp, s_up.path)) { remove(s_up.tmp); return fail(r, "cannot rename on the TF card"); }
    printf("APP lists saved %s glyphs=%u nodes=%u index=%u B\n", s_up.path, (unsigned)glyphs, (unsigned)nodes, (unsigned)index);
    cJSON_AddNumberToObject(r, "glyphs", glyphs);
    cJSON_AddNumberToObject(r, "nodes", nodes);
    cJSON_AddNumberToObject(r, "index_bytes", index);
    return r;
}

static cJSON *get(cJSON *r, const cJSON *q)
{
    char path[PC_LIST_PATH_MAX];
    uint32_t off, len;
    if (pc_list_path(str(q, "name"), path, sizeof path)) return fail(r, "bad name");
    if (!num(q, "off", 0, PC_LIST_MAX_BYTES, &off) || !num(q, "len", 1, PC_LIST_CHUNK, &len)) return fail(r, "off, len 1..2048?");
    FILE *f = fopen(path, "rb");
    if (!f) return fail(r, "no such list");
    setvbuf(f, NULL, _IONBF, 0);
    struct stat st = { 0 };
    size_t n = 0;
    if (!fstat(fileno(f), &st) && off <= (uint32_t)st.st_size && !fseek(f, (long)off, SEEK_SET)) n = fread(s_bin, 1, len, f);
    fclose(f);
    pc_b64_encode(s_bin, (uint32_t)n, s_b64, sizeof s_b64);
    cJSON_AddNumberToObject(r, "size", (double)st.st_size);
    cJSON_AddNumberToObject(r, "off", off);
    cJSON_AddStringToObject(r, "data", s_b64);
    return r;
}

static cJSON *rm(cJSON *r, const cJSON *q)
{
    char path[PC_LIST_PATH_MAX];
    if (pc_list_path(str(q, "name"), path, sizeof path)) return fail(r, "bad name");
    if (gt_pack_in_use(path)) return fail(r, "the list is open on the device: leave that screen first");
    if (remove(path)) return fail(r, "no such list");
    return r;
}

cJSON *lists_ctl(const char *op, cJSON *r, const cJSON *q)
{
    if (!strcmp(op, "list_ls")) return ls(r);
    if (!strcmp(op, "list_put_begin")) return begin(r, q);
    if (!strcmp(op, "list_put")) return chunk(r, q);
    if (!strcmp(op, "list_put_end")) return end(r, q);
    if (!strcmp(op, "list_get")) return get(r, q);
    if (!strcmp(op, "list_rm")) return rm(r, q);
    if (!strcmp(op, "list_put_abort")) { abandon(); return r; }
    if (!strcmp(op, "list_put_stat")) {
        if (!s_up.f || !s_up.u.active) return fail(r, "no upload");
        cJSON_AddNumberToObject(r, "got", s_up.u.got);
        return r;
    }
    return fail(r, "unknown op");
}
