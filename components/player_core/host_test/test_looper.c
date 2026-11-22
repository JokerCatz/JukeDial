// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_looper.h"

/* 48 kHz, 120 BPM 4/4: beat 24000, bar 96000; 64-sample blocks */
#define BEAT 24000u
#define BAR 96000u
#define NHEAD 8192u                                /* 8192 * 128 = 1048576 samples max (10.9 bars) */
static pc_lp_ev_t pool[4096];
static uint16_t heads[NHEAD];
static pc_looper_t lp;
typedef struct { int64_t pos; pc_midi_ev_t ev; } got_t;
static got_t got[2048];
static int ngot;
static int64_t cur, done;                          /* block being run; everything before done has been run */

static void emit(void *ctx, const pc_midi_ev_t *e)
{
    (void)ctx;
    if (ngot < 2048) { got[ngot].pos = cur; got[ngot].ev = *e; ngot++; }
}
static void run_to(int64_t to)                     /* blocks up to (not including) the one holding `to` */
{
    for (; done + 64 <= to; done += 64) { cur = done; pc_lp_block(&lp, done, 64, emit, NULL); }
}
static pc_midi_ev_t ev(int type, int ch, int a, int b) { pc_midi_ev_t e = { (uint8_t)type, (uint8_t)ch, (uint8_t)a, (uint8_t)b, 0, 0 }; return e; }
/* a live event heard at `heard`; the render head is `lead` samples further on (DAC latency) */
static void live(int type, int ch, int a, int b, int64_t heard, int64_t lead)
{
    run_to(heard + lead);
    pc_midi_ev_t e = ev(type, ch, a, b);
    pc_lp_record(&lp, &e, heard);
}
static int find(int type, int note, int from)      /* index of the next emitted event, -1 */
{
    for (int i = from; i < ngot; i++) if (got[i].ev.type == type && got[i].ev.a == note) return i;
    return -1;
}
static void fresh(void) { pc_lp_init(&lp, pool, 4096, heads, NHEAD); ngot = 0; cur = done = 0; }
static int64_t blk(int64_t p) { return p / 64 * 64; }

static void test_fixed_take(void)
{
    fresh();
    CHECK_EQ_INT(lp.state, PC_LP_EMPTY);
    CHECK_EQ_INT(pc_lp_arm(&lp, BAR, BAR, BEAT, 2, 0), 0);
    CHECK_EQ_INT(lp.state, PC_LP_COUNTIN);
    run_to(BAR);
    CHECK_EQ_INT(lp.state, PC_LP_COUNTIN);
    run_to(BAR + 64);
    CHECK_EQ_INT(lp.state, PC_LP_TAKE);
    live(PC_MIDI_NOTE_ON, 0, 60, 100, BAR + 1000, 300);
    live(PC_MIDI_NOTE_OFF, 0, 60, 0, BAR + 24000, 300);
    CHECK_EQ_INT(lp.cnt[0], 2);
    int64_t end = BAR + 2 * BAR;
    run_to(end);
    CHECK_EQ_INT(ngot, 0);                          /* a take does not play itself */
    CHECK_EQ_INT(lp.state, PC_LP_TAKE);
    run_to(end + 64);
    CHECK_EQ_INT(lp.state, PC_LP_PLAY);
    CHECK_EQ_INT(lp.len, 2 * BAR);
    CHECK_EQ_INT(lp.sel, 1);                        /* the next empty track */
    run_to(end + 2 * BAR + 64);
    int i = find(PC_MIDI_NOTE_ON, 60, 0), j = find(PC_MIDI_NOTE_OFF, 60, 0);
    CHECK(i >= 0 && j > i);
    CHECK_EQ_INT(got[i].pos, blk(end + 1000));
    CHECK_EQ_INT(got[i].ev.b, 100);
    CHECK_EQ_INT(got[j].pos, blk(end + 24000));
    run_to(end + 4 * BAR + 64);                     /* and every pass after */
    i = find(PC_MIDI_NOTE_ON, 60, i + 1);
    CHECK(i >= 0);
    CHECK_EQ_INT(got[i].pos, blk(end + 2 * BAR + 1000));
    CHECK_EQ_INT(lp.passes, 2);
}

static void test_seam_and_preroll(void)
{
    fresh();
    int64_t start = BAR + 10;                       /* not on a block boundary */
    pc_lp_arm(&lp, start, BAR, BEAT, 1, 0);
    live(PC_MIDI_NOTE_ON, 0, 50, 90, start - 3000, 300);    /* an eighth of a beat early: position 0 */
    live(PC_MIDI_NOTE_ON, 0, 51, 90, start - 20000, 300);   /* count-in: not recorded */
    CHECK_EQ_INT(lp.cnt[0], 1);
    run_to(start + 200);
    live(PC_MIDI_NOTE_ON, 0, 52, 90, start + 3, 400);
    live(PC_MIDI_NOTE_ON, 0, 53, 90, start + BAR - 5, 0);   /* just before the seam */
    int64_t end = start + BAR;
    run_to(blk(end) + 64);                          /* the block holding the seam */
    CHECK(find(PC_MIDI_NOTE_ON, 53, 0) < 0);        /* the take part of that block stays silent */
    int a = find(PC_MIDI_NOTE_ON, 50, 0), b = find(PC_MIDI_NOTE_ON, 52, 0);
    CHECK(a >= 0 && b >= 0);                        /* the wrapped part plays: positions 0 and 3 */
    CHECK_EQ_INT(got[a].pos, blk(end));
    run_to(end + BAR + 64);
    int c = find(PC_MIDI_NOTE_ON, 53, 0);
    CHECK(c >= 0);
    CHECK_EQ_INT(got[c].pos, blk(end + BAR - 5));
    CHECK(find(PC_MIDI_NOTE_ON, 51, 0) < 0);
}

static void test_free_take(void)
{
    fresh();
    pc_lp_arm(&lp, BAR, BAR, BEAT, 0, 0);
    run_to(BAR + 64);
    live(PC_MIDI_NOTE_ON, 0, 60, 100, BAR + 5000, 300);
    run_to(BAR + 100000);
    pc_lp_end_take(&lp, BAR + 100000);              /* ends on the next bar line: 2 bars */
    run_to(BAR + 2 * BAR);
    CHECK_EQ_INT(lp.state, PC_LP_TAKE);
    run_to(BAR + 2 * BAR + 64);
    CHECK_EQ_INT(lp.state, PC_LP_PLAY);
    CHECK_EQ_INT(lp.len, 2 * BAR);
    /* no tap: the take stops at the longest whole number of bars that fits */
    fresh();
    CHECK_EQ_INT(pc_lp_max_len(&lp), NHEAD * PC_LP_BUCKET);
    pc_lp_arm(&lp, BAR, BAR, BEAT, 0, 0);
    run_to(BAR + 12 * (int64_t)BAR);
    CHECK_EQ_INT(lp.state, PC_LP_PLAY);
    CHECK_EQ_INT(lp.len, 10 * BAR);
    /* a fixed length that does not fit is refused */
    fresh();
    CHECK_EQ_INT(pc_lp_arm(&lp, BAR, BAR, BEAT, 16, 0), -1);
    CHECK_EQ_INT(lp.state, PC_LP_EMPTY);
}

static int64_t take_1bar(int track_events)           /* an empty or one-note 1-bar loop; returns where pass 2 starts */
{
    fresh();
    pc_lp_arm(&lp, BAR, BAR, BEAT, 1, 0);
    run_to(BAR + 64);
    if (track_events) {
        live(PC_MIDI_NOTE_ON, 0, 40, 100, BAR + 2000, 300);
        live(PC_MIDI_NOTE_OFF, 0, 40, 0, BAR + 50000, 300);
    }
    run_to(2 * BAR + 64);
    CHECK_EQ_INT(lp.state, PC_LP_PLAY);
    return 2 * BAR;
}

static void test_overdub_and_held(void)
{
    int64_t p2 = take_1bar(1);
    CHECK_EQ_INT(lp.sel, 1);
    live(PC_MIDI_NOTE_ON, 0, 70, 100, p2 + 1000, 300);       /* not recording: ignored */
    CHECK_EQ_INT(lp.cnt[1], 0);
    run_to(p2 + 10000);
    pc_lp_overdub(&lp, true, p2 + 9700);
    CHECK(lp.rec);
    live(PC_MIDI_NOTE_ON, 0, 72, 100, p2 + 20000, 300);
    run_to(p2 + 60000);
    pc_lp_overdub(&lp, false, p2 + 59700);            /* still held: an off at the punch-out */
    CHECK(!lp.rec);
    CHECK_EQ_INT(lp.cnt[1], 2);
    ngot = 0;
    run_to(p2 + 2 * BAR);
    int i = find(PC_MIDI_NOTE_ON, 72, 0), j = find(PC_MIDI_NOTE_OFF, 72, 0);
    CHECK(i >= 0 && j > i);
    CHECK_EQ_INT(got[i].pos, blk(p2 + BAR + 20000));
    CHECK_EQ_INT(got[j].pos, blk(p2 + BAR + 59700));
    CHECK(find(PC_MIDI_NOTE_ON, 70, 0) < 0);
    CHECK(find(PC_MIDI_NOTE_ON, 40, 0) >= 0);           /* track 1 still plays */
}

static void test_quantise(void)
{
    fresh();
    pc_lp_arm(&lp, BAR, BAR, BEAT, 1, 2);             /* 1/16 = 6000 */
    CHECK_EQ_INT(lp.grid, 6000);
    run_to(BAR + 64);
    live(PC_MIDI_NOTE_ON, 0, 60, 100, BAR + 19700, 300);     /* -> 18000 */
    live(PC_MIDI_NOTE_OFF, 0, 60, 0, BAR + 30000, 300);      /* moves with it: 28300 */
    live(PC_MIDI_NOTE_ON, 0, 61, 100, BAR + BAR - 1000, 400); /* -> the seam: position 0, skipped once */
    live(PC_MIDI_NOTE_OFF, 0, 61, 0, BAR + BAR - 200, 100);
    int64_t p2 = 2 * BAR;
    run_to(p2 + BAR);
    int i = find(PC_MIDI_NOTE_ON, 60, 0), j = find(PC_MIDI_NOTE_OFF, 60, 0);
    CHECK(i >= 0 && j >= 0);
    CHECK_EQ_INT(got[i].pos, blk(p2 + 18000));
    CHECK_EQ_INT(got[j].pos, blk(p2 + 28300));
    CHECK(find(PC_MIDI_NOTE_ON, 61, 0) < 0);         /* heard live just before: not again at once */
    run_to(p2 + 2 * BAR);
    int k = find(PC_MIDI_NOTE_ON, 61, 0);
    CHECK(k >= 0);
    CHECK_EQ_INT(got[k].pos, p2 + BAR);
    /* overdub, snapped ahead of the playhead: skipped in this pass, played from the next */
    int64_t p3 = p2 + BAR;
    run_to(p3 + 2 * BAR + 64);
    pc_lp_overdub(&lp, true, done);
    int64_t p5 = p3 + 2 * BAR;
    ngot = 0;
    live(PC_MIDI_NOTE_ON, 0, 80, 100, p5 + 41500, 800);     /* -> 42000: already passed, next pass */
    live(PC_MIDI_NOTE_ON, 0, 81, 100, p5 + 47000, 400);     /* -> 48000: ahead of the playhead */
    run_to(p5 + BAR);
    CHECK(find(PC_MIDI_NOTE_ON, 80, 0) < 0);
    CHECK(find(PC_MIDI_NOTE_ON, 81, 0) < 0);
    run_to(p5 + 2 * BAR);
    int a = find(PC_MIDI_NOTE_ON, 80, 0), b = find(PC_MIDI_NOTE_ON, 81, 0);
    CHECK(a >= 0 && b >= 0);
    CHECK_EQ_INT(got[a].pos, blk(p5 + BAR + 42000));
    CHECK_EQ_INT(got[b].pos, p5 + BAR + 48000);
    pc_lp_set_quant(&lp, 1);
    CHECK_EQ_INT(lp.grid, 12000);
    pc_lp_set_quant(&lp, 0);
    CHECK_EQ_INT(lp.grid, 0);
}

static void test_mute_clear_undo(void)
{
    int64_t p2 = take_1bar(1);
    /* sounding note: mute sends its off; muted tracks stay silent */
    run_to(p2 + 10000);
    ngot = 0;
    pc_lp_mute(&lp, 0, emit, NULL);
    CHECK_EQ_INT(ngot, 1);
    CHECK_EQ_INT(got[0].ev.type, PC_MIDI_NOTE_OFF);
    CHECK_EQ_INT(got[0].ev.a, 40);
    ngot = 0;
    run_to(p2 + 2 * BAR);
    CHECK(find(PC_MIDI_NOTE_ON, 40, 0) < 0);
    pc_lp_mute(&lp, 0, emit, NULL);
    /* an overdub pass on track 2, then undo it */
    pc_lp_overdub(&lp, true, done);
    live(PC_MIDI_NOTE_ON, 0, 64, 100, done + 1000, 300);
    live(PC_MIDI_NOTE_OFF, 0, 64, 0, done + 4000, 300);
    CHECK_EQ_INT(lp.cnt[1], 2);
    CHECK_EQ_INT(lp.undo_kind, PC_LP_UNDO_PASS);
    CHECK_EQ_INT(pc_lp_undo(&lp, emit, NULL), 1);
    CHECK(!lp.rec);                                  /* undo while recording ends the pass first */
    CHECK_EQ_INT(lp.cnt[1], 0);
    CHECK_EQ_INT(pc_lp_undo(&lp, emit, NULL), 0);   /* single level */
    ngot = 0;
    run_to(done + 3 * BAR);
    CHECK(find(PC_MIDI_NOTE_ON, 64, 0) < 0);
    CHECK(find(PC_MIDI_NOTE_ON, 40, 0) >= 0);
    /* clear track 1, undo the clear */
    CHECK_EQ_INT(pc_lp_clear(&lp, 0, emit, NULL), 2);
    CHECK_EQ_INT(lp.cnt[0], 0);
    CHECK(!pc_lp_has_content(&lp));
    CHECK_EQ_INT(pc_lp_clear(&lp, 0, emit, NULL), 0);   /* nothing: no new undo step */
    ngot = 0;
    run_to(done + 2 * BAR);
    CHECK(find(PC_MIDI_NOTE_ON, 40, 0) < 0);
    CHECK_EQ_INT(pc_lp_undo(&lp, emit, NULL), 1);
    CHECK_EQ_INT(lp.cnt[0], 2);
    ngot = 0;
    run_to(done + 2 * BAR);
    CHECK(find(PC_MIDI_NOTE_ON, 40, 0) >= 0);
    /* a pass recorded after a clear plays, the cleared events do not come back with it */
    pc_lp_clear(&lp, 0, emit, NULL);
    pc_lp_select(&lp, 0);
    pc_lp_overdub(&lp, true, done);
    live(PC_MIDI_NOTE_ON, 0, 45, 100, done + 3000, 300);
    pc_lp_overdub(&lp, false, done + 5000);
    CHECK_EQ_INT(lp.cnt[0], 2);
    ngot = 0;
    run_to(done + 2 * BAR);
    CHECK(find(PC_MIDI_NOTE_ON, 45, 0) >= 0);
    CHECK(find(PC_MIDI_NOTE_ON, 40, 0) < 0);
}

static void test_stop_play(void)
{
    int64_t p2 = take_1bar(1);
    run_to(p2 + 10000);                             /* note 40 sounding */
    ngot = 0;
    pc_lp_stop(&lp, done, emit, NULL);
    CHECK_EQ_INT(lp.state, PC_LP_STOP);
    CHECK_EQ_INT(ngot, 1);
    CHECK_EQ_INT(got[0].ev.type, PC_MIDI_NOTE_OFF);
    ngot = 0;
    run_to(done + 2 * BAR);
    CHECK_EQ_INT(ngot, 0);
    int64_t s = done + 64 + 7;
    pc_lp_play(&lp, s);
    CHECK_EQ_INT(lp.state, PC_LP_PLAY);
    run_to(s + BAR);
    int i = find(PC_MIDI_NOTE_ON, 40, 0);
    CHECK(i >= 0);
    CHECK_EQ_INT(got[i].pos, blk(s + 2000));
    /* stop during the count-in or the take drops it */
    fresh();
    pc_lp_arm(&lp, BAR, BAR, BEAT, 1, 0);
    run_to(BAR + 64);
    live(PC_MIDI_NOTE_ON, 0, 40, 100, BAR + 100, 300);
    pc_lp_stop(&lp, done, emit, NULL);
    CHECK_EQ_INT(lp.state, PC_LP_EMPTY);
    CHECK_EQ_INT(lp.cnt[0], 0);
    CHECK_EQ_INT(lp.n, 0);
}

static void test_other_events(void)
{
    fresh();
    pc_lp_arm(&lp, BAR, BAR, BEAT, 1, 2);
    run_to(BAR + 64);
    pc_midi_ev_t e = ev(PC_MIDI_BEND, 2, 0, 0);
    e.bend = -1234;
    run_to(BAR + 3300);
    pc_lp_record(&lp, &e, BAR + 3000);
    live(PC_MIDI_CC, 2, 64, 127, BAR + 4000, 300);   /* sustain down */
    live(PC_MIDI_CC, 2, 123, 0, BAR + 4100, 300);    /* channel mode: never recorded */
    live(PC_MIDI_PROGRAM, 2, 5, 0, BAR + 4200, 300);
    live(PC_MIDI_NOTE_ON, 16, 36, 100, BAR + 5000, 300);   /* a pad */
    pc_midi_ev_t clk = ev(PC_MIDI_CLOCK, 0, 0, 0);
    pc_lp_record(&lp, &clk, BAR + 5100);
    CHECK_EQ_INT(lp.cnt[0], 4);
    run_to(2 * BAR + BAR);
    int b = -1, c = -1, p = -1, pad = -1;
    for (int i = 0; i < ngot; i++) {
        if (got[i].ev.type == PC_MIDI_BEND) b = i;
        if (got[i].ev.type == PC_MIDI_CC) c = i;
        if (got[i].ev.type == PC_MIDI_PROGRAM) p = i;
        if (got[i].ev.type == PC_MIDI_NOTE_ON && got[i].ev.ch == 16) pad = i;
    }
    CHECK(b >= 0 && c >= 0 && p >= 0 && pad >= 0);
    CHECK_EQ_INT(got[b].ev.bend, -1234);
    CHECK_EQ_INT(got[b].pos, blk(2 * BAR + 3000));    /* not quantised */
    CHECK_EQ_INT(got[c].ev.a, 64);
    CHECK_EQ_INT(got[p].ev.a, 5);
    CHECK_EQ_INT(got[pad].pos, blk(2 * BAR + 6000));  /* quantised */
    /* mute releases a sustain the playback holds down */
    ngot = 0;
    pc_lp_mute(&lp, 0, emit, NULL);
    CHECK(ngot >= 1);
    CHECK_EQ_INT(got[ngot - 1].ev.type, PC_MIDI_CC);
    CHECK_EQ_INT(got[ngot - 1].ev.a, 64);
    CHECK_EQ_INT(got[ngot - 1].ev.b, 0);
}

static void test_save_load(void)
{
    int64_t p2 = take_1bar(1);
    pc_lp_overdub(&lp, true, p2 + 100);
    live(PC_MIDI_NOTE_ON, 3, 77, 55, p2 + 30000, 300);
    pc_lp_overdub(&lp, false, p2 + 40000);
    pc_lp_mute(&lp, 1, emit, NULL);
    pc_lp_stop(&lp, done, emit, NULL);
    pc_lp_meta_t m = { 48000, lp.len, 12000, 4, 1, 2, lp.mute, "fm" }, m2;
    static uint8_t buf[65536];
    size_t n = pc_lp_save(&lp, &m, buf, sizeof buf);
    CHECK(n > 0);
    CHECK_EQ_INT(n, pc_lp_save_size(&lp));
    CHECK_EQ_INT(pc_lp_save(&lp, &m, buf, 10), 0);
    uint32_t c0 = lp.cnt[0], c1 = lp.cnt[1], len = lp.len;
    fresh();
    CHECK_EQ_INT(pc_lp_load(&lp, &m2, buf, n), 0);
    CHECK_EQ_INT(lp.state, PC_LP_STOP);
    CHECK_EQ_INT(lp.len, len);
    CHECK_EQ_INT(lp.cnt[0], c0);
    CHECK_EQ_INT(lp.cnt[1], c1);
    CHECK_EQ_INT(lp.mute, 2);
    CHECK_EQ_INT(m2.bpm_x100, 12000);
    CHECK_EQ_INT(m2.bpb, 4);
    CHECK_STR(m2.chip, "fm");
    CHECK_EQ_INT(lp.undo_kind, PC_LP_UNDO_NONE);
    pc_lp_mute(&lp, 1, emit, NULL);
    pc_lp_play(&lp, done);
    ngot = 0;
    run_to(done + BAR + 64);
    int i = find(PC_MIDI_NOTE_ON, 77, 0);
    CHECK(i >= 0);
    CHECK_EQ_INT(got[i].ev.ch, 3);
    CHECK_EQ_INT(got[i].ev.b, 55);
    buf[0] = 'X';
    CHECK_EQ_INT(pc_lp_load(&lp, &m2, buf, n), -1);
    buf[0] = 'J';
    CHECK_EQ_INT(pc_lp_load(&lp, &m2, buf, n - 3), -1);    /* truncated */
}

static void test_full(void)
{
    fresh();
    pc_lp_init(&lp, pool, 8, heads, NHEAD);
    pc_lp_arm(&lp, BAR, BAR, BEAT, 1, 0);
    run_to(BAR + 64);
    for (int k = 0; k < 12; k++) live(PC_MIDI_NOTE_ON, 0, 30 + k, 100, BAR + 1000 + k * 100, 300);
    CHECK_EQ_INT(lp.n, 8);
    CHECK(lp.full);
    run_to(3 * BAR);
    CHECK(ngot >= 8);
}

static void test_downbeat(void)
{
    /* beat 7 (the last of bar 2) at 120000: downbeats at 144000, 240000 ... */
    CHECK_EQ_INT(pc_lp_next_downbeat(100000, 120000, 7, BEAT, 4, 0), 144000);
    CHECK_EQ_INT(pc_lp_next_downbeat(100000, 120000, 7, BEAT, 4, BAR - BEAT / 2), 240000);
    CHECK_EQ_INT(pc_lp_next_downbeat(100000, 120000, 4, BEAT, 4, 0), 120000);
    CHECK_EQ_INT(pc_lp_next_downbeat(100000, 120000, 7, 16000.5, 3, 0), 152001);
}

static int mod_cell(const uint8_t *m, int channels, int row, int ch, int *period, int *ins, int *fx, int *arg)
{
    const uint8_t *c = m + 1084 + (row / 64) * 64 * channels * 4 + (row % 64) * channels * 4 + ch * 4;
    *period = (c[0] & 0x0F) << 8 | c[1];
    *ins = (c[0] & 0xF0) | c[2] >> 4;
    *fx = c[2] & 0x0F;
    *arg = c[3];
    return *period || *ins || *fx || *arg;
}

static void test_mod(void)
{
    fresh();
    pc_lp_arm(&lp, BAR, BAR, BEAT, 1, 0);
    run_to(BAR + 64);
    live(PC_MIDI_NOTE_ON, 0, 60, 127, BAR + 6000, 300);       /* row 1, C-2 */
    live(PC_MIDI_NOTE_OFF, 0, 60, 0, BAR + 24000, 300);       /* row 4: C00 */
    live(PC_MIDI_NOTE_ON, 9, 36, 100, BAR + 12000, 300);      /* row 2, drums: noise */
    run_to(2 * BAR + 64);
    static uint8_t out[200000];
    pc_lp_mod_info_t in;
    size_t n = pc_lp_mod(&lp, 120.0, 4, out, sizeof out, &in);
    CHECK(n > 1084);
    CHECK(!memcmp(out + 1080, "M.K.", 4));
    CHECK_EQ_INT(in.channels, 4);
    CHECK_EQ_INT(in.rows, 16);
    CHECK_EQ_INT(in.patterns, 1);
    CHECK_EQ_INT(out[950], 1);
    CHECK_EQ_INT(in.notes, 2);
    int per, ins, fx, arg;
    CHECK(mod_cell(out, 4, 1, 0, &per, &ins, &fx, &arg));
    CHECK_EQ_INT(per, 428);                         /* MIDI 60 = ProTracker C-2 */
    CHECK_EQ_INT(ins, 1);
    CHECK_EQ_INT(fx, 0xC);
    CHECK_EQ_INT(arg, 64);
    CHECK(mod_cell(out, 4, 4, 0, &per, &ins, &fx, &arg));
    CHECK_EQ_INT(per, 0);
    CHECK_EQ_INT(fx, 0xC);
    CHECK_EQ_INT(arg, 0);
    CHECK(mod_cell(out, 4, 2, 1, &per, &ins, &fx, &arg));
    CHECK_EQ_INT(ins, 2);
    CHECK(mod_cell(out, 4, 0, 2, &per, &ins, &fx, &arg));    /* row 0, last two channels: speed 6, tempo 120 */
    CHECK_EQ_INT(fx, 0xF);
    CHECK_EQ_INT(arg, 6);
    CHECK(mod_cell(out, 4, 0, 3, &per, &ins, &fx, &arg));
    CHECK_EQ_INT(arg, 120);
    CHECK_EQ_INT(out[20 + 22], 0);                 /* sample 1 length in words, big-endian */
    CHECK(out[20 + 23] > 0);
    CHECK_EQ_INT(n, 1084 + 1024 + (out[20 + 23] + out[50 + 23] * 1 + (out[50 + 22] << 8)) * 2);
    /* five notes at once: 8 channels */
    pc_lp_overdub(&lp, true, done);
    for (int k = 0; k < 5; k++) live(PC_MIDI_NOTE_ON, 0, 64 + k, 100, done + 100, 300);
    pc_lp_overdub(&lp, false, done + 200);
    n = pc_lp_mod(&lp, 120.0, 4, out, sizeof out, &in);
    CHECK(!memcmp(out + 1080, "8CHN", 4));
    CHECK_EQ_INT(in.channels, 8);
    CHECK_EQ_INT(pc_lp_mod(&lp, 120.0, 4, out, 2000, &in), 0);   /* no room */
    /* tempo out of the Fxx range: speed changes, rows stay 4 a beat */
    n = pc_lp_mod(&lp, 300.0, 4, out, sizeof out, &in);
    mod_cell(out, 8, 0, 6, &per, &ins, &fx, &arg);
    CHECK_EQ_INT(arg, 5);                           /* 250 * 24 / 5 = 1200 rows a minute = 300 * 4 */
    mod_cell(out, 8, 0, 7, &per, &ins, &fx, &arg);
    CHECK_EQ_INT(arg, 250);
}

/* changing the selected track while a note / the pedal is held: the off goes with its on */
static void test_select_mid_note(void)
{
    int64_t p2 = take_1bar(1);
    CHECK_EQ_INT(lp.sel, 1);
    run_to(p2 + 1000);
    pc_lp_overdub(&lp, true, p2 + 700);
    live(PC_MIDI_NOTE_ON, 0, 72, 100, p2 + 5000, 300);
    live(PC_MIDI_CC, 0, 64, 127, p2 + 6000, 300);
    pc_lp_select(&lp, 2);
    live(PC_MIDI_NOTE_OFF, 0, 72, 0, p2 + 20000, 300);
    live(PC_MIDI_CC, 0, 64, 0, p2 + 21000, 300);
    live(PC_MIDI_NOTE_ON, 0, 74, 100, p2 + 30000, 300);     /* on track 2 ... */
    pc_lp_select(&lp, 3);
    run_to(p2 + 60000);
    pc_lp_overdub(&lp, false, p2 + 59700);                    /* ... and its punch-out off too */
    CHECK_EQ_INT(lp.cnt[1], 4);
    CHECK_EQ_INT(lp.cnt[2], 2);
    CHECK_EQ_INT(lp.cnt[3], 0);
    /* muting track 1 mid-note silences it: nothing of it left sounding */
    ngot = 0;
    run_to(p2 + BAR + 10000);
    CHECK(find(PC_MIDI_NOTE_ON, 72, 0) >= 0);
    pc_lp_mute(&lp, 1, emit, NULL);
    CHECK(find(PC_MIDI_NOTE_OFF, 72, 0) >= 0);
    ngot = 0;
    run_to(p2 + 2 * BAR + 64000);
    CHECK(find(PC_MIDI_NOTE_ON, 72, 0) < 0);
    CHECK(find(PC_MIDI_NOTE_ON, 74, 0) >= 0);
}

/* a .jcl with bytes no MIDI event can have: those events are dropped, the rest loads */
static void test_load_validate(void)
{
    int64_t p2 = take_1bar(1);
    pc_lp_overdub(&lp, true, p2 + 100);
    live(PC_MIDI_NOTE_ON, 16, 38, 90, p2 + 30000, 300);      /* a pad */
    live(PC_MIDI_NOTE_OFF, 16, 38, 0, p2 + 31000, 300);
    pc_lp_overdub(&lp, false, p2 + 40000);
    pc_lp_stop(&lp, done, emit, NULL);
    pc_lp_meta_t m = { 48000, lp.len, 12000, 4, 1, 0, 0, "nes" }, m2;
    static uint8_t buf[4096];
    size_t n = pc_lp_save(&lp, &m, buf, sizeof buf);
    CHECK_EQ_INT(n, 36 + 4 * 10);
    for (int k = 0; k < 4; k++) {
        uint8_t *e = buf + 36 + k * 10;
        if (e[5] == 0 && e[4] == PC_MIDI_NOTE_ON) e[6] = 200;                 /* note 200 */
        if (e[5] == 16 && e[4] == PC_MIDI_NOTE_OFF) { e[4] = PC_MIDI_CC; e[6] = 7; }   /* a CC on the pad channel */
        if (e[5] == 16 && e[4] == PC_MIDI_NOTE_ON) e[7] = 255;                 /* velocity 255 */
    }
    fresh();
    CHECK_EQ_INT(pc_lp_load(&lp, &m2, buf, n), 0);
    CHECK_EQ_INT(lp.cnt[0], 1);                                 /* only the ch 1 note-off is left */
    CHECK_EQ_INT(lp.cnt[1], 0);
}

/* reset does not clear the bucket heads at once (up to 128 KB: too slow for one audio block): wipe steps do,
 * and arm / load finish whatever is left first */
static void test_reset_lazy(void)
{
    take_1bar(1);
    uint32_t b = (uint32_t)(2000 / PC_LP_BUCKET);
    CHECK(heads[b] != PC_LP_NIL);
    pc_lp_reset(&lp);
    CHECK(heads[b] != PC_LP_NIL);                               /* not touched yet */
    uint32_t left = lp.wipe;
    CHECK(left > b);
    CHECK_EQ_INT(pc_lp_wipe(&lp, 10), left - 10);
    /* armed before the wipe is done: still a clean take */
    CHECK_EQ_INT(pc_lp_arm(&lp, done + BAR, BAR, BEAT, 1, 0), 0);
    CHECK_EQ_INT(lp.wipe, 0);
    CHECK(heads[b] == PC_LP_NIL);
    int64_t s0 = done + BAR;
    run_to(s0 + 2 * BAR + 64);
    CHECK_EQ_INT(lp.state, PC_LP_PLAY);
    ngot = 0;
    run_to(s0 + 3 * BAR + 64);
    CHECK_EQ_INT(ngot, 0);                                      /* nothing of the old loop */
    pc_lp_reset(&lp);
    while (pc_lp_wipe(&lp, 7)) ;
    for (uint32_t i = 0; i < NHEAD; i++) CHECK(heads[i] == PC_LP_NIL);
}

int main(void)
{
    RUN(test_fixed_take);
    RUN(test_seam_and_preroll);
    RUN(test_free_take);
    RUN(test_overdub_and_held);
    RUN(test_quantise);
    RUN(test_mute_clear_undo);
    RUN(test_stop_play);
    RUN(test_other_events);
    RUN(test_save_load);
    RUN(test_full);
    RUN(test_downbeat);
    RUN(test_mod);
    RUN(test_select_mid_note);
    RUN(test_load_validate);
    RUN(test_reset_lazy);
    return 0;
}
