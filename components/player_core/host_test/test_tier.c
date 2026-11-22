// SPDX-License-Identifier: LGPL-2.1-or-later
/* Format tiers: the one table in pc_chipseq.c, the setting source, and pc_backend_chain (AdPlug first, then libxmp, experimental readers
 * skipped while the setting is off). Both real backends are linked; the inputs are a synthetic AdPlug RAW capture and the tiny.mod fixture. */
#include <stdbool.h>
#include "pc/pc_backend.h"
#include "pc/pc_chipseq.h"
#include "pc_test.h"

static bool s_on;
static bool src(void) { return s_on; }

typedef struct { uint8_t *d; uint32_t n; } buf_t;
static void rec(buf_t *b, uint8_t param, uint8_t cmd) { b->d[b->n++] = param; b->d[b->n++] = cmd; }
static buf_t make_raw(void)                       /* AdPlug RAW capture, one OPL2 voice, about 2 s */
{
    static const uint8_t m[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
    buf_t b = { calloc(1, 400), 0 };
    memcpy(b.d, "RAWADATA", 8); b.d[8] = 0xA9; b.d[9] = 0x04; b.n = 10;
    int mo = m[0], co = m[0] + 3;
    rec(&b, 0x21, (uint8_t)(0x20 + mo)); rec(&b, 0x21, (uint8_t)(0x20 + co));
    rec(&b, 0x10, (uint8_t)(0x40 + mo)); rec(&b, 0x00, (uint8_t)(0x40 + co));
    rec(&b, 0xF0, (uint8_t)(0x60 + mo)); rec(&b, 0xF0, (uint8_t)(0x60 + co));
    rec(&b, 0x07, (uint8_t)(0x80 + mo)); rec(&b, 0x07, (uint8_t)(0x80 + co));
    rec(&b, 0x44, 0xA0); rec(&b, 0x32, 0xB0);
    for (int i = 0; i < 8; i++) rec(&b, 255, 0);
    rec(&b, 0xFF, 0xFF);
    return b;
}
static uint8_t *load(const char *name, uint32_t *len)
{
    char p[256];
    snprintf(p, sizeof p, "%s/%s", FIXTURES, name);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    *len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*len);
    CHECK(fread(b, 1, *len, f) == *len);
    fclose(f);
    return b;
}

/* opens d as a file named `name` through the chain; the backend that decoded it (NULL = refused) */
static const pc_backend_t *open_as(const uint8_t *d, uint32_t n, const char *name, void **hout)
{
    static pc_reader_t r;
    pc_reader_mem(&r, d, n);
    pc_reader_set_ext(&r, name);
    void *h = NULL;
    if (pc_backend_chain.open(&h, &r, 0)) return NULL;
    const pc_backend_t *be = pc_backend_resolve(&pc_backend_chain, h);
    if (hout) *hout = h; else pc_backend_chain.close(h);
    return be;
}

static void test_table(void)
{
    int o, x;
    pc_tier_set_source(NULL);
    CHECK(!pc_tier_experimental_on());                          /* no source = off */
    CHECK_EQ_INT(pc_ext_backends(".rad", &o, &x), 1);  CHECK(o == PC_TIER_STABLE && x == PC_TIER_NONE);
    CHECK_EQ_INT(pc_ext_backends(".A2M", &o, &x), 1);  CHECK(o == PC_TIER_EXPERIMENTAL && x == PC_TIER_NONE);
    CHECK_EQ_INT(pc_ext_backends(".mod", &o, &x), 1);  CHECK(o == PC_TIER_NONE && x == PC_TIER_STABLE);
    CHECK_EQ_INT(pc_ext_backends(".ice", &o, &x), 1);  CHECK(x == PC_TIER_EXPERIMENTAL);
    CHECK_EQ_INT(pc_ext_backends(".imf", &o, &x), 2);  CHECK(o == PC_TIER_STABLE && x == PC_TIER_STABLE);
    CHECK_EQ_INT(pc_ext_backends(".ims", &o, &x), 2);
    CHECK_EQ_INT(pc_ext_backends(".dtm", &o, &x), 2);  CHECK(o == PC_TIER_EXPERIMENTAL && x == PC_TIER_STABLE);
    CHECK_EQ_INT(pc_ext_backends(".mp3", &o, &x), 0);
    CHECK_EQ_INT(pc_ext_backends(NULL, &o, &x), 0);
    /* the experimental AdPlug set of OPLFUZZ-report.md: slow scan + most findings */
    static const char *const exp[] = { ".a2m", ".a2t", ".d00", ".hsq", ".sqx", ".sdb", ".agd", ".ha2", ".lds", ".sa2", ".dtm", ".plx", ".rix", ".xms",
                                       ".mtr", ".sat", ".dfm", ".mad", ".laa", ".sci", ".cmf", ".mdi", ".pis" };
    for (size_t i = 0; i < sizeof exp / sizeof *exp; i++) { pc_ext_backends(exp[i], &o, &x); CHECK(o == PC_TIER_EXPERIMENTAL); }
    int n_exp = 0;
    static const char *const all[] = { ".rad", ".hsc", ".hsp", ".imf", ".wlf", ".amd", ".bam", ".adl", ".ksm", ".mus", ".mdy", ".ims", ".mkj", ".cff", ".dmo", ".mtk", ".rac",
                                       ".xad", ".bmf", ".rol", ".xsm", ".dro", ".msc", ".mkf", ".got", ".sop", ".sng" };
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) { pc_ext_backends(all[i], &o, &x); if (o == PC_TIER_EXPERIMENTAL) n_exp++; CHECK(o == PC_TIER_STABLE); }
    CHECK_EQ_INT(n_exp, 0);
    /* the gates */
    s_on = false;
    pc_tier_set_source(src);
    CHECK(pc_ext_blocked(".a2m") && pc_ext_scan_blind(".a2m") && pc_ext_has_gated(".a2m"));
    CHECK(pc_ext_blocked(".ice"));
    CHECK(!pc_ext_blocked(".dtm") && pc_ext_has_gated(".dtm") && pc_ext_scan_blind(".dtm"));   /* libxmp may still play it; the scan stays blind (one reader is gated) */
    CHECK(!pc_ext_blocked(".rad") && !pc_ext_scan_blind(".rad") && !pc_ext_has_gated(".rad"));
    CHECK(!pc_ext_blocked(".imf") && !pc_ext_scan_blind(".imf"));
    CHECK(!pc_ext_blocked(".mod") && !pc_ext_blocked(".mp3") && !pc_ext_blocked(NULL));
    s_on = true;
    CHECK(!pc_ext_blocked(".a2m") && !pc_ext_scan_blind(".a2m") && !pc_ext_has_gated(".dtm"));
    CHECK(pc_tier_allowed(PC_TIER_EXPERIMENTAL) && pc_tier_allowed(PC_TIER_STABLE) && !pc_tier_allowed(PC_TIER_NONE));
    pc_tier_set_source(NULL);
}

static void test_chain_order_and_gate(void)
{
    uint32_t ml;
    uint8_t *mod = load("tiny.mod", &ml);
    buf_t raw = make_raw();
    pc_tier_set_source(src);
    for (int on = 0; on < 2; on++) {
        s_on = on;
        CHECK(open_as(raw.d, raw.n, "a.rad", NULL) == &pc_backend_opl);            /* a stable AdPlug suffix */
        CHECK(open_as(raw.d, raw.n, "a.imf", NULL) == &pc_backend_opl);            /* both readers stable: AdPlug first (its content probe takes it) */
        CHECK(open_as(raw.d, raw.n, "a.ims", NULL) == &pc_backend_opl);
        CHECK(open_as(mod, ml, "a.mod", NULL) == &pc_backend_xmp);
        CHECK(open_as(mod, ml, "a.imf", NULL) == &pc_backend_xmp);                 /* AdPlug refuses the content: libxmp is tried next */
        CHECK(open_as(mod, ml, "a.dtm", NULL) == &pc_backend_xmp);                 /* the AdPlug reader is experimental (skipped when off, refused when on) */
        CHECK(open_as(mod, ml, "a.zzz", NULL) == NULL);                            /* no backend lists it */
        /* an experimental-only suffix: refused while off (nothing is opened), played when on */
        const pc_backend_t *a2m = open_as(raw.d, raw.n, "a.a2m", NULL);
        const pc_backend_t *ice = open_as(mod, ml, "a.ice", NULL);
        if (on) { CHECK(a2m == &pc_backend_opl); CHECK(ice == &pc_backend_xmp); }
        else { CHECK(a2m == NULL); CHECK(ice == NULL); }
    }
    /* the chain forwards everything to the backend that opened */
    s_on = false;
    void *h;
    CHECK(open_as(mod, ml, "a.mod", &h) == &pc_backend_xmp);
    CHECK_EQ_INT((int)pc_backend_chain.rate(h), 44100);
    float buf[2 * 300];
    CHECK(pc_backend_chain.render(h, buf, 300) > 0);
    CHECK(pc_backend_chain.length_ms(h) > 0);
    CHECK_EQ_INT(pc_backend_chain.track_count(h), 1);
    pc_backend_chain.close(h);
    CHECK(open_as(raw.d, raw.n, "a.rad", &h) == &pc_backend_opl);
    CHECK_EQ_INT((int)pc_backend_chain.rate(h), 44100);
    CHECK(pc_backend_chain.render(h, buf, 300) > 0);
    CHECK_EQ_INT((int)pc_backend_chain.byte_pos(h), 0);                            /* AdPlug has no resume position: 0 */
    pc_backend_chain.close(h);
    CHECK(pc_backend_resolve(&pc_backend_xmp, NULL) == &pc_backend_xmp);           /* any other backend resolves to itself */
    pc_tier_set_source(NULL);
    free(mod);
    free(raw.d);
}

int main(void)
{
    RUN(test_table);
    RUN(test_chain_order_and_gate);
    return 0;
}
