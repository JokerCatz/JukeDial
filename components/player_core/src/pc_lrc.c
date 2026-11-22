// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_lrc.h"
#include <string.h>

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* number of bytes of the valid UTF-8 character at s (n bytes left), 0 when invalid */
static int utf8_len(const unsigned char *s, size_t n)
{
    if (s[0] < 0x80) return 1;
    int k = s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : s[0] >= 0xC2 ? 2 : 0;
    if (s[0] > 0xF4 || !k || (size_t)k > n) return 0;
    for (int i = 1; i < k; i++) if ((s[i] & 0xC0) != 0x80) return 0;
    if (k == 3 && ((s[0] == 0xE0 && s[1] < 0xA0) || (s[0] == 0xED && s[1] >= 0xA0))) return 0;   /* overlong, surrogates */
    if (k == 4 && ((s[0] == 0xF0 && s[1] < 0x90) || (s[0] == 0xF4 && s[1] >= 0x90))) return 0;
    return k;
}

/* "[mm:ss.xx]" at p (p[0] == '['): the time in ms and the length of the tag; false when it is not a time tag */
static bool time_tag(const char *p, const char *end, int32_t *ms, int *len)
{
    const char *q = p + 1;
    int mm = 0, nm = 0;
    while (q < end && is_digit(*q) && nm < 3) { mm = mm * 10 + (*q - '0'); q++; nm++; }
    if (!nm || q >= end || *q != ':') return false;
    q++;
    int ss = 0, ns = 0;
    while (q < end && is_digit(*q) && ns < 2) { ss = ss * 10 + (*q - '0'); q++; ns++; }
    if (!ns) return false;
    int frac = 0;
    if (q < end && (*q == '.' || *q == ':')) {
        q++;
        int nf = 0, scale = 100;
        while (q < end && is_digit(*q) && nf < 3) { frac += (*q - '0') * scale; scale /= 10; q++; nf++; }
        if (!nf) return false;
    }
    if (q >= end || *q != ']') return false;
    *ms = (int32_t)((mm * 60 + ss) * 1000 + frac);
    *len = (int)(q + 1 - p);
    return true;
}

static bool offset_tag(const char *p, const char *end, int32_t *off, int *len)
{
    static const char K[] = "[offset:";
    const size_t kl = sizeof K - 1;
    if ((size_t)(end - p) < kl + 2) return false;
    for (size_t i = 0; i < kl; i++) { char c = p[i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != K[i]) return false; }
    const char *q = p + kl;
    while (q < end && *q == ' ') q++;
    int sign = 1;
    if (q < end && (*q == '+' || *q == '-')) { sign = *q == '-' ? -1 : 1; q++; }
    long v = 0;
    int nd = 0;
    while (q < end && is_digit(*q) && nd < 7) { v = v * 10 + (*q - '0'); q++; nd++; }
    while (q < end && *q == ' ') q++;
    if (!nd || q >= end || *q != ']') return false;
    *off = (int32_t)(sign * v);
    *len = (int)(q + 1 - p);
    return true;
}

int pc_lrc_parse(pc_lrc_t *l, const char *src, size_t n)
{
    if (!l) return PC_LRC_E_ARG;
    memset(l, 0, sizeof *l);
    if (!src) return PC_LRC_E_ARG;
    if (n > PC_LRC_MAX_FILE) { n = PC_LRC_MAX_FILE; l->truncated = true; }
    if (n >= 3 && (uint8_t)src[0] == 0xEF && (uint8_t)src[1] == 0xBB && (uint8_t)src[2] == 0xBF) { src += 3; n -= 3; }
    const char *p = src, *end = src + n;
    int32_t offset = 0;
    int bad = 0;
    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\n' && *eol != '\r') eol++;
        const char *q = p;
        while (q < eol && (*q == ' ' || *q == '\t')) q++;
        int32_t times[16];
        int nt = 0;
        bool drop_line = false;
        while (q < eol && *q == '[') {
            int32_t ms;
            int len;
            if (time_tag(q, eol, &ms, &len)) { if (nt < 16) times[nt++] = ms; q += len; }
            else if (offset_tag(q, eol, &ms, &len)) { offset = ms; q += len; }
            else if (nt == 0) {                                  /* [ti:..] [ar:..] and anything else in front of the times: skipped; a line of only such tags has no time */
                const char *e = memchr(q, ']', (size_t)(eol - q));
                if (!e) { drop_line = true; break; }
                q = e + 1;
            } else break;                                        /* after a time: "[Chorus]" is part of the text */
        }
        if (!drop_line && nt > 0) {
            /* the text: valid UTF-8 only, enhanced-LRC <mm:ss.xx> removed, at most PC_LRC_LINE_BYTES, trailing space trimmed */
            char tmp[PC_LRC_LINE_BYTES + 1];
            int tl = 0;
            const char *t = q;
            while (t < eol && tl < PC_LRC_LINE_BYTES) {
                if (*t == '<') {                                  /* a word time */
                    int32_t ms; int len;
                    char fake[16];
                    const char *e = memchr(t, '>', (size_t)(eol - t));
                    if (e && e - t < 14) { memcpy(fake, t, (size_t)(e - t + 1)); fake[0] = '['; fake[e - t] = ']'; if (time_tag(fake, fake + (e - t + 1), &ms, &len)) { t = e + 1; continue; } }
                }
                const int k = utf8_len((const unsigned char *)t, (size_t)(eol - t));
                if (!k) { bad++; t++; continue; }
                if (tl + k > PC_LRC_LINE_BYTES) break;
                if ((unsigned char)*t < 0x20) { t += k; continue; }   /* a control character */
                memcpy(tmp + tl, t, (size_t)k);
                tl += k; t += k;
            }
            if (t < eol) l->truncated = true;
            while (tl > 0 && (tmp[tl - 1] == ' ' || tmp[tl - 1] == '\t')) tl--;
            int s0 = 0;
            while (s0 < tl && (tmp[s0] == ' ' || tmp[s0] == '\t')) s0++;
            if (l->text_len + (tl - s0) + 1 > PC_LRC_TEXT_BYTES) l->truncated = true;
            else {
                const uint16_t off = l->text_len;
                memcpy(l->text + off, tmp + s0, (size_t)(tl - s0));
                l->text[off + (tl - s0)] = 0;
                l->text_len = (uint16_t)(off + (tl - s0) + 1);
                for (int i = 0; i < nt; i++) {
                    if (l->n >= PC_LRC_MAX_LINES) { l->truncated = true; break; }
                    l->line[l->n].ms = times[i];
                    l->line[l->n].off = off;
                    l->n++;
                }
            }
        }
        p = eol;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
    }
    if (bad >= 3) { memset(l, 0, sizeof *l); return PC_LRC_E_ENCODING; }   /* a GBK / Big5 file: several invalid sequences */
    if (!l->n) return PC_LRC_E_EMPTY;
    for (int i = 0; i < l->n; i++) { int64_t v = (int64_t)l->line[i].ms - offset; l->line[i].ms = v < 0 ? 0 : v > 0x7FFFFFFF ? 0x7FFFFFFF : (int32_t)v; }
    for (int i = 1; i < l->n; i++) {                              /* stable insertion sort by time */
        const pc_lrc_line_t x = l->line[i];
        int j = i - 1;
        while (j >= 0 && l->line[j].ms > x.ms) { l->line[j + 1] = l->line[j]; j--; }
        l->line[j + 1] = x;
    }
    return PC_LRC_OK;
}

int pc_lrc_find(const pc_lrc_t *l, int32_t pos_ms)
{
    if (!l || !l->n || pos_ms < l->line[0].ms) return -1;
    int lo = 0, hi = l->n - 1;
    while (lo < hi) { int mid = (lo + hi + 1) / 2; if (l->line[mid].ms <= pos_ms) lo = mid; else hi = mid - 1; }
    return lo;
}

const char *pc_lrc_text(const pc_lrc_t *l, int i) { return l && i >= 0 && i < l->n ? l->text + l->line[i].off : ""; }

bool pc_lrc_path(const char *audio, char *out, size_t cap)
{
    if (!audio || !*audio || !out) return false;
    const size_t n = strlen(audio);
    size_t dot = n;
    for (size_t i = n; i > 0; i--) { if (audio[i - 1] == '/') break; if (audio[i - 1] == '.') { dot = i - 1; break; } }
    if (dot == 0 || audio[dot - 1] == '/') dot = n;               /* ".hidden" has no extension */
    if (dot + 5 > cap) return false;
    memcpy(out, audio, dot);
    memcpy(out + dot, ".lrc", 5);
    return true;
}
