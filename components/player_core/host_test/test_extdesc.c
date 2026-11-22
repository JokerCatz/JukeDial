// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_extdesc.h"
#include "pc/pc_rate.h"
#include "pc/pc_exthid.h"
#include "pc_test.h"

static uint8_t d[PC_EXTDESC_MAX_LEN];
static pc_extdesc_info_t info;

static pc_extdesc_cfg_t mk(bool msc, bool uac, uint32_t cap, uint8_t bits)
{
    pc_extdesc_cfg_t c;
    memset(&c, 0, sizeof c);
    c.msc = msc; c.uac = uac; c.max_rate = cap; c.bits_mask = bits; c.hid_report_len = 87; c.str_msc = 4; c.str_hid = 5; c.str_audio = 6;
    return c;
}

/* walks the descriptor chain: every bLength is sane and they sum to wTotalLength exactly; returns the number of descriptors */
static int walk(int n, int *ep_seen /* [256] counts per bEndpointAddress */, int *max_pkt_out)
{
    int off = 0, cnt = 0;
    CHECK_EQ_INT(d[0], 9); CHECK_EQ_INT(d[1], 2);
    CHECK_EQ_INT(d[2] | (d[3] << 8), n);
    while (off < n) {
        CHECK(d[off] >= 2 && off + d[off] <= n);
        if (d[off + 1] == 5) {
            CHECK_EQ_INT(d[off], 7);
            ep_seen[d[off + 2]]++;
            if (d[off + 2] == PC_EXTDESC_EP_AUD_OUT) max_pkt_out[(off)] = d[off + 4] | (d[off + 5] << 8);
        }
        off += d[off];
        cnt++;
    }
    CHECK_EQ_INT(off, n);
    return cnt;
}

static int count_type(int n, int type)
{
    int off = 0, c = 0;
    while (off < n) { if (d[off + 1] == type) c++; off += d[off]; }
    return c;
}

static void test_pkt(void)
{   /* spec section 2 table */
    CHECK_EQ_INT(pc_extdesc_pkt(44100, 2), 28); CHECK_EQ_INT(pc_extdesc_pkt(48000, 2), 28);
    CHECK_EQ_INT(pc_extdesc_pkt(48000, 3), 42); CHECK_EQ_INT(pc_extdesc_pkt(48000, 4), 56);
    CHECK_EQ_INT(pc_extdesc_pkt(96000, 2), 52); CHECK_EQ_INT(pc_extdesc_pkt(96000, 3), 78); CHECK_EQ_INT(pc_extdesc_pkt(96000, 4), 104);
    CHECK_EQ_INT(pc_extdesc_pkt(192000, 2), 100); CHECK_EQ_INT(pc_extdesc_pkt(192000, 3), 150); CHECK_EQ_INT(pc_extdesc_pkt(192000, 4), 200);
    CHECK_EQ_INT(pc_extdesc_pkt(384000, 2), 196); CHECK_EQ_INT(pc_extdesc_pkt(384000, 3), 294); CHECK_EQ_INT(pc_extdesc_pkt(384000, 4), 392);
}

static void test_rates(void)
{
    uint32_t r[PC_EXTDESC_MAX_RATES];
    CHECK_EQ_INT(pc_extdesc_candidate(0), 44100); CHECK_EQ_INT(pc_extdesc_candidate(7), 384000); CHECK_EQ_INT(pc_extdesc_candidate(8), 0);
    CHECK_EQ_INT(pc_extdesc_candidate(-1), 0);
    int n = pc_extdesc_rates(48000, 0, r);   /* the default cap: only 44.1 / 48 */
    CHECK_EQ_INT(n, 2); CHECK_EQ_INT(r[0], 44100); CHECK_EQ_INT(r[1], 48000);
    n = pc_extdesc_rates(96000, 0, r);
    for (int i = 0; i < n; i++) { CHECK(r[i] <= 96000); CHECK(pc_mixer_rate_ok(r[i])); if (i) CHECK(r[i] > r[i - 1]); }
    CHECK(n >= 2);
    int all = pc_extdesc_rates(384000, 0, r);
    CHECK(all >= n); CHECK_EQ_INT(r[all - 1], 384000);
    for (int i = 0; i < all; i++) CHECK(pc_mixer_rate_ok(r[i]));
    int dropped = pc_extdesc_rates(384000, 1u << 7, r);     /* 384k dropped (read-back failure) */
    CHECK_EQ_INT(dropped, all - 1); CHECK(r[dropped - 1] < 384000);
    CHECK_EQ_INT(pc_extdesc_rates(0, 0, r), 0);
    CHECK_EQ_INT(pc_extdesc_rates(384000, 0xFF, r), 0);
}

static void test_three_in_one(void)
{
    pc_extdesc_cfg_t c = mk(true, true, 384000, 7);
    int n = pc_extdesc_build(&c, d, sizeof d, &info);
    CHECK(n > 0);
    /* independent sum: config 9 + MSC 23 + HID 25 + IAD 8 + AC (9+9+8+17+18+12) + AS alt0 9 + 3 alts x 53 */
    CHECK_EQ_INT(n, 9 + 23 + 25 + 8 + 73 + 9 + 3 * 53);
    CHECK_EQ_INT(n, 306);
    CHECK(n <= PC_EXTDESC_MAX_LEN);
    CHECK_EQ_INT(info.total_len, n);
    CHECK_EQ_INT(info.n_itf, 4); CHECK_EQ_INT(d[4], 4);
    CHECK_EQ_INT(info.msc_itf, 0); CHECK_EQ_INT(info.hid_itf, 1); CHECK_EQ_INT(info.ac_itf, 2); CHECK_EQ_INT(info.as_itf, 3);
    int ep[256] = { 0 }, mp[400] = { 0 };
    walk(n, ep, mp);
    CHECK_EQ_INT(ep[PC_EXTDESC_EP_MSC_OUT], 1); CHECK_EQ_INT(ep[PC_EXTDESC_EP_MSC_IN], 1); CHECK_EQ_INT(ep[PC_EXTDESC_EP_HID_IN], 1);
    CHECK_EQ_INT(ep[PC_EXTDESC_EP_AUD_FB], 3);            /* one feedback endpoint per alt, never two in one alt */
    CHECK_EQ_INT(ep[PC_EXTDESC_EP_AUD_OUT], 3);
    int total = 0;
    for (int i = 0; i < 256; i++) total += ep[i];
    CHECK_EQ_INT(total, 3 + 3 + 3);                       /* nothing else */
    CHECK_EQ_INT(count_type(n, 4), 1 + 1 + 1 + 1 + 3 + 0 + 0);   /* MSC, HID, AC, AS alt0, AS alt1..3 */
    CHECK_EQ_INT(count_type(n, 0x0B), 1);
    CHECK_EQ_INT(info.n_alt, 3);
    CHECK_EQ_INT(info.alt_bits[0], 16); CHECK_EQ_INT(info.alt_bits[1], 24); CHECK_EQ_INT(info.alt_bits[2], 32);
    CHECK_EQ_INT(info.alt_max_pkt[0], 196); CHECK_EQ_INT(info.alt_max_pkt[1], 294); CHECK_EQ_INT(info.alt_max_pkt[2], 392);
    for (int i = 0; i < 3; i++) CHECK(info.alt_max_pkt[i] <= 1024);
    /* the endpoint descriptors carry the same sizes, in order */
    int found = 0;
    for (int off = 0; off < n; off += d[off])
        if (d[off + 1] == 5 && d[off + 2] == PC_EXTDESC_EP_AUD_OUT) { CHECK_EQ_INT(d[off + 4] | (d[off + 5] << 8), info.alt_max_pkt[found]); CHECK_EQ_INT(d[off + 6], 1); CHECK_EQ_INT(d[off + 3], 5); found++; }
        else if (d[off + 1] == 5 && d[off + 2] == PC_EXTDESC_EP_AUD_FB) { CHECK_EQ_INT(d[off + 4], 4); CHECK_EQ_INT(d[off + 6], 4); CHECK_EQ_INT(d[off + 3], 0x11); }
        else if (d[off + 1] == 5 && (d[off + 2] & 0x7F) == 1) CHECK_EQ_INT(d[off + 4] | (d[off + 5] << 8), 512);
    CHECK_EQ_INT(found, 3);
    /* AudioControl total length field = bytes of the AC class descriptors */
    for (int off = 0; off < n; off += d[off])
        if (d[off + 1] == 0x24 && d[off + 2] == 0x01 && d[off] == 9) CHECK_EQ_INT(d[off + 6] | (d[off + 7] << 8), 9 + 8 + 17 + 18 + 12);
    CHECK_EQ_INT(d[7], 0x80); CHECK_EQ_INT(d[8], 50);
}

static void test_combos(void)
{
    int ep[256], mp[400];
    pc_extdesc_cfg_t c = mk(true, false, 0, 0);          /* HID + MSC */
    int n = pc_extdesc_build(&c, d, sizeof d, &info);
    CHECK_EQ_INT(n, 9 + 23 + 25); CHECK_EQ_INT(n, 57);
    CHECK_EQ_INT(info.n_itf, 2); CHECK_EQ_INT(info.msc_itf, 0); CHECK_EQ_INT(info.hid_itf, 1); CHECK_EQ_INT(info.ac_itf, 0xFF);
    memset(ep, 0, sizeof ep); walk(n, ep, mp);
    CHECK_EQ_INT(ep[0x01] + ep[0x81] + ep[0x82], 3); CHECK_EQ_INT(ep[0x02] + ep[0x83], 0);

    c = mk(false, true, 96000, 7);                       /* HID + UAC2 */
    n = pc_extdesc_build(&c, d, sizeof d, &info);
    CHECK_EQ_INT(n, 9 + 25 + 8 + 73 + 9 + 3 * 53);
    CHECK_EQ_INT(info.n_itf, 3); CHECK_EQ_INT(info.hid_itf, 0); CHECK_EQ_INT(info.ac_itf, 1); CHECK_EQ_INT(info.as_itf, 2); CHECK_EQ_INT(info.msc_itf, 0xFF);
    memset(ep, 0, sizeof ep); walk(n, ep, mp);
    CHECK_EQ_INT(ep[0x01] + ep[0x81], 0); CHECK_EQ_INT(ep[0x82], 1); CHECK_EQ_INT(ep[0x02], 3);
    CHECK_EQ_INT(info.alt_max_pkt[0], pc_extdesc_pkt(info.rates[info.n_rates - 1], 2));
    CHECK(info.rates[info.n_rates - 1] <= 96000);

    c = mk(true, true, 48000, PC_EXTDESC_BITS_24);       /* default cap, one bit depth */
    n = pc_extdesc_build(&c, d, sizeof d, &info);
    CHECK_EQ_INT(n, 9 + 23 + 25 + 8 + 73 + 9 + 53); CHECK_EQ_INT(n, 200);
    CHECK_EQ_INT(info.n_alt, 1); CHECK_EQ_INT(info.alt_bits[0], 24); CHECK_EQ_INT(info.alt_max_pkt[0], 42);
    CHECK_EQ_INT(info.n_rates, 2);
    c.bits_mask = PC_EXTDESC_BITS_16 | PC_EXTDESC_BITS_32;
    n = pc_extdesc_build(&c, d, sizeof d, &info);
    CHECK_EQ_INT(info.n_alt, 2); CHECK_EQ_INT(info.alt_bits[1], 32); CHECK_EQ_INT(info.alt_max_pkt[1], 56);
    CHECK_EQ_INT(n, 9 + 23 + 25 + 8 + 73 + 9 + 2 * 53);
}

static void test_failures(void)
{
    pc_extdesc_cfg_t c = mk(true, true, 384000, 7);
    CHECK_EQ_INT(pc_extdesc_build(&c, d, 305, &info), -1);       /* one byte short */
    CHECK_EQ_INT(pc_extdesc_build(&c, d, 306, &info), 306);
    CHECK_EQ_INT(pc_extdesc_build(&c, d, 0, NULL), -1);
    c.bits_mask = 0; CHECK_EQ_INT(pc_extdesc_build(&c, d, sizeof d, &info), -2);
    c = mk(true, true, 384000, 7); c.drop_mask = 0xFF; CHECK_EQ_INT(pc_extdesc_build(&c, d, sizeof d, &info), -2);
    c = mk(true, true, 0, 7); CHECK_EQ_INT(pc_extdesc_build(&c, d, sizeof d, &info), -2);
    c = mk(true, false, 0, 0); CHECK(pc_extdesc_build(&c, d, sizeof d, NULL) > 0);   /* info is optional; bits ignored without uac */
}

static uint32_t fnv(const uint8_t *p, int n) { uint32_t h = 2166136261u; while (n--) { h ^= *p++; h *= 16777619u; } return h; }
/* the shipped configurations at the default cap (48 k: the 有線最高取樣率 default) and at 384 k, 16/24/32 bit, with the real HID map length: lengths, the FIFO words
 * the controller must hold, and a hash of the exact bytes (a change to the descriptor generator shows here and has to be intended) */
static void test_shipped(void)
{
    static const struct { int combo; uint32_t cap; int len; uint32_t rx, total, hash; } t[] = {
        { PC_EXTCOMBO_ALL, 48000, 306, 304, 453, 0x8c7b4f34u }, { PC_EXTCOMBO_ALL, 384000, 306, 304, 453, 0x10e4c6bau },
        { PC_EXTCOMBO_DRIVE, 48000, 57, 304, 452, 0x56a22b65u }, { PC_EXTCOMBO_SOUND, 384000, 283, 244, 265, 0x5b146f6cu },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        pc_extdesc_cfg_t c = mk(pc_extdesc_combo_msc(t[i].combo), pc_extdesc_combo_uac(t[i].combo), t[i].cap, 7);
        c.hid_report_len = pc_exthid_map_len;
        int n = pc_extdesc_build(&c, d, sizeof d, &info);
        CHECK_EQ_INT(n, t[i].len);
        CHECK_EQ_INT(fnv(d, n), t[i].hash);
        pc_extdesc_fifo_t f;
        pc_extdesc_fifo(&c, &info, &f);
        CHECK_EQ_INT(f.rx, t[i].rx); CHECK_EQ_INT(f.total, t[i].total);
        CHECK(f.total <= PC_EXTDESC_FIFO_WORDS);
    }
}
static void test_combo(void)
{
    CHECK(pc_extdesc_combo_msc(PC_EXTCOMBO_ALL) && pc_extdesc_combo_uac(PC_EXTCOMBO_ALL));
    CHECK(pc_extdesc_combo_msc(PC_EXTCOMBO_DRIVE) && !pc_extdesc_combo_uac(PC_EXTCOMBO_DRIVE));
    CHECK(!pc_extdesc_combo_msc(PC_EXTCOMBO_SOUND) && pc_extdesc_combo_uac(PC_EXTCOMBO_SOUND));
    CHECK_EQ_INT(pc_extdesc_combo_next(PC_EXTCOMBO_ALL), PC_EXTCOMBO_DRIVE);
    CHECK_EQ_INT(pc_extdesc_combo_next(PC_EXTCOMBO_DRIVE), PC_EXTCOMBO_SOUND);
    CHECK_EQ_INT(pc_extdesc_combo_next(PC_EXTCOMBO_SOUND), PC_EXTCOMBO_ALL);
    CHECK_EQ_INT(pc_extdesc_combo_next(77), PC_EXTCOMBO_ALL);                   /* a stored value out of range is treated as ALL by the caller: next is ALL */
    CHECK(pc_extdesc_combo_pid(0) != pc_extdesc_combo_pid(1) && pc_extdesc_combo_pid(1) != pc_extdesc_combo_pid(2) && pc_extdesc_combo_pid(0) != pc_extdesc_combo_pid(2));
}

int main(void)
{
    RUN(test_shipped); RUN(test_combo);
    RUN(test_pkt); RUN(test_rates); RUN(test_three_in_one); RUN(test_combos); RUN(test_failures);
    return 0;
}
