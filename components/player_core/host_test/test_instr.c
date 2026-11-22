// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc_test.h"
#include "pc/pc_instr.h"
#include "pc/pc_ledfx.h"

/* a fake chip: logs every call; voice 0..2 melodic, 3 percussion */
static char logbuf[2048];
static int vnote[3] = { -1, -1, -1 };
static void lg(const char *fmt, int a, int b, int c)
{
    char t[64];
    snprintf(t, sizeof t, fmt, a, b, c);
    strncat(logbuf, t, sizeof logbuf - strlen(logbuf) - 1);
}
static void on(int ch, int n, int v) { lg("on%d.%d.%d ", ch, n, v); if (ch != 9) vnote[n % 3] = n; }
static void off(int ch, int n) { lg("off%d.%d%.0d ", ch, n, 0); if (vnote[n % 3] == n) vnote[n % 3] = -1; }
static void prog(int ch, int p) { lg("pc%d.%d%.0d ", ch, p, 0); }
static void alloff(void) { lg("alloff%.0d%.0d%.0d ", 0, 0, 0); for (int i = 0; i < 3; i++) vnote[i] = -1; }
static void ond(int v, int n, int vel) { lg("ond%d.%d.%d ", v, n, vel); }
static void offd(int v, int n) { lg("offd%d.%d%.0d ", v, n, 0); }
static void ongm(int ch, int n, int v) { lg("ongm%d.%d.%d ", ch, n, v); vnote[n % 3] = n; }
static void pcgm(int ch, int p) { lg("pcgm%d.%d%.0d ", ch, p, 0); }
static void bend(int ch, int c) { lg("bend%d.%d%.0d ", ch, c, 0); }
static int vcount(void) { return 4; }
static int vnote_f(int i) { return i < 3 ? vnote[i] : -1; }
static const pc_synth_t fake = { .id = "fake", .name = "Fake", .note_on = on, .note_off = off, .program_change = prog,
    .all_off = alloff, .note_on_direct = ond, .note_off_direct = offd, .note_on_gm = ongm, .program_change_gm = pcgm,
    .pitch_bend = bend, .voice_count = vcount, .voice_note = vnote_f };

static pc_midi_ev_t E(int type, int ch, int a, int b) { pc_midi_ev_t e = { (uint8_t)type, (uint8_t)ch, (uint8_t)a, (uint8_t)b, 0, 0 }; return e; }
static void ev(pc_instr_t *r, int type, int ch, int a, int b) { pc_midi_ev_t e = E(type, ch, a, b); pc_instr_event(r, &e); }
static void reset_log(void) { logbuf[0] = 0; }

static void test_auto_and_voice(void)
{
    pc_instr_t r;
    pc_instr_init(&r, &fake, PC_MAP_AUTO, -1);
    reset_log();
    pc_midi_ev_t e = E(PC_MIDI_NOTE_ON, 2, 61, 90);
    CHECK_EQ_INT(pc_instr_event(&r, &e), 1);          /* 61 % 3: the fake puts it on voice 1 */
    e = E(PC_MIDI_NOTE_ON, 9, 36, 100);
    CHECK_EQ_INT(pc_instr_event(&r, &e), 3);          /* percussion: the last voice */
    e = E(PC_MIDI_NOTE_OFF, 2, 61, 0);
    CHECK_EQ_INT(pc_instr_event(&r, &e), -1);
    ev(&r, PC_MIDI_PROGRAM, 0, 5, 0);
    CHECK_STR(logbuf, "on2.61.90 on9.36.100 off2.61 pc0.5 ");
    CHECK_EQ_INT(r.last_note, 36);
}

static void test_channel_filter(void)
{
    pc_instr_t r;
    pc_instr_init(&r, &fake, PC_MAP_AUTO, 3);         /* MIDI channel 4 only */
    reset_log();
    ev(&r, PC_MIDI_NOTE_ON, 0, 60, 1);
    ev(&r, PC_MIDI_NOTE_ON, 9, 36, 1);
    ev(&r, PC_MIDI_NOTE_ON, 3, 62, 1);
    CHECK_STR(logbuf, "on3.62.1 ");
}

static void test_direct_and_gm(void)
{
    pc_instr_t r;
    pc_instr_init(&r, &fake, PC_MAP_DIRECT, 5);        /* the channel filter does not apply in Direct */
    reset_log();
    ev(&r, PC_MIDI_NOTE_ON, 1, 60, 10);
    ev(&r, PC_MIDI_NOTE_ON, 3, 60, 10);                /* voice 3 is percussion: not a direct voice */
    ev(&r, PC_MIDI_NOTE_ON, 9, 40, 10);
    ev(&r, PC_MIDI_NOTE_OFF, 1, 60, 0);
    ev(&r, PC_MIDI_PROGRAM, 1, 7, 0);                  /* ignored in Direct */
    CHECK_STR(logbuf, "ond1.60.10 on9.40.10 offd1.60 ");
    reset_log();
    pc_instr_set_mode(&r, PC_MAP_GM);                  /* a mode change silences everything */
    ev(&r, PC_MIDI_NOTE_ON, 1, 40, 10);                /* GM: the channel filter (5) applies again */
    CHECK_STR(logbuf, "alloff ");
    pc_instr_set_channel(&r, -1);
    reset_log();
    ev(&r, PC_MIDI_PROGRAM, 1, 33, 0);
    ev(&r, PC_MIDI_NOTE_ON, 1, 40, 10);
    ev(&r, PC_MIDI_NOTE_OFF, 1, 40, 0);
    CHECK_STR(logbuf, "pcgm1.33 ongm1.40.10 off1.40 ");
}

static void test_sustain(void)
{
    pc_instr_t r;
    pc_instr_init(&r, &fake, PC_MAP_AUTO, -1);
    reset_log();
    ev(&r, PC_MIDI_NOTE_ON, 0, 60, 1);
    ev(&r, PC_MIDI_CC, 0, 64, 127);                    /* pedal down */
    ev(&r, PC_MIDI_NOTE_OFF, 0, 60, 0);                /* held by the pedal */
    ev(&r, PC_MIDI_NOTE_ON, 0, 64, 1);
    ev(&r, PC_MIDI_NOTE_OFF, 0, 64, 0);
    ev(&r, PC_MIDI_NOTE_ON, 1, 70, 1);
    ev(&r, PC_MIDI_NOTE_OFF, 1, 70, 0);                /* other channel: no pedal */
    CHECK_STR(logbuf, "on0.60.1 on0.64.1 on1.70.1 off1.70 ");
    reset_log();
    ev(&r, PC_MIDI_NOTE_ON, 0, 60, 2);                 /* the same note again: old one released first */
    CHECK_STR(logbuf, "off0.60 on0.60.2 ");
    reset_log();
    ev(&r, PC_MIDI_CC, 0, 64, 0);                      /* pedal up: 64 goes; 60 is held by the key again */
    CHECK_STR(logbuf, "off0.64 ");
    reset_log();
    ev(&r, PC_MIDI_NOTE_OFF, 0, 60, 0);
    CHECK_STR(logbuf, "off0.60 ");
}

static void test_cc_and_bend(void)
{
    pc_instr_t r;
    pc_instr_init(&r, &fake, PC_MAP_AUTO, -1);
    CHECK_EQ_INT(r.volume, 100);
    ev(&r, PC_MIDI_CC, 0, 7, 64);
    CHECK_EQ_INT(r.volume, 64);
    reset_log();
    pc_midi_ev_t e = E(PC_MIDI_BEND, 2, 0, 0);
    e.bend = 8191;
    pc_instr_event(&r, &e);                            /* +2 semitones (default range) */
    e.bend = -8192;
    pc_instr_event(&r, &e);
    CHECK_STR(logbuf, "bend2.199 bend2.-200 ");
    reset_log();
    ev(&r, PC_MIDI_CC, 0, 64, 127);
    ev(&r, PC_MIDI_NOTE_ON, 0, 60, 1);
    ev(&r, PC_MIDI_NOTE_OFF, 0, 60, 0);
    ev(&r, PC_MIDI_CC, 2, 121, 0);                     /* reset controllers on ch 2: its bend back to 0 */
    ev(&r, PC_MIDI_CC, 0, 123, 0);                     /* all notes off: the pedal-held 60 goes too */
    CHECK_STR(logbuf, "on0.60.1 bend2.0 alloff ");
    reset_log();
    ev(&r, PC_MIDI_CC, 0, 64, 0);                      /* nothing left held */
    ev(&r, PC_MIDI_CC, 0, 120, 0);
    CHECK_STR(logbuf, "alloff ");
    pc_instr_all_off(&r);                              /* disconnect / exit */
    pc_instr_set_chip(&r, &fake);
}

static void test_cfg(void)
{
    pc_instr_cfg_t c;
    pc_instr_cfg_default(&c);
    CHECK_STR(c.chip, "nes"); CHECK_EQ_INT(c.map, PC_MAP_AUTO); CHECK_EQ_INT(c.channel, -1);
    CHECK_EQ_INT(c.volume, 40); CHECK_EQ_INT(c.stable, 0); CHECK_EQ_INT(c.knob_volume, 0);
    char b[256];
    pc_instr_cfg_format(&c, b, sizeof b);
    CHECK_STR(b, "chip=nes map=auto ch=0 vol=40 timing=now knob=page kit=demo layer=auto clock=int bpm=120 bpb=4 click=on beathap=on fm=0 lbars=2 lq=0 fx=pitch");
    pc_instr_cfg_t d;
    pc_instr_cfg_parse("chip=gb map=gm ch=10 vol=55 timing=stable knob=volume", &d);
    CHECK_STR(d.chip, "gb"); CHECK_EQ_INT(d.map, PC_MAP_GM); CHECK_EQ_INT(d.channel, 9);
    CHECK_EQ_INT(d.volume, 55); CHECK_EQ_INT(d.stable, 1); CHECK_EQ_INT(d.knob_volume, 1);
    pc_instr_cfg_parse("garbage vol=500 map=nope ch=99", &d);   /* defaults for anything bad */
    CHECK_STR(d.chip, "nes"); CHECK_EQ_INT(d.volume, 100); CHECK_EQ_INT(d.map, PC_MAP_AUTO); CHECK_EQ_INT(d.channel, -1);
    pc_instr_cfg_parse(NULL, &d);
    CHECK_EQ_INT(d.volume, 40);
    CHECK_STR(pc_instr_map_name(PC_MAP_DIRECT), "direct");
    /* stage 3-4 values; an old (stage 2) value keeps the new ones at their defaults */
    pc_instr_cfg_parse("chip=gb kit=My|Drums/acoustic layer=both clock=ext bpm=97 bpb=7 click=off beathap=off", &d);
    CHECK_STR(d.kit, "My Drums/acoustic"); CHECK_EQ_INT(d.layer, PC_LAYER_BOTH); CHECK_EQ_INT(d.clock_ext, 1);
    CHECK_EQ_INT(d.bpm, 97); CHECK_EQ_INT(d.bpb, 7); CHECK_EQ_INT(d.click, 0); CHECK_EQ_INT(d.beat_haptic, 0);
    pc_instr_cfg_format(&d, b, sizeof b);
    pc_instr_cfg_parse(b, &c);
    CHECK_STR(c.kit, "My Drums/acoustic"); CHECK_EQ_INT(c.bpm, 97); CHECK_EQ_INT(c.layer, PC_LAYER_BOTH);
    pc_instr_cfg_parse("chip=gb map=gm ch=10 vol=55 timing=stable knob=volume", &d);
    CHECK_STR(d.kit, "demo"); CHECK_EQ_INT(d.bpm, 120); CHECK_EQ_INT(d.bpb, 4); CHECK_EQ_INT(d.click, 1);
    pc_instr_cfg_parse("bpm=1000 bpb=9 kit=-", &d);
    CHECK_EQ_INT(d.bpm, 120); CHECK_EQ_INT(d.bpb, 4); CHECK_STR(d.kit, "");
    /* stage 5-6: FM program, looper bars (0 = free) and quantise */
    CHECK_EQ_INT(d.fm, 0); CHECK_EQ_INT(d.loop_bars, 2); CHECK_EQ_INT(d.loop_q, 0);
    pc_instr_cfg_parse("fm=21 lbars=0 lq=2", &d);
    CHECK_EQ_INT(d.fm, 21); CHECK_EQ_INT(d.loop_bars, 0); CHECK_EQ_INT(d.loop_q, 2);
    pc_instr_cfg_format(&d, b, sizeof b);
    pc_instr_cfg_parse(b, &c);
    CHECK_EQ_INT(c.fm, 21); CHECK_EQ_INT(c.loop_bars, 0); CHECK_EQ_INT(c.loop_q, 2);
    pc_instr_cfg_parse("fm=200 lbars=3 lq=7", &d);
    CHECK_EQ_INT(d.fm, 0); CHECK_EQ_INT(d.loop_bars, 2); CHECK_EQ_INT(d.loop_q, 0);
    /* last stage: the ring effect */
    CHECK_EQ_INT(d.fx, PC_FX_PITCH);
    pc_instr_cfg_parse("fx=comet", &d);
    CHECK_EQ_INT(d.fx, PC_FX_COMET);
    pc_instr_cfg_format(&d, b, sizeof b);
    pc_instr_cfg_parse(b, &c);
    CHECK_EQ_INT(c.fx, PC_FX_COMET);
    pc_instr_cfg_parse("fx=disco", &d);
    CHECK_EQ_INT(d.fx, PC_FX_PITCH);
}

static void test_route(void)
{
    /* auto: drums to the kit whatever the channel setting; nothing to the kit without one */
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_AUTO, -1, 9, true), PC_ROUTE_KIT);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_AUTO, 0, 9, true), PC_ROUTE_KIT);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_AUTO, -1, 9, false), PC_ROUTE_CHIP);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_AUTO, 0, 9, false), 0);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_AUTO, -1, 0, true), PC_ROUTE_CHIP);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_KIT, -1, 0, true), PC_ROUTE_KIT);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_KIT, 2, 0, true), 0);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_BOTH, -1, 3, true), PC_ROUTE_CHIP | PC_ROUTE_KIT);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_BOTH, -1, 3, false), PC_ROUTE_CHIP);
    CHECK_EQ_INT(pc_layer_route(PC_LAYER_CHIP, -1, 9, true), PC_ROUTE_CHIP);
    CHECK_STR(pc_layer_name(PC_LAYER_KIT), "kit");
}

/* all off (disconnect, stop, CC123) also lets the pedal up: a pedal-up lost with the link never comes */
static void test_all_off_pedal(void)
{
    pc_instr_t r;
    pc_instr_init(&r, &fake, PC_MAP_AUTO, -1);
    ev(&r, PC_MIDI_CC, 0, 64, 127);
    pc_instr_all_off(&r);
    CHECK(!r.pedal[0]);
    reset_log();
    ev(&r, PC_MIDI_NOTE_ON, 0, 60, 1);
    ev(&r, PC_MIDI_NOTE_OFF, 0, 60, 0);                /* not held any more */
    CHECK_STR(logbuf, "on0.60.1 off0.60 ");
}

/* note / velocity bytes outside 0..127 (a bad file): ignored, the pedal-held bits of other channels untouched */
static void test_note_range(void)
{
    pc_instr_t r;
    pc_instr_init(&r, &fake, PC_MAP_AUTO, -1);
    ev(&r, PC_MIDI_CC, 0, 64, 127);
    ev(&r, PC_MIDI_CC, 1, 64, 127);
    reset_log();
    ev(&r, PC_MIDI_NOTE_ON, 0, 200, 1);
    ev(&r, PC_MIDI_NOTE_OFF, 0, 200, 0);
    ev(&r, PC_MIDI_NOTE_ON, 0, 60, 200);
    ev(&r, PC_MIDI_CC, 1, 64, 0);
    CHECK_STR(logbuf, "");
}

/* a note-off follows its note-on's layer, whatever the layer / kit is now */
static void test_noteroute(void)
{
    static pc_noteroute_t nr;
    pc_noteroute_reset(&nr);
    pc_midi_ev_t on = E(PC_MIDI_NOTE_ON, 0, 60, 100), off = E(PC_MIDI_NOTE_OFF, 0, 60, 0);
    CHECK_EQ_INT(pc_noteroute(&nr, &on, PC_ROUTE_CHIP), PC_ROUTE_CHIP);
    CHECK_EQ_INT(pc_noteroute(&nr, &off, PC_ROUTE_KIT), PC_ROUTE_CHIP);   /* layer went to kit meanwhile */
    CHECK_EQ_INT(pc_noteroute(&nr, &off, PC_ROUTE_KIT), PC_ROUTE_KIT);    /* nothing open: as now */
    pc_midi_ev_t d = E(PC_MIDI_NOTE_ON, 9, 36, 100), doff = E(PC_MIDI_NOTE_OFF, 9, 36, 0);
    pc_noteroute(&nr, &d, PC_ROUTE_CHIP);                                 /* auto, no kit yet */
    CHECK_EQ_INT(pc_noteroute(&nr, &doff, PC_ROUTE_KIT), PC_ROUTE_CHIP);  /* the kit came in */
    pc_noteroute(&nr, &on, PC_ROUTE_CHIP | PC_ROUTE_KIT);
    pc_noteroute_reset(&nr);                                              /* all off */
    CHECK_EQ_INT(pc_noteroute(&nr, &off, 0), 0);
    pc_midi_ev_t bad = E(PC_MIDI_NOTE_ON, 0, 200, 1);
    CHECK_EQ_INT(pc_noteroute(&nr, &bad, PC_ROUTE_CHIP), 0);
    pc_midi_ev_t cc = E(PC_MIDI_CC, 0, 64, 1);
    CHECK_EQ_INT(pc_noteroute(&nr, &cc, PC_ROUTE_CHIP), PC_ROUTE_CHIP);
}

int main(void)
{
    RUN(test_auto_and_voice);
    RUN(test_channel_filter);
    RUN(test_direct_and_gm);
    RUN(test_sustain);
    RUN(test_cc_and_bend);
    RUN(test_cfg);
    RUN(test_route);
    RUN(test_all_off_pedal);
    RUN(test_note_range);
    RUN(test_noteroute);
    return 0;
}
