// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_ts.h"
#include "pc_test.h"

/* builds a TS stream: PAT, PMT (AAC on 0x101, timed ID3 on 0x102), then PES packets */
static uint8_t ts[188 * 64];
static int nts;
static void pkt(int pid, int pusi, const uint8_t *pay, int n, int adapt)
{
    uint8_t *p = ts + 188 * nts++;
    memset(p, 0xFF, 188);
    p[0] = 0x47; p[1] = (uint8_t)((pusi ? 0x40 : 0) | (pid >> 8)); p[2] = (uint8_t)pid;
    int off = 4;
    if (adapt || n < 184) {                       /* adaptation field pads the payload to the end */
        int alen = 183 - n;
        p[3] = 0x30; p[4] = (uint8_t)alen;
        if (alen > 0) p[5] = 0x00;
        off = 5 + alen;
    } else p[3] = 0x10;
    memcpy(p + off, pay, (size_t)n);
}
static void psi(int pid, const uint8_t *sec, int n) { uint8_t b[184] = { 0 }; memcpy(b + 1, sec, (size_t)n); pkt(pid, 1, b, n + 1, 0); }

static void build(uint8_t *es, int *esn)
{
    nts = 0;
    const uint8_t pat[] = { 0x00, 0xB0, 0x0D, 0, 1, 0xC1, 0, 0, 0, 1, 0xF0, 0x00, 0, 0, 0, 0 };   /* program 1 -> PMT 0x1000 */
    psi(0, pat, sizeof pat);
    const uint8_t pmt[] = { 0x02, 0xB0, 0x17, 0, 1, 0xC1, 0, 0, 0xE1, 0x01, 0xF0, 0x00,
                            0x15, 0xE1, 0x02, 0xF0, 0x00,          /* timed ID3 on 0x102 */
                            0x0F, 0xE1, 0x01, 0xF0, 0x00,          /* AAC (ADTS) on 0x101 */
                            0, 0, 0, 0 };
    psi(0x1000, pmt, sizeof pmt);
    /* ID3 PES on 0x102: must not reach the output */
    uint8_t id3[40] = { 0, 0, 1, 0xBD, 0, 0, 0x80, 0, 0 };
    memcpy(id3 + 9, "ID3junkjunk", 11);
    pkt(0x102, 1, id3, 20, 0);
    /* AAC: two PES, the first spanning three packets */
    *esn = 0;
    for (int pes = 0; pes < 2; pes++) {
        uint8_t b[600];
        int n = 0;
        b[n++] = 0; b[n++] = 0; b[n++] = 1; b[n++] = 0xC0; b[n++] = 0; b[n++] = 0;
        b[n++] = 0x80; b[n++] = 0x80; b[n++] = 5; for (int i = 0; i < 5; i++) b[n++] = 0x21;   /* PTS */
        int body = pes ? 150 : 400;
        for (int i = 0; i < body; i++) { b[n] = (uint8_t)(pes * 100 + i); es[(*esn)++] = b[n]; n++; }
        for (int off = 0, first = 1; off < n; first = 0) {
            int k = n - off > 184 ? 184 : n - off;
            pkt(0x101, first, b + off, k, 0);
            off += k;
        }
    }
}

static void test_whole(void)
{
    uint8_t es[1000], out[2000];
    int esn;
    build(es, &esn);
    pc_ts_t t;
    pc_ts_init(&t);
    size_t n = pc_ts_feed(&t, ts, (size_t)(188 * nts), out, sizeof out);
    CHECK_EQ_INT(n, esn);
    CHECK(!memcmp(out, es, n));
    CHECK_EQ_INT(t.audio_pid, 0x101);
    CHECK_EQ_INT(t.stream_type, 0x0F);
}

static void test_odd_chunks(void)
{
    uint8_t es[1000], out[2000];
    int esn;
    build(es, &esn);
    pc_ts_t t;
    pc_ts_init(&t);
    size_t n = 0, total = (size_t)(188 * nts);
    for (size_t off = 0; off < total; off += 7) {           /* 7-byte pieces: packets split everywhere */
        size_t k = total - off < 7 ? total - off : 7;
        n += pc_ts_feed(&t, ts + off, k, out + n, sizeof out - n);
    }
    CHECK_EQ_INT(n, esn);
    CHECK(!memcmp(out, es, n));
}

static void test_resync(void)
{
    uint8_t es[1000], out[2000], in[188 * 64 + 5];
    int esn;
    build(es, &esn);
    memcpy(in, "junk!", 5);                                  /* joined mid-packet */
    memcpy(in + 5, ts, (size_t)(188 * nts));
    pc_ts_t t;
    pc_ts_init(&t);
    CHECK_EQ_INT(pc_ts_feed(&t, in, sizeof in - (sizeof in - 5 - 188 * (size_t)nts), out, sizeof out), esn);
}

int main(void)
{
    RUN(test_whole);
    RUN(test_odd_chunks);
    RUN(test_resync);
    return 0;
}
