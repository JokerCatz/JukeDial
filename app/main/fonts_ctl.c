// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fonts_ctl.h"
#include "pc/pc_paths.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "pc/pc_sha256.h"
#include "cfg.h"
#include "usb_mode.h"
#include "pc/pc_crc32.h"
#include "pc/pc_upload.h"

/* Font upload over the settings protocol; the page is tools/web/fontput.js.
 *   font_put_begin {name, size}          name = ui-tc.ttf | ui-sc.ttf | ui-ext.ttf; size 1..FONT_MAX; writes <name>.part   -> {chunk}
 *   font_put {off, crc, data}            data base64 <= 2048 B, crc = CRC-32 of those raw bytes (a bad chunk is refused and not written); in order; a chunk already
 *                                        written is acknowledged again without writing (a resend after a lost reply)         -> {got}
 *   font_put_stat                        -> {got}
 *   font_put_end {sha256}                SHA-256 of the whole file must match and it must be a TrueType font; then <name>.part becomes <name>.stg (verified, not active) -> {staged, size, sha256}
 *   font_put_commit {names:[...]}        every named .stg becomes .new, all or none: only .new files are swapped in at boot, so a failed set never mixes old and new fonts -> {committed}
 *   font_put_abort {all?}                removes <name>.part; with all:true also every .stg (a set that failed half way)
 * Refused while the card is the computer's (USB drive / external mode) or missing. The swap into place happens at the next boot (font.c); until then nothing the
 * device draws with changes. Every refusal carries "code" (the page translates it) and "error" (English), and "got" when an upload is open. */
#define FONT_MAX (4u << 20)                /* the fonts partition is 4 MB: no single file may be bigger */
#define FONT_CHUNK PC_LIST_CHUNK
static const char *const NAMES[] = { "ui-tc.ttf", "ui-sc.ttf", "ui-ext.ttf" };

static struct { pc_upload_t u; FILE *f; char path[80], part[84], stage[84]; } s_up;   /* main's .bss: PSRAM (linker.lf) */
static uint8_t s_bin[FONT_CHUNK];

/* "code" is what the web page translates (tools/web/fonttab.js, W_FONTS_DEV_*); "error" stays the English text for logs and other clients */
static cJSON *fail(cJSON *r, const char *code, const char *why) { cJSON_ReplaceItemInObject(r, "ok", cJSON_CreateFalse()); cJSON_AddStringToObject(r, "code", code); cJSON_AddStringToObject(r, "error", why); return r; }
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
    if (s_up.f) { fclose(s_up.f); s_up.f = NULL; remove(s_up.part); printf("APP fonts upload abandoned %s\n", s_up.path); }
    s_up.u.active = 0;
}
static const char *card_refusal(void)   /* a code */
{
    if (usb_mode_active() || usb_mode_where() > 0) return "card_pc";
    if (!g_sd_ok) return "no_card";
    return NULL;
}
static const char *card_text(const char *code) { return !strcmp(code, "card_pc") ? "the card is on the computer's side: leave USB drive / external mode first" : "no TF card"; }

static cJSON *begin(cJSON *r, const cJSON *q)
{
    abandon();
    uint32_t size;
    const char *name = str(q, "name"), *why = card_refusal();
    if (why) return fail(r, why, card_text(why));
    bool known = false;
    for (unsigned i = 0; name && i < sizeof NAMES / sizeof NAMES[0]; i++) known |= !strcmp(name, NAMES[i]);
    if (!known) return fail(r, "name", "name must be ui-tc.ttf, ui-sc.ttf or ui-ext.ttf");
    if (!num(q, "size", 1024, FONT_MAX, &size)) return fail(r, "size", "size 1 KB..4 MB");
    mkdir(PC_INT, 0777);
    mkdir(PC_FONTS_DIR, 0777);
    snprintf(s_up.path, sizeof s_up.path, "%s/%s", PC_FONTS_DIR, name);
    snprintf(s_up.part, sizeof s_up.part, "%s.part", s_up.path);
    snprintf(s_up.stage, sizeof s_up.stage, "%s.stg", s_up.path);   /* verified, waiting for font_put_commit */
    pc_upload_begin(&s_up.u, size);
    s_up.f = fopen(s_up.part, "wb");
    if (!s_up.f) { s_up.u.active = 0; return fail(r, "write", "cannot write the TF card"); }
    setvbuf(s_up.f, NULL, _IONBF, 0);
    printf("APP fonts upload %s %u B\n", s_up.path, (unsigned)size);
    cJSON_AddNumberToObject(r, "chunk", FONT_CHUNK);
    return r;
}

static cJSON *chunk(cJSON *r, const cJSON *q)
{
    uint32_t off, crc;
    const char *d = str(q, "data"), *why = card_refusal();
    if (!s_up.f || !s_up.u.active) return fail(r, "no_upload", "no upload (font_put_begin first)");
    if (why) { abandon(); return fail(r, why, card_text(why)); }
    if (!d || !num(q, "off", 0, FONT_MAX, &off) || !num(q, "crc", 0, 4294967295.0, &crc)) return fail(r, "args", "off, crc and data?");
    int n = pc_b64_decode(d, s_bin, sizeof s_bin);
    if (n <= 0) return fail(r, "base64", "bad base64 or chunk over 2048 B");
    cJSON_AddNumberToObject(r, "got", s_up.u.got);
    if (pc_crc32(0, s_bin, (uint32_t)n) != crc) return fail(r, "chunk_crc", "chunk CRC mismatch: send it again");
    int rc = pc_upload_chunk(&s_up.u, off, s_bin, (uint32_t)n);
    if (rc == PC_UPLOAD_WRITE && fwrite(s_bin, 1, (size_t)n, s_up.f) != (size_t)n) { abandon(); return fail(r, "write", "TF card write failed"); }
    cJSON_ReplaceItemInObject(r, "got", cJSON_CreateNumber(s_up.u.got));
    if (rc < 0) return rc == -E2BIG ? fail(r, "past_size", "past the announced size") : fail(r, "order", "out of order");
    return r;
}

static bool file_sha(const char *path, char hex[65], uint8_t head[4])
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    setvbuf(f, NULL, _IONBF, 0);
    static uint8_t b[4096];                       /* main's .bss: PSRAM (linker.lf) */
    pc_sha256_t sh;
    pc_sha256_init(&sh);
    size_t n;
    bool first = true;
    memset(head, 0, 4);
    while ((n = fread(b, 1, sizeof b, f)) > 0) {
        if (first) { memcpy(head, b, n < 4 ? n : 4); first = false; }
        pc_sha256_update(&sh, b, (uint32_t)n);
    }
    fclose(f);
    uint8_t h[32];
    pc_sha256_final(&sh, h);
    pc_sha256_hex(h, hex);
    return true;
}

static cJSON *end(cJSON *r, const cJSON *q)
{
    const char *want = str(q, "sha256");
    if (!s_up.f || !s_up.u.active) return fail(r, "no_upload", "no upload");
    if (!want || strlen(want) != 64) return fail(r, "args", "sha256?");
    if (s_up.u.got != s_up.u.size) { cJSON_AddNumberToObject(r, "got", s_up.u.got); return fail(r, "incomplete", "incomplete"); }
    fclose(s_up.f);
    s_up.f = NULL;
    s_up.u.active = 0;
    char hex[65] = "";
    uint8_t head[4] = { 0 };
    if (!file_sha(s_up.part, hex, head)) { remove(s_up.part); return fail(r, "write", "cannot read the TF card"); }
    if (strcmp(hex, want)) { remove(s_up.part); return fail(r, "sha", "SHA-256 mismatch: send it again"); }
    if (memcmp(head, "\x00\x01\x00\x00", 4)) { remove(s_up.part); return fail(r, "not_ttf", "not a TrueType font (glyf outlines)"); }
    remove(s_up.stage);                              /* FAT rename does not replace */
    if (rename(s_up.part, s_up.stage)) { remove(s_up.part); return fail(r, "rename", "cannot rename on the TF card"); }
    printf("APP fonts verified %s size=%u sha=%s\n", s_up.stage, (unsigned)s_up.u.size, hex);
    cJSON_AddBoolToObject(r, "staged", true);
    cJSON_AddNumberToObject(r, "size", s_up.u.size);
    cJSON_AddStringToObject(r, "sha256", hex);
    return r;
}


static void name_path(const char *name, const char *ext, char *out, size_t cap) { snprintf(out, cap, "%s/%s%s", PC_FONTS_DIR, name, ext); }

/* remove every half-done file of a failed set: the .part of the upload that broke and the .stg files of the ones that had finished */
static void discard_all(void)
{
    char p[96];
    for (unsigned i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++) {
        name_path(NAMES[i], ".part", p, sizeof p); remove(p);
        name_path(NAMES[i], ".stg", p, sizeof p); remove(p);
    }
}

/* font_put_commit {names:[...]}: every named .stg becomes .new, all or none (the boot-time swap in font.c only looks at .new, so a set that failed half way never mixes a new TC with an old SC).
 * If a rename fails the ones already done are put back. */
static cJSON *commit(cJSON *r, const cJSON *q)
{
    const cJSON *a = cJSON_GetObjectItem(q, "names");
    const char *why = card_refusal();
    if (why) return fail(r, why, card_text(why));
    if (!cJSON_IsArray(a) || cJSON_GetArraySize(a) < 1 || cJSON_GetArraySize(a) > 3) return fail(r, "args", "names: 1 to 3 font names");
    char stg[3][96], nw[3][96];
    int n = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, a) {
        bool known = false;
        for (unsigned i = 0; cJSON_IsString(it) && i < sizeof NAMES / sizeof NAMES[0]; i++) known |= !strcmp(it->valuestring, NAMES[i]);
        if (!known) return fail(r, "name", "name must be ui-tc.ttf, ui-sc.ttf or ui-ext.ttf");
        name_path(it->valuestring, ".stg", stg[n], sizeof stg[n]); name_path(it->valuestring, ".new", nw[n], sizeof nw[n]);
        struct stat st;
        if (stat(stg[n], &st)) return fail(r, "nothing_staged", "that font was not uploaded and verified first");
        n++;
    }
    for (int i = 0; i < n; i++) {
        remove(nw[i]);
        if (rename(stg[i], nw[i])) {
            for (int k = 0; k < i; k++) rename(nw[k], stg[k]);   /* back to staged: nothing is active */
            return fail(r, "rename", "cannot rename on the TF card");
        }
    }
    printf("APP fonts committed %d file(s): swapped in at the next boot\n", n);
    cJSON_AddNumberToObject(r, "committed", n);
    return r;
}

cJSON *fonts_ctl(const char *op, cJSON *r, const cJSON *q)
{
    if (!strcmp(op, "font_put_begin")) return begin(r, q);
    if (!strcmp(op, "font_put")) return chunk(r, q);
    if (!strcmp(op, "font_put_end")) return end(r, q);
    if (!strcmp(op, "font_put_abort")) { abandon(); if (cJSON_IsTrue(cJSON_GetObjectItem(q, "all"))) discard_all(); return r; }
    if (!strcmp(op, "font_put_stat")) {
        if (!s_up.f || !s_up.u.active) return fail(r, "no_upload", "no upload");
        cJSON_AddNumberToObject(r, "got", s_up.u.got);
        return r;
    }
    if (!strcmp(op, "font_put_commit")) return commit(r, q);
    return fail(r, "unknown", "unknown op");
}
