// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extkeys.h"
#include <string.h>
#include "pc/pc_i18n.h"

/* keyboard usage page 0x07 */
#define MOD_CTRL 0x01
#define MOD_SHIFT 0x02
#define MOD_ALT 0x04
#define MOD_GUI 0x08

int pc_extkeys_ascii_usage(char c, uint8_t *mod)
{
    static const char shifted[] = ")!@#$%^&*(";
    uint8_t m = 0;
    int u = 0;
    if (c >= 'a' && c <= 'z') u = 0x04 + (c - 'a');
    else if (c >= 'A' && c <= 'Z') { u = 0x04 + (c - 'A'); m = MOD_SHIFT; }
    else if (c >= '1' && c <= '9') u = 0x1E + (c - '1');
    else if (c == '0') u = 0x27;
    else if (c == '\n') u = 0x28;
    else if (c == '\t') u = 0x2B;
    else if (c == ' ') u = 0x2C;
    else {
        const char *p = c ? strchr(shifted, c) : NULL;
        if (p) { u = p[0] == ')' ? 0x27 : 0x1E + (int)(p - shifted) - 1; m = MOD_SHIFT; }
        else {
            static const struct { char c; uint8_t u, s; } t[] = {
                { '-', 0x2D, 0 }, { '_', 0x2D, 1 }, { '=', 0x2E, 0 }, { '+', 0x2E, 1 }, { '[', 0x2F, 0 }, { '{', 0x2F, 1 },
                { ']', 0x30, 0 }, { '}', 0x30, 1 }, { '\\', 0x31, 0 }, { '|', 0x31, 1 }, { ';', 0x33, 0 }, { ':', 0x33, 1 },
                { '\'', 0x34, 0 }, { '"', 0x34, 1 }, { '`', 0x35, 0 }, { '~', 0x35, 1 }, { ',', 0x36, 0 }, { '<', 0x36, 1 },
                { '.', 0x37, 0 }, { '>', 0x37, 1 }, { '/', 0x38, 0 }, { '?', 0x38, 1 } };
            for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
                if (t[i].c == c) { u = t[i].u; m = t[i].s ? MOD_SHIFT : 0; break; }
        }
    }
    if (mod) *mod = m;
    return u;
}

void pc_extkeys_default(pc_extkeys_cfg_t *out)
{
    static const struct { const char *n; uint16_t c; } k[] = { { "播放", 0xCD }, { "上首", 0xB6 }, { "下首", 0xB5 }, { "靜音", 0xE2 } };
    memset(out, 0, sizeof *out);
    out->nlayers = 1;
    pc_extkeys_layer_t *l = &out->layers[0];
    strcpy(l->name, "媒體鍵");
    l->knob = PC_KNOB_VOLUME;
    l->nkeys = 4;
    for (int i = 0; i < 4; i++) {
        strcpy(l->keys[i].name, k[i].n);
        l->keys[i].act = PC_XACT_CONSUMER;
        l->keys[i].code = k[i].c;
    }
}

const char *pc_extkeys_err_text(int code)
{
    static const char *const t[PC_XERR_COUNT] = {
        "", "檔案太大", "有不能用的字元", "看不懂的開頭", "鍵出現在 layer 之前", "層太多（最多 8）", "這層鍵太多（最多 8）",
        "層名太長或空白（最多 12 位元組）", "鍵名太長或空白（最多 23 位元組）", "knob 要是 volume/scroll/arrows/none",
        "動作要是 consumer/combo/text/macro", "consumer 碼要在 0x01..0x3FF", "按鍵組合看不懂", "text 只能是 ASCII",
        "text 超過 64 位元組", "text 的引號沒配好", "macro 超過 16 步", "macro 的步驟看不懂", "wait 要在 0..2000 ms", "這層沒有鍵" };
    return (code > 0 && code < PC_XERR_COUNT) ? TZ(t[code]) : "?";   /* the zh-TW text above in 繁中, its data.tsv row in 簡中 */
}

int pc_extkeys_err_fmt(const pc_extkeys_err_t *e, char *buf, size_t n)
{
    if (!buf || n == 0) return 0;
    char num[8];
    size_t nl = 0;
    buf[0] = 0;
    if (e->line) {
        unsigned v = e->line;
        char r[6];
        int rl = 0;
        while (v && rl < 5) { r[rl++] = (char)('0' + v % 10); v /= 10; }
        while (rl) num[nl++] = r[--rl];
        num[nl++] = 0;
    }
    const char *txt = pc_extkeys_err_text(e->code);
    size_t o = 0;
    if (e->line) {
        for (const char *p = num; *p && o + 1 < n; p++) buf[o++] = *p;
        static const char colon[] = "：";
        for (const char *p = colon; *p && o + 1 < n; p++) buf[o++] = *p;
    }
    for (const char *p = txt; *p && o + 1 < n; p++) buf[o++] = *p;
    buf[o] = 0;
    return (int)o;
}

/* ---- parser ---- */
typedef struct {
    pc_extkeys_cfg_t *cfg;
    pc_extkeys_res_t *res;
    int line;
    uint16_t layer_line[PC_EXTKEYS_LAYERS];
} ctx_t;

static void err(ctx_t *c, int code)
{
    if (c->res->total < 0xFFFF) c->res->total++;
    if (c->res->nerr < PC_EXTKEYS_ERRS) {
        c->res->err[c->res->nerr].line = (uint16_t)c->line;
        c->res->err[c->res->nerr].code = (uint8_t)code;
        c->res->nerr++;
    }
}

static bool is_sp(char ch) { return ch == ' ' || ch == '\t'; }

/* the first whitespace-delimited word of [*p, end): returns its length, advances *p past it and the spaces after */
static size_t word(const char **p, const char *end, const char **w)
{
    while (*p < end && is_sp(**p)) (*p)++;
    *w = *p;
    while (*p < end && !is_sp(**p)) (*p)++;
    size_t n = (size_t)(*p - *w);
    while (*p < end && is_sp(**p)) (*p)++;
    return n;
}

/* strict UTF-8 (no overlongs, no surrogates, <= U+10FFFF): names are drawn by LVGL */
static bool utf8_ok(const char *w, size_t n)
{
    const unsigned char *s = (const unsigned char *)w;
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t k;
        unsigned cp;
        if (c < 0x80) { i++; continue; }
        else if (c >= 0xC2 && c <= 0xDF) { k = 1; cp = c & 0x1F; }
        else if (c >= 0xE0 && c <= 0xEF) { k = 2; cp = c & 0x0F; }
        else if (c >= 0xF0 && c <= 0xF4) { k = 3; cp = c & 0x07; }
        else return false;
        if (i + k >= n) return false;   /* truncated sequence */
        if (i + k >= n) return false;   /* truncated sequence */
        for (size_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (s[i + j] & 0x3F);
        }
        if ((k == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (k == 3 && (cp < 0x10000 || cp > 0x10FFFF))) return false;
        i += k + 1;
    }
    return true;
}

static bool weq(const char *w, size_t n, const char *lit) { return strlen(lit) == n && !memcmp(w, lit, n); }

static bool parse_uint(const char *w, size_t n, unsigned max, unsigned *out)
{
    if (n == 0 || n > 8) return false;
    unsigned base = 10, v = 0;
    size_t i = 0;
    if (n > 2 && w[0] == '0' && (w[1] == 'x' || w[1] == 'X')) { base = 16; i = 2; }
    for (; i < n; i++) {
        char ch = w[i];
        unsigned d;
        if (ch >= '0' && ch <= '9') d = (unsigned)(ch - '0');
        else if (base == 16 && ch >= 'a' && ch <= 'f') d = (unsigned)(ch - 'a' + 10);
        else if (base == 16 && ch >= 'A' && ch <= 'F') d = (unsigned)(ch - 'A' + 10);
        else return false;
        v = v * base + d;
        if (v > max) return false;
    }
    *out = v;
    return true;
}

/* one key name (lower case) -> keyboard usage; 0 = unknown */
static int key_usage(const char *w, size_t n)
{
    static const struct { const char *n; uint8_t u; } t[] = {
        { "enter", 0x28 }, { "return", 0x28 }, { "esc", 0x29 }, { "escape", 0x29 }, { "backspace", 0x2A }, { "tab", 0x2B },
        { "space", 0x2C }, { "plus", 0x2E }, { "minus", 0x2D }, { "comma", 0x36 }, { "dot", 0x37 }, { "slash", 0x38 },
        { "capslock", 0x39 }, { "printscreen", 0x46 }, { "insert", 0x49 }, { "home", 0x4A }, { "pageup", 0x4B }, { "pgup", 0x4B },
        { "delete", 0x4C }, { "del", 0x4C }, { "end", 0x4D }, { "pagedown", 0x4E }, { "pgdn", 0x4E }, { "right", 0x4F },
        { "left", 0x50 }, { "down", 0x51 }, { "up", 0x52 } };
    if (n == 1 && w[0] != '+' && w[0] != ',') {
        char ch = w[0];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        uint8_t m;
        int u = pc_extkeys_ascii_usage(ch, &m);
        if (u && !m && ch != '\n' && ch != '\t' && ch != ' ') return u;
        return 0;
    }
    if ((w[0] == 'f' || w[0] == 'F') && n >= 2 && n <= 3) {
        unsigned v;
        if (parse_uint(w + 1, n - 1, 12, &v) && v >= 1 && w[1] != '0') return 0x3A + (int)v - 1;
        return 0;
    }
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        size_t l = strlen(t[i].n);
        if (l != n) continue;
        size_t j = 0;
        for (; j < n; j++) {
            char ch = w[j];
            if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
            if (ch != t[i].n[j]) break;
        }
        if (j == n) return t[i].u;
    }
    return 0;
}

static int mod_bit(const char *w, size_t n)
{
    static const struct { const char *n; uint8_t m; } t[] = { { "ctrl", MOD_CTRL }, { "control", MOD_CTRL }, { "shift", MOD_SHIFT },
        { "alt", MOD_ALT }, { "gui", MOD_GUI }, { "win", MOD_GUI }, { "cmd", MOD_GUI }, { "meta", MOD_GUI } };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        size_t l = strlen(t[i].n);
        if (l != n) continue;
        size_t j = 0;
        for (; j < n; j++) {
            char ch = w[j];
            if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
            if (ch != t[i].n[j]) break;
        }
        if (j == n) return t[i].m;
    }
    return 0;
}

/* "ctrl+shift+c": every part but the last a modifier; the last a key or a modifier (modifiers only). Spaces around parts are not allowed. */
static bool parse_combo(const char *w, size_t n, uint8_t *mod, uint16_t *usage)
{
    uint8_t m = 0;
    uint16_t u = 0;
    size_t i = 0;
    if (n == 0) return false;
    while (i <= n) {
        size_t j = i;
        while (j < n && w[j] != '+') j++;
        size_t pl = j - i;
        if (pl == 0) {
            /* "+" as the last part spelled literally is not allowed (use "plus"); empty part = malformed */
            return false;
        }
        bool last = (j == n);
        int mb = mod_bit(w + i, pl);
        if (mb) { m = (uint8_t)(m | mb); }
        else if (last) { int k = key_usage(w + i, pl); if (!k) return false; u = (uint16_t)k; }
        else return false;
        if (last) break;
        i = j + 1;
        if (i >= n) return false;   /* trailing '+' */
    }
    *mod = m;
    *usage = u;
    return true;
}

/* quoted text at [*p,end): returns 0 ok / error code; the content (unescaped) into dst (cap PC_EXTKEYS_TEXT_MAX+1), *len; *p after the closing quote */
static int parse_quoted(const char **p, const char *end, char *dst, uint8_t *len)
{
    const char *s = *p;
    size_t n = 0;
    if (s >= end || *s != '"') return PC_XERR_TEXT_QUOTE;
    s++;
    for (;;) {
        if (s >= end) return PC_XERR_TEXT_QUOTE;
        unsigned char ch = (unsigned char)*s++;
        if (ch == '"') break;
        if (ch == '\\') {
            if (s >= end) return PC_XERR_TEXT_QUOTE;
            char e = *s++;
            if (e == 'n') ch = '\n';
            else if (e == 't') ch = '\t';
            else if (e == '\\' || e == '"') ch = (unsigned char)e;
            else return PC_XERR_TEXT_ASCII;
        } else if (ch < 0x20 || ch > 0x7E) return PC_XERR_TEXT_ASCII;
        if (n >= PC_EXTKEYS_TEXT_MAX) return PC_XERR_TEXT_LONG;
        dst[n++] = (char)ch;
    }
    dst[n] = 0;
    *len = (uint8_t)n;
    *p = s;
    while (*p < end && is_sp(**p)) (*p)++;
    return 0;
}

static void parse_key(ctx_t *c, const char *p, const char *end)
{
    pc_extkeys_cfg_t *cfg = c->cfg;
    if (cfg->nlayers == 0) { err(c, PC_XERR_NO_LAYER); return; }
    pc_extkeys_layer_t *l = &cfg->layers[cfg->nlayers - 1];
    const char *w;
    size_t n;
    /* name: a word, or a quoted string (no escapes needed for display names) */
    char name[PC_EXTKEYS_KEY_NAME + 1];
    while (p < end && is_sp(*p)) p++;
    if (p < end && *p == '"') {
        const char *q = p + 1;
        while (q < end && *q != '"') q++;
        if (q >= end) { err(c, PC_XERR_TEXT_QUOTE); return; }
        w = p + 1; n = (size_t)(q - w); p = q + 1;
        while (p < end && is_sp(*p)) p++;
    } else n = word(&p, end, &w);
    if (n == 0 || n > PC_EXTKEYS_KEY_NAME) { err(c, PC_XERR_KEY_NAME); return; }
    if (!utf8_ok(w, n)) { err(c, PC_XERR_BAD_BYTE); return; }
    memcpy(name, w, n);
    name[n] = 0;

    pc_extkeys_key_t k;
    memset(&k, 0, sizeof k);
    memcpy(k.name, name, n + 1);
    size_t an = word(&p, end, &w);
    int bad = 0;
    if (weq(w, an, "consumer")) {
        const char *v;
        size_t vn = word(&p, end, &v);
        unsigned u;
        if (!parse_uint(v, vn, 0x3FF, &u) || u == 0 || p < end) bad = PC_XERR_CONSUMER;
        else { k.act = PC_XACT_CONSUMER; k.code = (uint16_t)u; }
    } else if (weq(w, an, "combo")) {
        const char *v;
        size_t vn = word(&p, end, &v);
        if (!parse_combo(v, vn, &k.mod, &k.code) || p < end) bad = PC_XERR_COMBO;
        else k.act = PC_XACT_COMBO;
    } else if (weq(w, an, "text")) {
        bad = parse_quoted(&p, end, k.u.text, &k.n);
        if (!bad && p < end) bad = PC_XERR_TEXT_QUOTE;
        if (!bad) k.act = PC_XACT_TEXT;
    } else if (weq(w, an, "macro")) {
        k.act = PC_XACT_MACRO;
        /* steps separated by commas: combo or "wait N" */
        for (;;) {
            const char *s = p;
            while (p < end && *p != ',') p++;
            const char *e = p;
            while (s < e && is_sp(*s)) s++;
            while (e > s && is_sp(e[-1])) e--;
            if (k.n >= PC_EXTKEYS_MACRO_MAX) { bad = PC_XERR_MACRO_LONG; break; }
            const char *sw;
            const char *sp = s;
            size_t sn = word(&sp, e, &sw);
            pc_extkeys_step *st = &k.u.steps[k.n];
            if (sn == 0) { bad = PC_XERR_MACRO_STEP; break; }
            if (weq(sw, sn, "wait")) {
                const char *v;
                size_t vn = word(&sp, e, &v);
                unsigned ms;
                if (!parse_uint(v, vn, 0xFFFFFF, &ms) || sp < e) { bad = PC_XERR_WAIT; break; }
                if (ms > PC_EXTKEYS_WAIT_MAX) { bad = PC_XERR_WAIT; break; }
                st->type = PC_XSTEP_WAIT;
                st->val = (uint16_t)ms;
            } else {
                uint8_t m;
                uint16_t u;
                if (sp < e || !parse_combo(sw, sn, &m, &u)) { bad = PC_XERR_MACRO_STEP; break; }
                st->type = PC_XSTEP_COMBO;
                st->mod = m;
                st->val = u;
            }
            k.n++;
            if (p >= end) break;
            p++;   /* the comma */
        }
    } else bad = PC_XERR_ACTION;
    if (bad) { err(c, bad); return; }
    if (l->nkeys >= PC_EXTKEYS_KEYS) { err(c, PC_XERR_TOO_MANY_KEYS); return; }
    l->keys[l->nkeys++] = k;
}

static void parse_line(ctx_t *c, const char *s, const char *e)
{
    /* comment: '#' outside quotes */
    bool q = false;
    for (const char *p = s; p < e; p++) {
        if (q && *p == '\\') p++;   /* an escaped byte inside quotes, as parse_quoted reads it */
        else if (*p == '"') q = !q;
        else if (*p == '#' && !q) { e = p; break; }
    }
    while (e > s && (is_sp(e[-1]) || e[-1] == '\r')) e--;
    const char *p = s;
    const char *w;
    size_t n = word(&p, e, &w);
    if (n == 0) return;
    if (weq(w, n, "layer")) {
        /* a layer must end non-empty; checked when the next layer starts and at the end */
        const char *v;
        size_t vn = word(&p, e, &v);
        if (vn == 0 || vn > PC_EXTKEYS_LAYER_NAME || p < e) { err(c, PC_XERR_LAYER_NAME); return; }
        if (!utf8_ok(v, vn)) { err(c, PC_XERR_BAD_BYTE); return; }
        if (c->cfg->nlayers >= PC_EXTKEYS_LAYERS) { err(c, PC_XERR_TOO_MANY_LAYERS); return; }
        pc_extkeys_layer_t *l = &c->cfg->layers[c->cfg->nlayers++];
        memcpy(l->name, v, vn);
        l->name[vn] = 0;
        l->knob = PC_KNOB_VOLUME;
        l->nkeys = 0;
        return;
    }
    if (weq(w, n, "knob")) {
        if (c->cfg->nlayers == 0) { err(c, PC_XERR_NO_LAYER); return; }
        const char *v;
        size_t vn = word(&p, e, &v);
        pc_extkeys_layer_t *l = &c->cfg->layers[c->cfg->nlayers - 1];
        if (p < e) { err(c, PC_XERR_KNOB); return; }
        if (weq(v, vn, "volume")) l->knob = PC_KNOB_VOLUME;
        else if (weq(v, vn, "scroll")) l->knob = PC_KNOB_SCROLL;
        else if (weq(v, vn, "arrows")) l->knob = PC_KNOB_ARROWS;
        else if (weq(v, vn, "none")) l->knob = PC_KNOB_NONE;
        else err(c, PC_XERR_KNOB);
        return;
    }
    if (weq(w, n, "key")) { parse_key(c, p, e); return; }
    err(c, PC_XERR_UNKNOWN_WORD);
}

void pc_extkeys_parse(const char *buf, size_t len, pc_extkeys_cfg_t *out, pc_extkeys_res_t *res)
{
    memset(res, 0, sizeof *res);
    res->ok = true;
    memset(out, 0, sizeof *out);
    if (!buf || len == 0) { pc_extkeys_default(out); return; }
    ctx_t c = { out, res, 0, { 0 } };
    if (len > PC_EXTKEYS_FILE_MAX) {
        err(&c, PC_XERR_TOO_BIG);
    } else {
        if (len >= 3 && !memcmp(buf, "\xEF\xBB\xBF", 3)) { buf += 3; len -= 3; }   /* UTF-8 BOM from Windows editors */
        /* bytes: NUL and other control characters (except tab, CR, LF) are errors; UTF-8 (key names) passes */
        const char *p = buf, *end = buf + len;
        c.line = 0;
        while (p < end) {
            const char *nl = p;
            while (nl < end && *nl != '\n') nl++;
            c.line++;
            bool badb = false;
            for (const char *q = p; q < nl; q++)
                if ((unsigned char)*q < 0x20 && *q != '\t' && *q != '\r') { badb = true; break; }
            if (badb) err(&c, PC_XERR_BAD_BYTE);
            else {
                int nl0 = out->nlayers;
                parse_line(&c, p, nl);
                if (out->nlayers > nl0) c.layer_line[out->nlayers - 1] = (uint16_t)c.line;
            }
            p = nl < end ? nl + 1 : nl;
        }
        for (int i = 0; i < out->nlayers; i++)   /* an empty layer is a mistake, reported on its own line */
            if (out->layers[i].nkeys == 0) { c.line = c.layer_line[i]; err(&c, PC_XERR_EMPTY_LAYER); }
    }
    if (res->total) {
        res->ok = false;
        pc_extkeys_default(out);
    } else if (out->nlayers == 0) {
        pc_extkeys_default(out);   /* comments only: same as no file */
    } else res->from_file = true;
}
