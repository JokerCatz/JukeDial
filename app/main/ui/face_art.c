// SPDX-License-Identifier: LGPL-2.1-or-later
/* Designed faces (face_art.h). The art is greyscale layers (frame, accent, label, head, warn) and sprite sheets (seven-segment digits drawn as
 * polygons at build time, state plates); this file only tints and copies them. Colour: each layer role and each slot ink takes one colour of the
 * scheme; the warning layer replaces the head layer and the whole face takes the red scheme while a real warning state lasts.
 * Light themes: the face keeps its own dark ground (like the moon's night backing): the art is emissive light on dark, a light variant would be a
 * second design of every face. Memory: two RGB565 frames (background cache + the shown frame) and the decoded sheets, PSRAM, freed with the box. */
#include "face_art.h"
#include "misc/cache/instance/lv_image_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "pc/pc_io.h"
#include "pc/pc_paths.h"
#include "pc/pc_zip.h"
#include "psram_alloc.h"

static const char *const s_fields[FAV__N] = {
    "time_hm", "time_s", "date", "wday", "alarm", "alarm_hm", "sync", "power", "batt", "battbar", "clock_hm",
    "main", "cc", "hours", "state", "set", "prog", "laps", "lap1_no", "lap1", "lap2_no", "lap2", "lap3_no", "lap3",
    "best", "phase", "round",
};
const char *face_art_field_name(int f) { return f >= 0 && f < FAV__N ? s_fields[f] : "?"; }

enum { R_FRAME, R_ACCENT, R_LABEL, R_HEAD, R_WARN, R__N };
static const char *const s_roles[R__N] = { "frame", "accent", "label", "head", "warn" };
enum { I_BG, I_FRAME, I_ACCENT, I_LABEL, I_DIGIT, I_VALUE, I_WARN, I__N };
/* bg frame accent label digit value warn (tools/gen/make_faces.py SCHEMES, keep in step) */
static const uint32_t s_schemes[FA__SCHEMES][I__N] = {
    { 0x000a03, 0x22d64c, 0x2cf05a, 0xa8ffb8, 0x52ff6e, 0x52ff6e, 0xff2a10 },
    { 0x0c0200, 0xff4014, 0xff4c1c, 0xffa078, 0xff5c22, 0xff7c44, 0xff2a10 },
    { 0x00070b, 0x40c8e6, 0x40c8e6, 0xc8f6ff, 0x9cf2ff, 0x9cf2ff, 0xff2a10 },
    { 0x06030c, 0x8e62d4, 0xff6c1c, 0xe8deff, 0xff7c20, 0x5cff5c, 0xff2a10 },
};
static const uint8_t s_role_ink[R__N] = { I_FRAME, I_ACCENT, I_LABEL, I_LABEL, I_WARN };

#define MAX_LAYERS 8
#define MAX_SLOTS 40
#define MAX_SHEETS 12
typedef struct {
    char name[16];
    bool glyph;                     /* digit strip (cells side by side, advance per glyph) or a pick sheet (cells stacked) */
    int cw, h, pad, n;
    char glyphs[24];
    uint8_t adv[24];
    uint8_t *px;                    /* A8, PSRAM */
    int pw, ph;
} sheet_t;
typedef struct {
    uint8_t type;                   /* 0 digits, 1 pick, 2 bar */
    uint8_t field, ink, ghost;
    int8_t sheet;
    int16_t x, y, w, h, n, gap;
    char chars[16];                 /* digits: the fixed format ("88:88"), its length is the slot's */
    char sname[16];                 /* the sheet's name in face.txt */
    int16_t rx, ry, rw, rh;         /* the rectangle it owns */
} slot_t;
typedef struct { uint8_t role; char file[16]; int16_t x, y; uint32_t off, len; } layer_t;   /* off / len: the layer's bytes in the zip (stored) */
struct face_art {
    lv_obj_t *img;
    lv_image_dsc_t dsc;
    uint16_t *fb, *bg;
    int w, h;
    bool round, drawn, warn;
    int scheme;
    char dir[16];                   /* the face's folder inside the zip ("rd_clock") */
    FILE *zf;                       /* _player/faces.zip: open only while the face opens or composes its background (no FATFS file object held between), else NULL */
    pc_reader_t zr;
    layer_t L[MAX_LAYERS]; int nl;
    slot_t S[MAX_SLOTS]; int ns;
    sheet_t sh[MAX_SHEETS]; int nsh;
    face_vals_t cur;
    uint32_t mem;
    int cx0, cx1;                   /* blend clips x to [cx0, cx1) (a partly redrawn digit slot) */
};
static EXT_RAM_BSS_ATTR uint32_t s_redraw_px;
uint32_t face_art_redraw_px(void) { return s_redraw_px; }
uint32_t face_art_mem(const face_art_t *fa) { return fa ? fa->mem : 0; }

/* ---- files: the faces zip on the card (_player/faces.zip), STORE method only: the bytes of an entry are read straight from the file, nothing is inflated ---- */
typedef struct { pc_zip_entry_t *e; int n; } zdir_t;      /* the central directory, only while a face opens */
static bool zip_entry(const face_art_t *fa, const zdir_t *z, const char *path, uint32_t *off, uint32_t *len)
{
    const pc_zip_entry_t *e = pc_zip_find(z->e, z->n, path);
    if (!e || e->method != 0 || e->csize != e->usize || pc_zip_data_offset(&fa->zr, e, off)) return false;
    *len = e->usize;
    return true;
}
static bool zip_begin(face_art_t *fa)
{
    fa->zf = fopen(PC_FACES_ZIP, "rb");
    if (fa->zf && pc_reader_file(&fa->zr, fa->zf)) { fclose(fa->zf); fa->zf = NULL; }
    return fa->zf != NULL;
}
static void zip_end(face_art_t *fa) { if (fa->zf) fclose(fa->zf); fa->zf = NULL; }
#define ZIP_FILE_MAX (1024 * 1024)
/* the bytes at off / len in a PSRAM buffer (one spare byte, zero), NULL when too big or unreadable; psram_free it */
static uint8_t *zip_read(const face_art_t *fa, uint32_t off, uint32_t len)
{
    if (!fa->zf || !len || len > ZIP_FILE_MAX) return NULL;
    uint8_t *b = psram_malloc((size_t)len + 1);
    if (!b) return NULL;
    if ((uint32_t)fa->zr.read(fa->zr.u, off, b, len) != len) { psram_free(b); return NULL; }
    b[len] = 0;
    return b;
}
static uint8_t *zip_file(const face_art_t *fa, const zdir_t *z, const char *dir, const char *file, uint32_t *n)   /* "<dir>/<file>" */
{
    char p[64];
    snprintf(p, sizeof p, "%s/%s", dir, file);
    uint32_t off;
    return zip_entry(fa, z, p, &off, n) ? zip_read(fa, off, *n) : NULL;
}
/* the state of the faces zip: FACE_PACK_OK, _MISSING (no file), _BAD (not a zip, no sheets/sheets.txt, or an entry that is not stored) */
int face_art_pack_state(void)
{
    FILE *f = fopen(PC_FACES_ZIP, "rb");
    if (!f) return FACE_PACK_MISSING;
    pc_reader_t r;
    int st = FACE_PACK_BAD;
    if (!pc_reader_file(&r, f)) {
        const char *names[1] = { "sheets/sheets.txt" };
        pc_zip_entry_t e;
        if (pc_zip_lookup(&r, names, 1, &e) == 1 && e.method == 0 && e.csize == e.usize) st = FACE_PACK_OK;
    }
    fclose(f);
    return st;
}

/* ---- images: "JDL1" RLE (make_faces.py) or "P5" binary PGM (any paint program) -> A8 in PSRAM ---- */
static uint8_t *decode(const uint8_t *b, size_t n, int *w, int *h)
{
    if (n >= 8 && !memcmp(b, "JDL1", 4)) {
        *w = b[4] | b[5] << 8; *h = b[6] | b[7] << 8;
        size_t tot = (size_t)*w * *h, o = 0;
        uint8_t *px = tot ? psram_malloc(tot) : NULL;
        if (!px) return NULL;
        for (size_t i = 8; i < n && o < tot; i++) {
            uint8_t t = b[i];
            size_t run = 1; uint8_t v;
            if (t < 0x40) { run = t + 1u; v = 0; }
            else if (t < 0x80) { run = t - 0x3fu; v = 255; }
            else if (t < 0xff) v = (uint8_t)((t & 0x7f) * 2);
            else { if (i + 2 >= n) break; run = b[i + 1] + 1u; v = b[i + 2]; i += 2; }
            if (run > tot - o) run = tot - o;
            memset(px + o, v, run);
            o += run;
        }
        if (o < tot) memset(px + o, 0, tot - o);
        return px;
    }
    if (n >= 2 && b[0] == 'P' && b[1] == '5') {             /* P5 <w> <h> <max>\n then w*h bytes (max 255) */
        int v[3], k = 0; size_t i = 2;
        while (k < 3 && i < n) {
            while (i < n && (b[i] == ' ' || b[i] == '\n' || b[i] == '\r' || b[i] == '\t')) i++;
            if (i < n && b[i] == '#') { while (i < n && b[i] != '\n') i++; continue; }
            int x = 0; bool any = false;
            while (i < n && b[i] >= '0' && b[i] <= '9') { x = x * 10 + (b[i++] - '0'); any = true; if (x > 100000) return NULL; }
            if (!any) return NULL;
            v[k++] = x;
        }
        if (k < 3 || v[2] != 255 || i >= n) return NULL;
        i++;
        size_t tot = (size_t)v[0] * v[1];
        if (!tot || n - i < tot) return NULL;
        uint8_t *px = psram_malloc(tot);
        if (!px) return NULL;
        memcpy(px, b + i, tot);
        *w = v[0]; *h = v[1];
        return px;
    }
    return NULL;
}

/* ---- pixels ---- */
static inline uint16_t rgb565(uint32_t c) { return (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x1f)); }
static inline uint16_t mix(uint16_t d, uint32_t c, unsigned a)   /* a 0..255 */
{
    unsigned dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
    unsigned sr = (c >> 19) & 31, sg = (c >> 10) & 63, sb = (c >> 3) & 31;
    dr += ((int)(sr - dr) * (int)a) / 255;
    dg += ((int)(sg - dg) * (int)a) / 255;
    db += ((int)(sb - db) * (int)a) / 255;
    return (uint16_t)(dr << 11 | dg << 5 | db);
}
/* blend the (sx, sy, sw, sh) part of an A8 image (stride aw) into dst at (dx, dy), colour c, strength k (0..256) */
static void blend(face_art_t *fa, uint16_t *dst, const uint8_t *a, int aw, int sx, int sy, int sw, int sh, int dx, int dy, uint32_t c, int k)
{
    for (int y = 0; y < sh; y++) {
        int ty = dy + y;
        if (ty < 0 || ty >= fa->h) continue;
        const uint8_t *row = a + (size_t)(sy + y) * aw + sx;
        uint16_t *o = dst + (size_t)ty * fa->w;
        for (int x = 0; x < sw; x++) {
            int tx = dx + x;
            unsigned v = row[x];
            if (!v || tx < fa->cx0 || tx >= fa->cx1) continue;
            v = (v * (unsigned)k) >> 8;
            if (v) o[tx] = mix(o[tx], c, v > 255 ? 255 : v);
        }
    }
}

/* ---- the definition ---- */
static int tok(char *s, char **t, int max)
{
    int n = 0;
    char *sv = NULL;
    for (char *p = strtok_r(s, " \t\r", &sv); p && n < max; p = strtok_r(NULL, " \t\r", &sv)) t[n++] = p;
    return n;
}
static int find_field(const char *s) { for (int i = 0; i < FAV__N; i++) if (!strcmp(s, s_fields[i])) return i; return -1; }
static int find_ink(const char *s)
{
    static const char *const n[I__N] = { "bg", "frame", "accent", "label", "digit", "value", "warn" };
    for (int i = 1; i < I__N; i++) if (!strcmp(s, n[i])) return i;
    return I_DIGIT;
}
static int sheet_idx(face_art_t *fa, const char *name)
{
    for (int i = 0; i < fa->nsh; i++) if (!strcmp(fa->sh[i].name, name)) return i;
    return -1;
}
/* one sheets.txt line: sheet <name> <file> glyph <cw> <h> <pad> <glyphs> <adv,...> | sheet <name> <file> pick <w> <h> <n> */
static void sheet_line(face_art_t *fa, const zdir_t *z, char **t, int n)
{
    if (n < 7 || strcmp(t[0], "sheet") || fa->nsh >= MAX_SHEETS || sheet_idx(fa, t[1]) >= 0) return;
    bool used = false;                                    /* only the sheets a slot names are decoded */
    for (int i = 0; i < fa->ns && !used; i++) used = !strcmp(fa->S[i].sname, t[1]);
    if (!used) return;
    sheet_t *s = &fa->sh[fa->nsh];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", t[1]);
    s->glyph = !strcmp(t[3], "glyph");
    s->cw = atoi(t[4]); s->h = atoi(t[5]);
    if (s->glyph) {
        if (n < 9) return;
        s->pad = atoi(t[6]);
        snprintf(s->glyphs, sizeof s->glyphs, "%s", t[7]);
        s->n = (int)strlen(s->glyphs);
        for (int i = 0; i < s->n; i++) if (s->glyphs[i] == '_') s->glyphs[i] = ' ';
        char *p = t[8];
        for (int i = 0; i < s->n && *p; i++) { s->adv[i] = (uint8_t)atoi(p); while (*p && *p != ',') p++; if (*p) p++; }
    } else s->n = atoi(t[6]);
    uint32_t len;
    uint8_t *b = zip_file(fa, z, "sheets", t[2], &len);
    if (!b) return;
    s->px = decode(b, len, &s->pw, &s->ph);
    psram_free(b);
    if (!s->px || s->cw <= 0 || s->h <= 0) { psram_free(s->px); return; }
    const char *why = s->n < 1 ? "n < 1" : s->cw > s->pw ? "cw wider than the image" : s->h > s->ph ? "h taller than the image"
        : s->glyph ? ((int64_t)s->n * s->cw > s->pw ? "glyph cells wider than the image" : NULL)
        : ((int64_t)s->n * s->h > s->ph ? "pick cells taller than the image" : NULL);
    if (why) { printf("APP face sheet '%s' dropped: %s (n=%d cw=%d h=%d image %dx%d)\n", s->name, why, s->n, s->cw, s->h, s->pw, s->ph); psram_free(s->px); return; }
    fa->mem += (uint32_t)s->pw * s->ph;
    fa->nsh++;
}
static void parse_sheets(face_art_t *fa, const zdir_t *z, char *txt)
{
    for (char *line = txt, *nx; line && *line; line = nx) {
        nx = strchr(line, '\n');
        if (nx) *nx++ = 0;
        if (line[0] == '#') continue;
        char *t[10];
        int n = tok(line, t, 10);
        if (n) sheet_line(fa, z, t, n);
    }
}
static void parse_face(face_art_t *fa, char *txt)
{
    for (char *line = txt, *nx; line && *line; line = nx) {
        nx = strchr(line, '\n');
        if (nx) *nx++ = 0;
        if (line[0] == '#') continue;
        char *t[12];
        int n = tok(line, t, 12);
        if (n >= 4 && !strcmp(t[0], "face")) { fa->w = atoi(t[2]); fa->h = atoi(t[3]); fa->round = n > 4 && !strcmp(t[4], "round"); }
        else if (n >= 5 && !strcmp(t[0], "layer") && fa->nl < MAX_LAYERS) {
            int r = -1;
            for (int i = 0; i < R__N; i++) if (!strcmp(t[1], s_roles[i])) r = i;
            if (r < 0) continue;
            layer_t *L = &fa->L[fa->nl++];
            L->role = (uint8_t)r;
            snprintf(L->file, sizeof L->file, "%s", t[2]);
            L->x = (int16_t)atoi(t[3]); L->y = (int16_t)atoi(t[4]);
        }
        else if (fa->ns < MAX_SLOTS && n >= 6 && (!strcmp(t[0], "slot") || !strcmp(t[0], "pick"))) {
            int f = find_field(t[1]);
            if (f < 0) continue;
            slot_t *s = &fa->S[fa->ns];
            memset(s, 0, sizeof *s);
            s->type = t[0][0] == 's' ? 0 : 1;
            s->field = (uint8_t)f;
            s->x = (int16_t)atoi(t[2]); s->y = (int16_t)atoi(t[3]);
            s->sheet = -1;
            snprintf(s->sname, sizeof s->sname, "%s", t[4]);
            s->ink = (uint8_t)find_ink(t[5]);
            for (int i = 6; i < n; i++) {
                if (!strncmp(t[i], "ghost=", 6)) s->ghost = (uint8_t)atoi(t[i] + 6);
                else if (!strncmp(t[i], "chars=", 6)) snprintf(s->chars, sizeof s->chars, "%s", t[i] + 6);
            }
            fa->ns++;
        }
        else if (fa->ns < MAX_SLOTS && n >= 9 && !strcmp(t[0], "bar")) {
            int f = find_field(t[1]);
            if (f < 0) continue;
            slot_t *s = &fa->S[fa->ns++];
            memset(s, 0, sizeof *s);
            s->type = 2; s->field = (uint8_t)f;
            s->x = (int16_t)atoi(t[2]); s->y = (int16_t)atoi(t[3]); s->w = (int16_t)atoi(t[4]); s->h = (int16_t)atoi(t[5]);
            s->n = (int16_t)atoi(t[6]); s->gap = (int16_t)atoi(t[7]);
            s->ink = (uint8_t)find_ink(t[8]);
            if (s->n < 1) s->n = 1;
            s->rx = s->x; s->ry = s->y; s->rw = (int16_t)(s->w + s->h / 2 + 1); s->rh = s->h;
        }
    }
}

/* ---- layout: each slot's sheet and rectangle ---- */
static void resolve(face_art_t *fa)
{
    for (int i = 0; i < fa->ns; i++) {
        slot_t *s = &fa->S[i];
        if (s->type == 2) continue;
        s->sheet = (int8_t)sheet_idx(fa, s->sname);
        if (s->sheet < 0) { s->rw = 0; continue; }
        const sheet_t *h = &fa->sh[s->sheet];
        if (s->type == 1) { s->rx = s->x; s->ry = s->y; s->rw = (int16_t)h->cw; s->rh = (int16_t)h->h; continue; }
        if (!h->glyph) { s->sheet = -1; continue; }
        int w = 0;
        for (const char *c = s->chars; *c; c++) { const char *g = strchr(h->glyphs, *c); w += h->adv[g ? g - h->glyphs : 0]; }
        s->rx = (int16_t)(s->x - h->pad); s->ry = s->y; s->rw = (int16_t)(w + 2 * h->pad); s->rh = (int16_t)h->h;
    }
}
static int glyph_of(const sheet_t *h, char c)
{
    const char *g = c ? strchr(h->glyphs, c) : NULL;
    if (!g) g = strchr(h->glyphs, ' ');
    return g ? (int)(g - h->glyphs) : 0;
}
static void draw_digits(face_art_t *fa, uint16_t *dst, const slot_t *s, const char *txt, uint32_t c, int k)
{
    const sheet_t *h = &fa->sh[s->sheet];
    int x = s->x, len = (int)strlen(s->chars);
    for (int i = 0; i < len; i++) {
        char ch = txt[i];
        if (!ch) break;
        int gi = glyph_of(h, ch);
        if (gi * h->cw + h->cw <= h->pw) blend(fa, dst, h->px, h->pw, gi * h->cw, 0, h->cw, h->h, x - h->pad, s->y, c, k);
        x += h->adv[gi];
    }
}
static void draw_bar(face_art_t *fa, uint16_t *dst, const slot_t *s, int lit, uint32_t col, bool unlit_only)
{
    int seg = (s->w - (s->n - 1) * s->gap) / s->n, lean = s->h / 2;
    uint16_t c = rgb565(col);
    for (int i = 0; i < s->n; i++) {
        bool on = i < lit;
        if (unlit_only == on) continue;
        int x0 = s->x + i * (seg + s->gap);
        for (int y = 0; y < s->h; y++) {
            int ty = s->y + y;
            if (ty < 0 || ty >= fa->h) continue;
            int sh = lean * (s->h - 1 - y) / (s->h > 1 ? s->h - 1 : 1);
            uint16_t *o = dst + (size_t)ty * fa->w;
            for (int x = x0 + sh; x < x0 + sh + seg; x++) if (x >= fa->cx0 && x < fa->cx1) o[x] = unlit_only ? mix(o[x], col, 40) : c;
        }
    }
}
static const uint32_t *scheme(const face_art_t *fa) { return s_schemes[fa->warn ? FA_RED : fa->scheme]; }

/* the background: ground, the layers, the unlit segments of every digit slot and bar */
static void compose(face_art_t *fa)
{
    const uint32_t *sc = scheme(fa);
    uint16_t g = rgb565(sc[I_BG]);
    size_t tot = (size_t)fa->w * fa->h;
    for (size_t i = 0; i < tot; i++) fa->bg[i] = g;
    zip_begin(fa);
    for (int i = 0; i < fa->nl; i++) {
        const layer_t *L = &fa->L[i];
        if ((L->role == R_HEAD && fa->warn) || (L->role == R_WARN && !fa->warn)) continue;
        int w, h;
        uint8_t *b = L->len ? zip_read(fa, L->off, L->len) : NULL;
        if (!b) continue;
        uint8_t *px = decode(b, L->len, &w, &h);
        psram_free(b);
        if (!px) continue;
        blend(fa, fa->bg, px, w, 0, 0, w, h, L->x, L->y, sc[s_role_ink[L->role]], 256);
        psram_free(px);
    }
    zip_end(fa);
    for (int i = 0; i < fa->ns; i++) {
        const slot_t *s = &fa->S[i];
        if (s->type == 0 && s->sheet >= 0 && s->ghost) {
            char t[16];
            snprintf(t, sizeof t, "%s", s->chars);
            draw_digits(fa, fa->bg, s, t, sc[s->ink], 26);
        }
        if (s->type == 2) draw_bar(fa, fa->bg, s, 0, sc[s->ink], true);
    }
    if (fa->round) {                                      /* outside the glass: black */
        int r = fa->w / 2;
        for (int y = 0; y < fa->h; y++) {
            int dy = y - fa->h / 2;
            for (int x = 0; x < fa->w; x++) { int dx = x - r; if (dx * dx + dy * dy > r * r) fa->bg[(size_t)y * fa->w + x] = 0; }
        }
    }
}
/* x of glyph i of a digit slot (its cell's left edge, pad included) */
static int glyph_x(const face_art_t *fa, const slot_t *s, const char *txt, int i)
{
    const sheet_t *h = &fa->sh[s->sheet];
    int x = s->x;
    for (int k = 0; k < i && txt[k]; k++) x += h->adv[glyph_of(h, txt[k])];
    return x - h->pad;
}
/* draw a slot again over its background; old (the value shown, or NULL): a digit slot then redraws only the glyphs that changed (and the
 * background under them), *x0 / *x1 get the columns touched */
static void draw_slot(face_art_t *fa, const slot_t *s, const char *v, const char *old, int *x0o, int *x1o)
{
    int rx0 = s->rx, rx1 = s->rx + s->rw;
    if (!s->rw) { *x0o = *x1o = 0; return; }
    if (s->type == 0 && old) {                            /* fixed format: same positions, so only the changed run of glyphs */
        int len = (int)strlen(s->chars), a = -1, b = -1;
        for (int i = 0; i < len; i++) if (v[i] != old[i] || !v[i]) { if (a < 0) a = i; b = i; if (!v[i] || !old[i]) { b = len - 1; break; } }
        if (a < 0) { *x0o = *x1o = 0; return; }
        const sheet_t *h = &fa->sh[s->sheet];
        rx0 = glyph_x(fa, s, v, a) + (a > 0 ? h->pad : 0);
        rx1 = glyph_x(fa, s, v, b) + h->adv[glyph_of(h, v[b])] + (b + 1 < len ? h->pad : 2 * h->pad);
        if (rx0 < s->rx) rx0 = s->rx;
        if (rx1 > s->rx + s->rw) rx1 = s->rx + s->rw;
    }
    if (rx0 < 0) rx0 = 0;
    if (rx1 > fa->w) rx1 = fa->w;
    for (int y = 0; y < s->rh; y++) {                     /* the background under the slot back first */
        int ty = s->ry + y;
        if (ty < 0 || ty >= fa->h) continue;
        if (rx1 > rx0) memcpy(fa->fb + (size_t)ty * fa->w + rx0, fa->bg + (size_t)ty * fa->w + rx0, (size_t)(rx1 - rx0) * 2);
    }
    const uint32_t *sc = scheme(fa);
    fa->cx0 = rx0; fa->cx1 = rx1;
    if (s->type == 0) draw_digits(fa, fa->fb, s, v, sc[s->ink], 256);
    else if (s->type == 1) {
        const sheet_t *h = &fa->sh[s->sheet];
        int i = atoi(v);
        if (i < 0) i = 0;
        if (i >= h->n) i = h->n - 1;
        if ((i + 1) * h->h <= h->ph) blend(fa, fa->fb, h->px, h->pw, 0, i * h->h, h->cw, h->h, s->x, s->y, sc[s->ink], 256);
    }
    else draw_bar(fa, fa->fb, s, atoi(v), sc[s->ink], false);
    fa->cx0 = 0; fa->cx1 = fa->w;
    *x0o = rx0; *x1o = rx1;
}
static void inval(face_art_t *fa, int x, int y, int w, int h)
{
    lv_area_t a;
    lv_obj_get_coords(fa->img, &a);
    lv_area_t r = { a.x1 + x, a.y1 + y, a.x1 + x + w - 1, a.y1 + y + h - 1 };
    lv_obj_invalidate_area(fa->img, &r);
    s_redraw_px += (uint32_t)(w * h);
}

/* ---- life ---- */
static void on_del(lv_event_t *e)
{
    face_art_t *fa = lv_event_get_user_data(e);
    lv_image_cache_drop(&fa->dsc);
    for (int i = 0; i < fa->nsh; i++) psram_free(fa->sh[i].px);
    psram_free(fa->fb);
    psram_free(fa->bg);
    psram_free(fa);
}
static bool name_ok(const char *n)
{
    size_t l = n ? strlen(n) : 0;
    if (!l || l >= 16) return false;
    for (size_t i = 0; i < l; i++) if (!((n[i] >= 'a' && n[i] <= 'z') || (n[i] >= '0' && n[i] <= '9') || n[i] == '_' || n[i] == '-')) return false;
    return true;
}
static void zip_close(face_art_t *fa, zdir_t *z)           /* an open that failed: everything it took */
{
    if (z->e) pc_zip_free(z->e, z->n);
    zip_end(fa);
    psram_free(fa);
}
face_art_t *face_art_open(lv_obj_t *box, const char *name)
{
    if (!name_ok(name)) return NULL;
    face_art_t *fa = psram_calloc(1, sizeof *fa);
    if (!fa) return NULL;
    fa->mem = sizeof *fa;
    zdir_t z = { NULL, 0 };
    if (!zip_begin(fa) || pc_zip_open(&fa->zr, &z.e, &z.n)) { zip_close(fa, &z); return NULL; }
    snprintf(fa->dir, sizeof fa->dir, "%s", name);
    uint32_t n;
    char *txt = (char *)zip_file(fa, &z, fa->dir, "face.txt", &n);
    if (!txt) { zip_close(fa, &z); return NULL; }
    parse_face(fa, txt);
    psram_free(txt);
    for (int i = 0; i < fa->nl; i++) {                    /* where each layer sits in the zip */
        char p[64];
        snprintf(p, sizeof p, "%s/%s", fa->dir, fa->L[i].file);
        if (!zip_entry(fa, &z, p, &fa->L[i].off, &fa->L[i].len)) fa->L[i].len = 0;
    }
    txt = (char *)zip_file(fa, &z, "sheets", "sheets.txt", &n);
    if (txt) { parse_sheets(fa, &z, txt); psram_free(txt); }
    pc_zip_free(z.e, z.n);
    zip_end(fa);
    resolve(fa);
    int bw = (int)lv_obj_get_width(box), bh = (int)lv_obj_get_height(box);
    if (fa->w <= 0 || fa->h <= 0 || fa->w > 1024 || fa->h > 1024) { fa->w = bw; fa->h = bh; }
    size_t px = (size_t)fa->w * fa->h;
    fa->fb = psram_malloc(px * 2);
    fa->bg = psram_malloc(px * 2);
    if (!fa->fb || !fa->bg) {
        for (int i = 0; i < fa->nsh; i++) psram_free(fa->sh[i].px);
        psram_free(fa->fb); psram_free(fa->bg);
        zdir_t none = { NULL, 0 };
        zip_close(fa, &none);
        return NULL;
    }
    fa->mem += (uint32_t)(px * 4);
    fa->scheme = -1;
    fa->cx0 = 0; fa->cx1 = fa->w;
    fa->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    fa->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    fa->dsc.header.w = (uint32_t)fa->w;
    fa->dsc.header.h = (uint32_t)fa->h;
    fa->dsc.header.stride = (uint32_t)fa->w * 2;
    fa->dsc.data = (const uint8_t *)fa->fb;
    fa->dsc.data_size = (uint32_t)(px * 2);
    memset(fa->fb, 0, px * 2);
    fa->img = lv_image_create(box);
    lv_obj_remove_flag(fa->img, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(fa->img, &fa->dsc);
    lv_obj_center(fa->img);
    lv_obj_add_event_cb(fa->img, on_del, LV_EVENT_DELETE, fa);
    return fa;
}

void face_art_set(face_art_t *fa, const face_vals_t *v, int sc)
{
    if (!fa || !v) return;
    if (sc < 0 || sc >= FA__SCHEMES) sc = 0;
    if (!fa->drawn || sc != fa->scheme || v->warn != fa->warn) {      /* a new look: the background again, every slot */
        fa->scheme = sc;
        fa->warn = v->warn;
        compose(fa);
        memcpy(fa->fb, fa->bg, (size_t)fa->w * fa->h * 2);
        for (int i = 0; i < fa->ns; i++) { int a, b; draw_slot(fa, &fa->S[i], v->v[fa->S[i].field], NULL, &a, &b); }
        fa->cur = *v;
        fa->drawn = true;
        lv_image_cache_drop(&fa->dsc);
        inval(fa, 0, 0, fa->w, fa->h);
        return;
    }
    for (int i = 0; i < fa->ns; i++) {
        const slot_t *s = &fa->S[i];
        if (!strcmp(fa->cur.v[s->field], v->v[s->field])) continue;
        int x0, x1;
        draw_slot(fa, s, v->v[s->field], fa->cur.v[s->field], &x0, &x1);
        if (x1 > x0) inval(fa, x0, s->ry, x1 - x0, s->rh);
    }
    fa->cur = *v;
}
