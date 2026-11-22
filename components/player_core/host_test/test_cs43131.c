// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_cs43131.h"
#include "pc_test.h"

/* a model of the chip: a sparse register file, the power-down register's effect on the status bits, and a log of every write (order checked below) */
typedef struct { uint32_t reg; uint8_t v; uint8_t pdn_before; uint8_t mute_before; } wlog_t;
typedef struct {
    uint8_t pdn, pcm, vol_a, vol_b, hp_out, det, intst;
    bool reset_high, xtal_dead, fail_id, hp_never_done;
    int nw; wlog_t log[200];
    uint8_t other[16];
    bool bad_hp_out_write, unmuted_while_down, pll_dead, vol_stuck, unmuted_unverified, pll_down_while_clock, src_to_dead_pll;
    uint8_t pllstart, sysclk, hpf, pllmode, classh, err2;
    bool dead_after_stop, reset_now;
    int resets, fail_at, nwr;   /* fail_at: the n-th write (1-based) fails; 0 = never */
} chip_t;

static int cw(void *ctx, uint32_t r, uint8_t v)
{
    chip_t *c = ctx;
    if (!c->reset_high) return -1;
    if (c->fail_at && ++c->nwr == c->fail_at) return -1;
    if (c->nw < 200) c->log[c->nw++] = (wlog_t){ r, v, c->pdn, c->pcm & 3 };
    switch (r) {
    case 0x020000:
        if (!(v & 0x08) && !c->xtal_dead) c->intst |= 0x10;
        if (!(v & 0x08) && c->xtal_dead) c->intst |= 0x08;
        if ((v & 0x10) && !(c->pdn & 0x10) && !c->hp_never_done) c->intst |= 0x01;   /* the amp finished its ramp-down */
        if ((v & 0x04) && (c->sysclk & 3) == 1) c->pll_down_while_clock = true;   /* PDN_PLL set while the PLL is the source */
        c->pdn = v; break;
    case 0x090003: c->pcm = v; if (!(v & 3) && c->vol_a != c->vol_b) c->unmuted_unverified = true; if (!(v & 3) && (c->pdn & 0x50)) c->unmuted_while_down = true; break;
    case 0x090001: if (!c->vol_stuck) c->vol_b = v; break;
    case 0x090002: if (!c->vol_stuck) { c->vol_a = v; if (c->pcm & 0x40) c->vol_b = v; } break;
    case 0x090000: c->hpf = v; break;
    case 0x0B0000: c->classh = v; break;
    case 0x03001B: c->pllmode = v; break;
    case 0x030001: c->pllstart = v & 1; if (v & 1) c->intst |= c->pll_dead ? 0x02 : 0x04; break;
    case 0x010006:
        if ((v & 3) == 1 && !(c->pllstart && !c->pll_dead)) c->src_to_dead_pll = true;
        c->sysclk = v; break;
    case 0x080000: if (!(c->pdn & 0x10)) c->bad_hp_out_write = true; c->hp_out = v; break;
    default: break;
    }
    return 0;
}
static int cr(void *ctx, uint32_t r, uint8_t *v)
{
    chip_t *c = ctx;
    if (!c->reset_high) return -1;
    switch (r) {
    case 0x010000: *v = c->dead_after_stop ? 0xFF : 0x43; return 0;
    case 0x010001: *v = c->fail_id ? 0x30 : 0x13; return 0;
    case 0x010002: *v = 0x10; return 0;
    case 0x020000: *v = c->pdn; return 0;
    case 0x03001B: *v = c->pllmode; return 0;
    case 0x0B0000: *v = c->classh; return 0;
    case 0x080000: *v = c->hp_out; return 0;
    case 0x010006: *v = c->sysclk; return 0;
    case 0x0F0001: *v = c->err2; c->err2 = 0; return 0;
    case 0x090001: *v = c->vol_b; return 0;
    case 0x090002: *v = c->vol_a; return 0;
    case 0x0F0000: *v = c->intst; c->intst = 0; return 0;
    case 0x0D0001: *v = c->det; return 0;
    default: *v = 0; return 0;
    }
}
static void cd(void *ctx, uint32_t n) { (void)ctx; (void)n; }
static void cpin(void *ctx, bool h) { chip_t *c = ctx; if (!h) c->resets++; c->reset_high = h; }

static void mk(pc_cs_t *d, chip_t *c)
{
    memset(c, 0, sizeof *c);
    c->pdn = 0xFE; c->pcm = 0xA8; c->vol_a = c->vol_b = 0x78; c->hp_out = 0x30; c->classh = 0x1E; c->sysclk = 0x06;   /* the reset values */
    pc_cs_io_t io = { c, cw, cr, cd, cpin };
    pc_cs_init(d, &io);
}
static int idx(chip_t *c, uint32_t reg, int from) { for (int i = from; i < c->nw; i++) if (c->log[i].reg == reg) return i; return -1; }

static void test_rates(void)
{
    CHECK_EQ_INT(pc_cs_rate_code(32000), 0); CHECK_EQ_INT(pc_cs_rate_code(44100), 1); CHECK_EQ_INT(pc_cs_rate_code(48000), 2);
    CHECK_EQ_INT(pc_cs_rate_code(88200), 3); CHECK_EQ_INT(pc_cs_rate_code(96000), 4); CHECK_EQ_INT(pc_cs_rate_code(176400), 5);
    CHECK_EQ_INT(pc_cs_rate_code(192000), 6);
    CHECK_EQ_INT(pc_cs_rate_code(352800), 7); CHECK_EQ_INT(pc_cs_rate_code(384000), 8);       /* DS 7.1.7 p.114 */
    CHECK_EQ_INT(pc_cs_rate_code(22050), -1); CHECK_EQ_INT(pc_cs_rate_code(8000), -1); CHECK_EQ_INT(pc_cs_rate_code(16000), -1); CHECK_EQ_INT(pc_cs_rate_code(0), -1);
    CHECK(pc_cs_rate_is_441(44100) && pc_cs_rate_is_441(88200) && pc_cs_rate_is_441(176400) && pc_cs_rate_is_441(352800));
    CHECK(!pc_cs_rate_is_441(32000) && !pc_cs_rate_is_441(48000) && !pc_cs_rate_is_441(96000) && !pc_cs_rate_is_441(192000) && !pc_cs_rate_is_441(384000));
}

static void test_probe_and_boot(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    CHECK_EQ_INT(pc_cs_boot(&d), PC_CS_ESTATE);                       /* boot before probe is refused */
    CHECK_EQ_INT(pc_cs_probe(&d), PC_CS_OK);
    CHECK_EQ_INT(c.resets, 1);
    CHECK(c.reset_high);
    CHECK_EQ_INT(pc_cs_boot(&d), PC_CS_OK);
    CHECK_EQ_INT(d.state, PC_CS_READY);
    CHECK(d.muted);
    CHECK_EQ_INT(c.pcm & 3, 3);                                       /* muted */
    CHECK_EQ_INT(c.vol_a, PC_CS_VOL_BOOT); CHECK_EQ_INT(c.vol_b, PC_CS_VOL_BOOT);
    CHECK_EQ_INT(c.hpf, 0x02);                                          /* DS 7.5.2: fast, low latency, high-pass on, no de-emphasis */
    CHECK_EQ_INT(c.pcm, 0xEB);
    CHECK_EQ_INT(c.hp_out, PC_CS_OUT_FS_1V0);
    CHECK((c.pdn & 0x08) == 0 && (c.pdn & 0x50) == 0x50);              /* crystal on, amp and serial port still down */
    CHECK(!c.bad_hp_out_write);
    int ib = idx(&c, 0x020052, 0), xt = idx(&c, 0x020000, 0), sc = idx(&c, 0x010006, 0);
    CHECK(ib >= 0 && ib < xt && xt < sc);                             /* bias before the crystal, the crystal before it becomes the clock */
}

/* LAB (pc_lab.h): XTAL_IBIAS and the headphone-detect polarity at boot; the defaults are the constants, a bad ibias falls back to them */
static void test_lab_boot_knobs(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d);
    CHECK_EQ_INT(c.log[idx(&c, 0x020052, 0)].v, 0x04); CHECK_EQ_INT(c.log[idx(&c, 0x0D0000, 0)].v, 0xC4);
    pc_cs_t e; chip_t k; mk(&e, &k);
    pc_cs_set_lab(&e, 1, 6);
    pc_cs_probe(&e); CHECK_EQ_INT(pc_cs_boot(&e), PC_CS_OK);
    CHECK_EQ_INT(k.log[idx(&k, 0x020052, 0)].v, 0x06); CHECK_EQ_INT(k.log[idx(&k, 0x0D0000, 0)].v, 0xE4);
    pc_cs_t f; chip_t m; mk(&f, &m);
    pc_cs_set_lab(&f, 0, 3);
    pc_cs_probe(&f); pc_cs_boot(&f);
    CHECK_EQ_INT(m.log[idx(&m, 0x020052, 0)].v, 0x04); CHECK_EQ_INT(m.log[idx(&m, 0x0D0000, 0)].v, 0xC4);
}

static void test_probe_failures(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    c.fail_id = true;
    CHECK_EQ_INT(pc_cs_probe(&d), PC_CS_EID);                         /* a CS43130 (id ...30) is refused */
    CHECK_EQ_INT(pc_cs_boot(&d), PC_CS_ESTATE);
    mk(&d, &c); c.xtal_dead = true;
    CHECK_EQ_INT(pc_cs_probe(&d), PC_CS_OK);
    CHECK_EQ_INT(pc_cs_boot(&d), PC_CS_EXTAL);
    CHECK(d.state != PC_CS_READY);
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_ESTATE);           /* no start without a running crystal */
}

static void test_start_order_and_level(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d);
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 96000, 32), PC_CS_OK);
    CHECK_EQ_INT(d.state, PC_CS_RUNNING);
    CHECK(d.muted); CHECK_EQ_INT(c.pcm & 3, 3);                       /* running, still muted */
    int rate = idx(&c, 0x01000B, 0), key = idx(&c, 0x010010, 0), asp = idx(&c, 0x020000, 0), hp = idx(&c, 0x020000, asp + 1);
    CHECK(rate >= 0 && c.log[rate].v == 4);                           /* 96 kHz code */
    CHECK(key > rate && asp > key && hp > asp);                       /* format, then the test key, then ASP, then the amp */
    CHECK((c.log[asp].v & 0x40) == 0 && (c.log[hp].v & 0x10) == 0);
    int dxd = idx(&c, 0x080032, 0);
    CHECK(dxd >= 0 && dxd < asp && c.log[dxd].v == 0x20);              /* the pop-free value is in before the power-up ... */
    int dxd2 = idx(&c, 0x080032, dxd + 1), key2 = idx(&c, 0x010010, key + 1);
    CHECK(dxd2 > hp && c.log[dxd2].v == 0 && key2 > dxd2 && c.log[key2].v == 0);   /* ... and restored after it */
    int lcpr = idx(&c, 0x040016, 0), size = idx(&c, 0x01000C, 0);
    CHECK(c.log[lcpr].v == 63 && c.log[size].v == 0x04);              /* 64 BCK per frame, 32-bit slots */
    for (int i = 0; i < c.nw; i++) if (c.log[i].reg == 0x090003) CHECK(c.log[i].v & 3);   /* every PCM control write in the start keeps the mute */
    CHECK(!c.bad_hp_out_write && !c.unmuted_while_down);
    CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_OK);
    CHECK_EQ_INT(c.pcm & 3, 3); CHECK(d.muted);                       /* no volume yet (0): asking for sound keeps the mute bits */
    CHECK_EQ_INT(pc_cs_mute(&d, true), PC_CS_OK);
}

/* BTAUDIO: 24-bit slots (dac_slot24_from): ASP_SPSIZE 01, channel size RES 10, LCHI 23, LCPR 47 = 48 BCK per frame (18.432 MHz at 384 kHz) */
static void test_24_bit_slot(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d);
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 384000, 24), PC_CS_OK);
    int lcpr = idx(&c, 0x040016, 0), lchi = idx(&c, 0x040014, 0), size = idx(&c, 0x01000C, 0), ch1 = idx(&c, 0x05000A, 0), ch2 = idx(&c, 0x05000B, 0);
    CHECK(lcpr >= 0 && c.log[lcpr].v == 47 && c.log[lchi].v == 23 && c.log[size].v == 0x05);
    CHECK(c.log[ch1].v == 0x06 && c.log[ch2].v == 0x0E);
    CHECK_EQ_INT(d.bits, 24);
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_OK);                 /* back to 32 in one stop and start */
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 20), PC_CS_ERATE);
}

static void test_vol_db(void)
{
    CHECK_EQ_INT(pc_cs_vol_db_x10(100), 0);
    CHECK_EQ_INT(pc_cs_vol_db_x10(0), -9999);
    CHECK(pc_cs_vol_db_x10(50) < 0 && pc_cs_vol_db_x10(50) > pc_cs_vol_db_x10(10));
    CHECK_EQ_INT(pc_cs_vol_db_x10(10), -540);                           /* register 108 = -54 dB */
}

static void test_16_bit(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d); c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 16), PC_CS_OK);
    CHECK_EQ_INT(c.log[idx(&c, 0x040016, 0)].v, 31);                  /* 32 BCK per frame */
    CHECK_EQ_INT(c.log[idx(&c, 0x01000C, 0)].v, 0x06);
    CHECK_EQ_INT(c.log[idx(&c, 0x05000A, 0)].v, 0x05);               /* DS 7.3.23: RES 01 = 16 bit (0x06 is 24 bit); right channel 0x0D */
    CHECK_EQ_INT(c.log[idx(&c, 0x05000B, 0)].v, 0x0D);
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 20), PC_CS_ERATE);                 /* 24 is a slot width now (BTAUDIO); other widths stay refused */
}

static void test_stop_mutes_first_and_change_is_full_cycle(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false);
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_stop(&d), PC_CS_OK);
    CHECK_EQ_INT(d.state, PC_CS_READY);
    int m = idx(&c, 0x090003, 0), a = idx(&c, 0x020000, 0), b = idx(&c, 0x020000, a + 1);
    CHECK(m >= 0 && m < a && (c.log[m].v & 3) == 3);                   /* muted before anything powers down */
    CHECK(c.log[a].v & 0x10 && !(c.log[a].v & 0x40));                  /* the amp first ... */
    CHECK(c.log[b].v & 0x40);                                          /* ... then the serial port */
    CHECK_EQ_INT(pc_cs_stop(&d), PC_CS_OK);                            /* a second stop does nothing */
    pc_cs_start(&d, 44100, 32); pc_cs_mute(&d, false); c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 192000, 32), PC_CS_OK);               /* a rate change while running: stop, then start */
    int s = idx(&c, 0x090003, 0), up = idx(&c, 0x01000B, 0);
    CHECK(s >= 0 && s < up && (c.log[s].v & 3) == 3);
    CHECK_EQ_INT(c.log[up].v, 6);
    CHECK(!c.unmuted_while_down && d.muted);
}

static void test_power_down_sequence_and_alive_check(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false);
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_stop(&d), PC_CS_OK);
    int m = idx(&c, 0x090003, 0), hp = idx(&c, 0x020000, 0), asp = idx(&c, 0x020000, hp + 1);
    CHECK(m == 0 && (c.log[m].v & 3) == 3 && hp > m && asp > hp);         /* Ex. 5-3: mute, amp, serial port */
    CHECK_EQ_INT(idx(&c, 0x080046, 0), -1); CHECK_EQ_INT(idx(&c, 0x010010, 0), -1);   /* no undocumented test register in the power-down (DS 5.8.1 is the external-VCPFILT mode) */
    CHECK(d.muted && d.state == PC_CS_READY && !c.unmuted_while_down);
    CHECK_EQ_INT(d.done_miss, 0);
    pc_cs_start(&d, 48000, 32); c.dead_after_stop = true;                 /* the chip stops answering after the power-down */
    CHECK_EQ_INT(pc_cs_stop(&d), PC_CS_EIO); CHECK(d.muted);
}

static void test_reset_detected_before_power_up(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_set_volume(&d, 40); pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false);
    pc_cs_stop(&d);
    c.pdn |= 0x08; c.sysclk = 0x06; c.hp_out = 0x30; c.classh = 0x1E;     /* a POR: every register is back at its default */
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_ERESET);
    CHECK_EQ_INT(d.state, PC_CS_PROBED); CHECK(d.muted);
    for (int i = 0; i < c.nw; i++) CHECK(!(c.log[i].reg == 0x020000 && !(c.log[i].v & 0x10)));   /* the amplifier was never powered up on the default registers */
    CHECK_EQ_INT(idx(&c, 0x010006, 0), -1 + 0);                           /* and no clock was switched on a dead crystal */
    /* each register on its own */
    for (int k = 0; k < 4; k++) {
        mk(&d, &c); pc_cs_probe(&d); pc_cs_boot(&d);
        if (k == 0) c.pdn |= 0x08;
        if (k == 1) c.sysclk = 0x06;
        if (k == 2) c.hp_out = 0x30;
        if (k == 3) c.classh = 0x1E;
        CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_ERESET);
    }
}

static void test_serial_error_poll(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d);
    CHECK_EQ_INT(pc_cs_serial_errors(&d), 0);                             /* not running: nothing */
    pc_cs_set_volume(&d, 40); pc_cs_start(&d, 384000, 32);
    c.err2 = 0x04; CHECK_EQ_INT(pc_cs_serial_errors(&d), 0);              /* other status bits are not serial errors */
    c.err2 = 0x10 | 0x04; CHECK_EQ_INT(pc_cs_serial_errors(&d), 0x10);    /* EARLY */
    CHECK_EQ_INT(pc_cs_serial_errors(&d), 0);                             /* sticky bits cleared by the read */
    c.err2 = 0x48; CHECK_EQ_INT(pc_cs_serial_errors(&d), 0x48);           /* NOLRCK + ERROR */
}

static void test_pll_mode_is_read_modify_write(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_set_volume(&d, 40);
    c.pllmode = 0x11;                                                    /* bit1 clear, other bits set: only bit1 may change */
    CHECK_EQ_INT(pc_cs_start(&d, 44100, 32), PC_CS_OK);
    CHECK_EQ_INT(c.pllmode, 0x13);
    mk(&d, &c); pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_set_volume(&d, 40); c.pllmode = 0x03;
    pc_cs_start(&d, 44100, 32); CHECK_EQ_INT(c.pllmode, 0x03);         /* the kernel's reset value stays as it is */
}

static void test_stop_without_done_still_ends_safely(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false);
    c.hp_never_done = true;
    CHECK_EQ_INT(pc_cs_stop(&d), PC_CS_OK);                           /* no PDN_DONE (the crystal stays on): bounded wait, counted, not an error */
    CHECK_EQ_INT(d.state, PC_CS_READY); CHECK(c.pdn & 0x50); CHECK(d.muted); CHECK_EQ_INT(d.done_miss, 1);
    pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false); c.hp_never_done = false; c.nw = 0;
    CHECK_EQ_INT(pc_cs_stop(&d), PC_CS_OK); CHECK_EQ_INT(d.done_miss, 1);   /* with PDN_DONE present: no miss */
}

static void test_power_down_pair_option_and_shutdown(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_set_pdn_pair(&d, 1);
    pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false); c.nw = 0;
    CHECK_EQ_INT(pc_cs_stop(&d), PC_CS_OK);
    int key = idx(&c, 0x010010, 0), v1 = idx(&c, 0x080046, 0), m = idx(&c, 0x090003, 0);
    CHECK(key == 0 && c.log[key].v == 0x99 && v1 == 1 && c.log[v1].v == 0x21 && m > v1);   /* the pair opens before the mute */
    int v2 = idx(&c, 0x080046, v1 + 1), k2 = idx(&c, 0x010010, key + 1);
    CHECK(v2 > m && c.log[v2].v == 0x20 && k2 > v2 && c.log[k2].v == 0);                   /* and closes after PDN_ASP, key back to 0 */
    /* shutdown: stop, crystal off, RESET low and held; a part that is only READY is muted first */
    mk(&d, &c); pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false);
    CHECK_EQ_INT(pc_cs_shutdown(&d), PC_CS_OK);
    CHECK(!c.reset_high && d.state == PC_CS_OFF && d.muted && (c.pdn & 0x08) && !c.unmuted_while_down);
    mk(&d, &c); pc_cs_probe(&d); pc_cs_boot(&d);
    CHECK_EQ_INT(pc_cs_shutdown(&d), PC_CS_OK); CHECK(!c.reset_high && d.state == PC_CS_OFF);
    mk(&d, &c); pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_start(&d, 48000, 32); c.dead_after_stop = true;
    CHECK_EQ_INT(pc_cs_shutdown(&d), PC_CS_EIO); CHECK(!c.reset_high);                      /* a failing stop still ends with RESET low */
}

static void test_unmute_refused_unless_running(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_ESTATE);
    pc_cs_probe(&d); CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_ESTATE);
    pc_cs_boot(&d);   CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_ESTATE);
    CHECK_EQ_INT(c.pcm & 3, 3);
    pc_cs_start(&d, 48000, 32); CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_OK);
    pc_cs_stop(&d);   CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_ESTATE);
}

static void test_volume_map(void)
{
    CHECK_EQ_INT(pc_cs_vol_reg(0), 0xFF);                              /* the mute register */
    CHECK_EQ_INT(pc_cs_vol_reg(100), 0x00);                            /* the top step reaches 0 dB (full scale) */
    CHECK_EQ_INT(pc_cs_vol_reg(40), 72);                               /* -36 dB: the software curve's dB at the default step */
    CHECK_EQ_INT(pc_cs_vol_reg(10), 108);
    CHECK_EQ_INT(pc_cs_vol_reg(1), 160);                               /* -80 dB */
    int prev = 0x100;
    for (int v = 1; v <= 100; v++) {
        int r = pc_cs_vol_reg(v);
        CHECK(r < prev && r >= 0 && r <= 0xFE);                         /* strictly louder per step, never the mute code, inside the chip's range */
        prev = r;
    }
    CHECK(pc_cs_vol_reg(99) - pc_cs_vol_reg(100) <= 2 && pc_cs_vol_reg(11) - pc_cs_vol_reg(12) <= 2);   /* 0.6 dB (1 - 2 half-dB codes) per step on top */
    CHECK(pc_cs_vol_reg(2) - pc_cs_vol_reg(3) > 4);                    /* steeper at the bottom */
    CHECK(pc_cs_digital_gain(1) == 1.0f && pc_cs_digital_gain(100) == 1.0f && pc_cs_digital_gain(0) == 0.0f);   /* unity digital stage */
}

static void test_volume_and_unmute_order(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d);
    CHECK_EQ_INT(pc_cs_set_volume(&d, 40), PC_CS_OK);                  /* READY: written and read back, nothing unmuted */
    CHECK_EQ_INT(c.vol_a, 72); CHECK_EQ_INT(c.vol_b, 72); CHECK(d.vol_ok); CHECK_EQ_INT(c.pcm & 3, 3);
    pc_cs_start(&d, 48000, 32);
    CHECK(d.vol_ok); CHECK_EQ_INT(c.vol_a, 72);                         /* start writes it again, before the power-up */
    CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_OK); CHECK_EQ_INT(c.pcm & 3, 0);
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_set_volume(&d, 100), PC_CS_OK); CHECK_EQ_INT(c.vol_a, 0); CHECK_EQ_INT(c.pcm & 3, 0);   /* a change while playing: no mute needed */
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_set_volume(&d, 0), PC_CS_OK);                   /* to 0: mute bits first, then the mute code */
    int m = idx(&c, 0x090003, 0), v = idx(&c, 0x090002, 0);
    CHECK(m >= 0 && v > m && (c.log[m].v & 3) == 3); CHECK_EQ_INT(c.vol_a, 0xFF); CHECK(d.muted);
    CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_OK); CHECK(d.muted);      /* at volume 0 an unmute request leaves the mute bits set */
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_set_volume(&d, 30), PC_CS_OK);                  /* the rise: volume registers first, read back, then the unmute */
    v = idx(&c, 0x090002, 0); m = idx(&c, 0x090003, 0);
    CHECK(v >= 0 && m > v && (c.log[m].v & 3) == 0); CHECK(!d.muted);
    CHECK(!c.unmuted_unverified && !c.unmuted_while_down);
    /* a register that does not take the value: never sound */
    c.vol_stuck = true;
    CHECK_EQ_INT(pc_cs_set_volume(&d, 50), PC_CS_EVOL); CHECK(d.muted && !d.vol_ok); CHECK_EQ_INT(c.pcm & 3, 3);
    CHECK_EQ_INT(pc_cs_mute(&d, false), PC_CS_OK); CHECK(d.muted);
    c.vol_stuck = false;
    CHECK_EQ_INT(pc_cs_set_volume(&d, 50), PC_CS_OK); CHECK(!d.muted);  /* verified again: the standing request is honoured */
    /* a stop forgets the request: the next start begins muted and a volume write alone never unmutes it */
    pc_cs_stop(&d); pc_cs_start(&d, 44100, 32);
    CHECK(d.muted); CHECK_EQ_INT(pc_cs_set_volume(&d, 60), PC_CS_OK); CHECK(d.muted); CHECK_EQ_INT(c.pcm & 3, 3);
    /* a start whose volume does not read back fails and stays muted */
    mk(&d, &c); pc_cs_probe(&d); pc_cs_boot(&d); c.vol_stuck = true; CHECK_EQ_INT(pc_cs_set_volume(&d, 40), PC_CS_EVOL);
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_EVOL); CHECK_EQ_INT(d.state, PC_CS_READY); CHECK(d.muted);
    /* before boot the value only waits */
    mk(&d, &c); CHECK_EQ_INT(pc_cs_set_volume(&d, 70), PC_CS_OK); CHECK_EQ_INT(d.vol, 70); CHECK_EQ_INT(c.nw, 0);
}

static void test_clock_family_switches(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_set_volume(&d, 40);
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 22050 * 2, 32), PC_CS_OK);            /* 44.1 kHz: PLL to 22.5792 MHz */
    CHECK_EQ_INT(d.clk, 1); CHECK_EQ_INT(c.sysclk, 0x05); CHECK_EQ_INT(c.pllstart, 1); CHECK(!(c.pdn & 0x04));
    int pre = idx(&c, 0x040002, 0), od = idx(&c, 0x030008, 0), fr = idx(&c, 0x030004, 0), in = idx(&c, 0x030005, 0), cal = idx(&c, 0x03000A, 0), go = idx(&c, 0x030001, 0), sw = idx(&c, 0x010006, go);
    CHECK(pre >= 0 && pre < od && od < fr && fr < in && in < cal && cal < go && go < sw);   /* DS 4.7.2.1 order, the source switch last */
    CHECK_EQ_INT(c.log[pre].v, 0x03); CHECK_EQ_INT(c.log[od].v, 0x0A); CHECK_EQ_INT(c.log[fr].v, 0x80); CHECK_EQ_INT(c.log[in].v, 0x49); CHECK_EQ_INT(c.log[cal].v, 118);
    CHECK_EQ_INT(c.log[idx(&c, 0x01000B, 0)].v, 1);
    pc_cs_mute(&d, false);
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_OK);                /* 44.1 -> 48: stop, mute, back to the crystal, PLL down */
    CHECK_EQ_INT(d.clk, 0); CHECK_EQ_INT(c.sysclk, 0x00); CHECK_EQ_INT(c.pllstart, 0); CHECK(c.pdn & 0x04);
    int mu = idx(&c, 0x090003, 0), r = idx(&c, 0x010006, 0);
    CHECK(mu >= 0 && mu < r && (c.log[mu].v & 3) == 3); CHECK(d.muted);
    pc_cs_mute(&d, false);
    c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 192000, 32), PC_CS_OK);               /* same family: no clock work at all */
    CHECK_EQ_INT(idx(&c, 0x010006, 0), -1); CHECK_EQ_INT(idx(&c, 0x030001, 0), -1);
    CHECK_EQ_INT(pc_cs_start(&d, 22050 * 16, 32), PC_CS_OK);           /* 352.8 kHz: code 7, PLL */
    CHECK_EQ_INT(c.log[idx(&c, 0x01000B, 0)].v + 0, 6);
    int last = -1; for (int i = 0; i < c.nw; i++) if (c.log[i].reg == 0x01000B) last = i;
    CHECK_EQ_INT(c.log[last].v, 7); CHECK_EQ_INT(d.clk, 1);
    CHECK_EQ_INT(pc_cs_start(&d, 384000, 32), PC_CS_OK);               /* 384 kHz: code 8, crystal */
    last = -1; for (int i = 0; i < c.nw; i++) if (c.log[i].reg == 0x01000B) last = i;
    CHECK_EQ_INT(c.log[last].v, 8); CHECK_EQ_INT(d.clk, 0);
    CHECK(!c.src_to_dead_pll && !c.pll_down_while_clock && !c.unmuted_while_down);
}

static void test_pll_no_lock_falls_back_to_the_crystal(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d); pc_cs_set_volume(&d, 40);
    pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false);
    c.pll_dead = true;
    CHECK_EQ_INT(pc_cs_start(&d, 44100, 32), PC_CS_OK);                /* DS 7.1.7 p.114: the rate takes either MCLK_INT: it plays from the crystal */
    CHECK_EQ_INT(d.state, PC_CS_RUNNING); CHECK(d.muted); CHECK_EQ_INT(d.note, PC_CS_NOTE_PLL); CHECK(d.pll_bad);
    CHECK_EQ_INT(d.clk, 0); CHECK_EQ_INT(c.sysclk, 0x00); CHECK_EQ_INT(c.pllstart, 0); CHECK(c.pdn & 0x04);   /* the dead PLL never became the source and is powered down */
    CHECK(!c.src_to_dead_pll && !c.pll_down_while_clock);
    d.note = 0; c.nw = 0;
    CHECK_EQ_INT(pc_cs_start(&d, 88200, 32), PC_CS_OK);                /* no second try, no second note */
    CHECK_EQ_INT(idx(&c, 0x030001, 0), -1); CHECK_EQ_INT(d.note, 0);
}

static void test_cfg_variants(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_set_cfg(&d, 0, 1, 0);                                        /* dac_hpf 0, dac_fs 1 = 1.41 V, dac_amute 0 */
    pc_cs_probe(&d); pc_cs_boot(&d);
    CHECK_EQ_INT(c.hpf, 0x00); CHECK_EQ_INT(c.hp_out, PC_CS_OUT_FS_1V41); CHECK(!c.bad_hp_out_write);
    CHECK_EQ_INT(c.classh, 0x1C); CHECK_EQ_INT(pc_cs_drive_rb(&d), 1);    /* 1.41 V keeps HV off, byte for byte as before */
    CHECK_EQ_INT(c.pcm, 0xE3);                                         /* mute bits set, PCM_AMUTE (bit 3) clear */
    pc_cs_set_volume(&d, 50); pc_cs_start(&d, 48000, 32); pc_cs_mute(&d, false);
    CHECK_EQ_INT(c.pcm, 0xE0);                                         /* unmuted: still no auto-mute */
    pc_cs_mute(&d, true); CHECK_EQ_INT(c.pcm, 0xE3);
    mk(&d, &c);                                                        /* defaults: hpf on, 1.0 V, auto-mute on (the chip's reset behaviour) */
    pc_cs_probe(&d); pc_cs_boot(&d);
    CHECK_EQ_INT(c.hpf, 0x02); CHECK_EQ_INT(c.hp_out, PC_CS_OUT_FS_1V0); CHECK_EQ_INT(c.pcm, 0xEB);
    CHECK_EQ_INT(c.classh, 0x1C); CHECK_EQ_INT(pc_cs_drive_rb(&d), 0);
}

/* the experimental 1.73 V drive (dac_fs 2): OUT_FS 0x30 with Class H HV on (0x1E); the integrity check wants that pair, and any other is caught */
static void test_drive_1v73(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_set_cfg(&d, 1, 2, 1);
    pc_cs_set_cfg(&d, 1, 7, 1); CHECK_EQ_INT(d.cfg_fs, 2);              /* anything above 1 is the top value */
    CHECK_EQ_INT(pc_cs_probe(&d), PC_CS_OK); CHECK_EQ_INT(pc_cs_boot(&d), PC_CS_OK);
    CHECK_EQ_INT(c.hp_out, PC_CS_OUT_FS_1V73); CHECK_EQ_INT(c.hp_out, 0x30);
    CHECK_EQ_INT(c.classh, PC_CS_CLASSH_HV); CHECK_EQ_INT(c.classh, 0x1E);
    CHECK(!c.bad_hp_out_write);                                         /* written while PDN_HP = 1 only */
    CHECK_EQ_INT(pc_cs_drive_rb(&d), 2);
    pc_cs_set_volume(&d, 20);
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_OK);                 /* the pair boot wrote is what start wants */
    CHECK_EQ_INT(pc_cs_drive_rb(&d), 2);
    CHECK(!c.unmuted_while_down);
    pc_cs_stop(&d);
    c.classh = 0x1C;                                                    /* HV dropped behind our back */
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_ERESET); CHECK_EQ_INT(pc_cs_drive_rb(&d), 3); CHECK(d.muted);
    mk(&d, &c); pc_cs_set_cfg(&d, 1, 2, 1); pc_cs_probe(&d); pc_cs_boot(&d);
    c.hp_out = 0x20;                                                    /* the right Class H with the wrong OUT_FS */
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_ERESET);
    /* the other values keep HV off: a Class H of 0x1E where 0x1C is wanted is caught too */
    for (int fs = 0; fs < 2; fs++) {
        mk(&d, &c); pc_cs_set_cfg(&d, 1, fs, 1); pc_cs_probe(&d); pc_cs_boot(&d);
        CHECK_EQ_INT(c.classh, 0x1C); CHECK_EQ_INT(c.hp_out, fs ? 0x20 : 0x10);
        c.classh = 0x1E;
        CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_ERESET);
    }
    mk(&d, &c); pc_cs_set_cfg(&d, 1, 2, 1); pc_cs_probe(&d); pc_cs_boot(&d);
    c.sysclk = 0x06; c.pdn |= 0x08;                                     /* a POR returns exactly the 1.73 V pair: the crystal / SYSCLK check is what sees it */
    c.hp_out = 0x30; c.classh = 0x1E;
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_ERESET);
}

static void test_headphone(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    CHECK_EQ_INT(pc_cs_headphone(&d), -1);
    pc_cs_probe(&d); pc_cs_boot(&d);
    c.det = 0; CHECK_EQ_INT(pc_cs_headphone(&d), -1);                  /* the debounce has not decided */
    c.det = 0x40; CHECK_EQ_INT(pc_cs_headphone(&d), 1);
    c.det = 0; CHECK_EQ_INT(pc_cs_headphone(&d), 1);                   /* keeps the last verdict while undecided */
    c.det = 0x20; CHECK_EQ_INT(pc_cs_headphone(&d), 0);
}

static void test_io_failure(void)
{
    pc_cs_t d; chip_t c; mk(&d, &c);
    pc_cs_probe(&d); pc_cs_boot(&d);
    c.reset_high = false;                                              /* the chip stops answering */
    CHECK_EQ_INT(pc_cs_start(&d, 48000, 32), PC_CS_EIO);
    CHECK(d.muted);
}

static void test_start_fails_midway_powers_down(void)
{
    for (int n = 1; n < 70; n++) {
        pc_cs_t d; chip_t c; mk(&d, &c);
        pc_cs_probe(&d); pc_cs_boot(&d);
        c.fail_at = n; c.nwr = 0;
        const int rc = pc_cs_start(&d, 48000, 32);
        if (rc == PC_CS_OK) { CHECK_EQ_INT(d.state, PC_CS_RUNNING); continue; }
        CHECK_EQ_INT(d.state, PC_CS_READY);
        CHECK(d.muted);
        CHECK((c.pdn & 0x50) == 0x50);                                  /* HP amp and ASP down again */
        CHECK(c.pcm & 3);
    }
}

int main(void)
{
    RUN(test_rates);
    RUN(test_probe_and_boot);
    RUN(test_probe_failures);
    RUN(test_start_order_and_level);
    RUN(test_16_bit);
    RUN(test_stop_mutes_first_and_change_is_full_cycle);
    RUN(test_stop_without_done_still_ends_safely);
    RUN(test_power_down_sequence_and_alive_check);
    RUN(test_power_down_pair_option_and_shutdown);
    RUN(test_reset_detected_before_power_up);
    RUN(test_serial_error_poll);
    RUN(test_pll_mode_is_read_modify_write);
    RUN(test_unmute_refused_unless_running);
    RUN(test_24_bit_slot);
    RUN(test_vol_db);
    RUN(test_volume_map);
    RUN(test_volume_and_unmute_order);
    RUN(test_clock_family_switches);
    RUN(test_pll_no_lock_falls_back_to_the_crystal);
    RUN(test_cfg_variants);
    RUN(test_drive_1v73);
    RUN(test_lab_boot_knobs);
    RUN(test_headphone);
    RUN(test_io_failure);
    RUN(test_start_fails_midway_powers_down);
    return 0;
}
