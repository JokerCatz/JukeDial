// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes inst_engine (the instrument's audio engine: one task on core 1, 48 kHz, BLE and UI event rings, chip render, metronome, looper). */
/* Behaviour: settings are remembered and each change prints a "SIM inst ..." line; the metronome and the looper are state machines read lazily from the virtual clock (a loop fixes the tempo: inst_metro_t.loop; an external clock never locks, so a take is refused with NOCLOCK); notes from siminst note come out of the note queue. */
/* Empty: all audio (the DAC, the chips' registers, the kit sampler), statistics, the BLE timestamp mapping, jitter and timing measurements. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "inst_engine.h"
#include "inst_loopfile.h"
#include "inst_chip.h"
#include "pc/pc_looper.h"
#include "sim.h"

static struct { int chip, vol, ch, layer, bpm, bpb, bars; pc_map_t map; bool stable, click, ext, run; } E = { .vol = 100, .ch = -1, .bpm = 120, .bpb = 4 };
static int64_t s_run_t0;               /* the metronome's first beat */
static uint32_t s_beats_done;          /* beats of earlier runs, so the sequence number only goes up */
static inst_note_t s_q[16];
static int s_qn, s_last = -1;
static int8_t s_held[128];
static int s_nheld, s_held_note[16];

static int64_t beat_us(void) { return 60000000LL / (E.bpm > 0 ? E.bpm : 120); }
static uint32_t beats_now(void) { return E.run ? (uint32_t)((sim_now_us() - s_run_t0) / beat_us()) + 1 : 0; }

static void print_metro(void) { printf("SIM inst metro running=%d bpm=%d bpb=%d\n", E.run, E.bpm, E.bpb); }

/* ---- looper: empty -> count-in (one bar) -> take (until the next tap) -> play; overdub, mute, clear, undo, reset ---- */
static struct { int state, sel, bars; bool rec, can_undo; uint8_t mute; uint32_t cnt[4], events; int64_t t0, len_us; } L;
static void lp_update(void)
{
    int64_t now = sim_now_us(), bar = beat_us() * E.bpb;
    if (L.state == PC_LP_COUNTIN && now - L.t0 >= bar) { L.t0 += bar; L.state = PC_LP_TAKE; }
    if (L.state == PC_LP_TAKE && L.bars > 0 && now - L.t0 >= bar * L.bars) { L.len_us = bar * L.bars; L.t0 += L.len_us; L.state = PC_LP_PLAY; }
}
static int s_loop_msg;                 /* INST_LOOP_MSG_*, read once by inst_engine_loop_msg (as the real engine) */
static const char *const k_op[] = { "rec", "playstop", "sel", "mute", "clear", "undo", "reset", "quant", "bars" };

void inst_engine_loop(int op, int arg)
{
    lp_update();
    printf("SIM inst loop %s %d\n", op >= 0 && op < (int)(sizeof k_op / sizeof *k_op) ? k_op[op] : "?", arg);
    int64_t now = sim_now_us();
    switch (op) {
    case INST_LOOP_REC:
        if (L.state == PC_LP_EMPTY && E.ext) s_loop_msg = INST_LOOP_MSG_NOCLOCK;   /* the fake external clock never locks */
        else if (L.state == PC_LP_EMPTY) { L.state = PC_LP_COUNTIN; L.t0 = now; }
        else if (L.state == PC_LP_TAKE) { L.len_us = now - L.t0; L.t0 = now; L.state = PC_LP_PLAY; L.can_undo = false; }
        else if (L.state == PC_LP_PLAY) { L.rec = !L.rec; L.can_undo = L.rec; }
        else if (L.state == PC_LP_STOP) { L.state = PC_LP_PLAY; L.t0 = now; L.rec = true; }
        break;
    case INST_LOOP_PLAYSTOP:
        if (L.state == PC_LP_PLAY) { L.state = PC_LP_STOP; L.rec = false; }
        else if (L.state == PC_LP_STOP) { L.state = PC_LP_PLAY; L.t0 = now; }
        break;
    case INST_LOOP_SEL: L.sel = arg & 3; break;
    case INST_LOOP_MUTE: L.mute ^= (uint8_t)(1u << (arg & 3)); break;
    case INST_LOOP_CLEAR: L.cnt[arg & 3] = 0; L.can_undo = false; if (!(L.cnt[0] | L.cnt[1] | L.cnt[2] | L.cnt[3]) && L.state >= PC_LP_PLAY) { L.state = PC_LP_EMPTY; L.rec = false; } break;
    case INST_LOOP_UNDO: L.can_undo = false; break;
    case INST_LOOP_RESET: memset(&L, 0, sizeof L); L.bars = E.bars; break;
    case INST_LOOP_BARS: L.bars = E.bars = arg; break;
    default: break;
    }
}
void inst_engine_loop_state(inst_loop_t *s)
{
    lp_update();
    int64_t now = sim_now_us(), bar = beat_us() * E.bpb;
    memset(s, 0, sizeof *s);
    s->state = L.state; s->rec = L.rec; s->sel = L.sel; s->mute = L.mute; s->can_undo = L.can_undo;
    memcpy(s->cnt, L.cnt, sizeof s->cnt);
    s->events = L.events; s->cap = 4096;
    s->len = (uint32_t)(L.len_us * INST_RATE / 1000000);
    if (L.state == PC_LP_COUNTIN) s->countin = (int32_t)((bar - (now - L.t0) + beat_us() - 1) / beat_us());
    if (L.state == PC_LP_TAKE) s->pos = (uint32_t)((now - L.t0) * INST_RATE / 1000000);
    if (L.state == PC_LP_PLAY && L.len_us) s->pos = (uint32_t)(((now - L.t0) % L.len_us) * INST_RATE / 1000000);
    s->bar = (uint32_t)(s->pos / (uint32_t)(bar * INST_RATE / 1000000 ? bar * INST_RATE / 1000000 : 1));
}
int inst_engine_loop_msg(void) { int m = s_loop_msg; s_loop_msg = INST_LOOP_MSG_NONE; return m; }
int fake_inst_engine_loop_ready(void)
{
    lp_update();
    if ((L.state == PC_LP_STOP || L.state == PC_LP_PLAY) && !L.rec && L.len_us) return 0;
    return L.rec || L.state == PC_LP_TAKE ? INST_LF_BUSY : INST_LF_EMPTY;
}
bool fake_inst_engine_loop_content(void) { return L.cnt[0] | L.cnt[1] | L.cnt[2] | L.cnt[3]; }
void fake_inst_engine_loop_loaded(void)
{
    int bars = L.bars ? L.bars : 2;
    memset(&L, 0, sizeof L);
    L.bars = E.bars;
    L.state = PC_LP_STOP;
    L.len_us = beat_us() * E.bpb * bars;
    L.cnt[0] = 1;
    L.events = 1;
}
void inst_engine_loop_timing(inst_loop_timing_t *t) { memset(t, 0, sizeof *t); }

/* ---- notes ---- */
void fake_inst_engine_note(int note, int vel)
{
    if (vel > 0) {
        if (s_qn < 16) s_q[s_qn++] = (inst_note_t){ 0, (uint8_t)note, (uint8_t)vel, 0 };
        s_last = note;
        if (note >= 0 && note < 128 && !s_held[note] && s_nheld < 16) { s_held[note] = 1; s_held_note[s_nheld++] = note; }
        lp_update();
        if (L.state == PC_LP_TAKE || (L.state == PC_LP_PLAY && L.rec)) { L.cnt[L.sel]++; L.events++; }
    } else if (note >= 0 && note < 128 && s_held[note]) {
        s_held[note] = 0;
        for (int i = 0; i < s_nheld; i++) if (s_held_note[i] == note) { memmove(&s_held_note[i], &s_held_note[i + 1], (size_t)(s_nheld - i - 1) * sizeof(int)); s_nheld--; break; }
    }
    printf("SIM inst note %s %d\n", vel > 0 ? "on" : "off", note);
}
int fake_inst_engine_held(int voice)
{
    int n = inst_chip(E.chip) ? inst_chip(E.chip)->voice_count() : 0;
    for (int i = 0; i < s_nheld; i++) if (n && s_held_note[i] % n == voice) return s_held_note[i];
    return -1;
}
void inst_engine_post_ui(const pc_midi_ev_t *e)
{
    if (e->type == PC_MIDI_NOTE_ON) fake_inst_engine_note(e->a, e->b ? e->b : 100);
    else if (e->type == PC_MIDI_NOTE_OFF) fake_inst_engine_note(e->a, 0);
}
bool inst_engine_note_pop(inst_note_t *n)
{
    if (!s_qn) return false;
    *n = s_q[0];
    memmove(&s_q[0], &s_q[1], (size_t)(--s_qn) * sizeof s_q[0]);
    return true;
}
int inst_engine_last_note(void) { return s_last; }
void inst_engine_all_off(void) { memset(s_held, 0, sizeof s_held); s_nheld = 0; }
void inst_engine_stop(void) { inst_engine_all_off(); }
void inst_engine_tap(bool on) { (void)on; }
int inst_engine_tap_read(float *mono, int n)       /* silence unless a note is held, then a sine of that note */
{
    int note = s_nheld ? s_held_note[s_nheld - 1] : -1;
    double f = note >= 0 ? 440.0 * pow(2.0, (note - 69) / 12.0) : 0;
    for (int i = 0; i < n; i++) mono[i] = note >= 0 ? (float)(0.5 * sin(2 * M_PI * f * i / INST_RATE)) : 0.0f;
    return n;
}

/* ---- settings ---- */
int inst_engine_start(const pc_instr_cfg_t *c, int chip)
{
    E.chip = chip; E.vol = c->volume; E.map = c->map; E.ch = c->channel; E.stable = c->stable != 0; E.layer = c->layer;
    E.bpm = c->bpm; E.bpb = c->bpb; E.click = c->click != 0; E.ext = c->clock_ext != 0; E.bars = L.bars = c->loop_bars;
    return 0;
}
void inst_engine_set_chip(int idx) { E.chip = idx; inst_engine_all_off(); printf("SIM inst chip %d\n", idx); }
int inst_engine_chip(void) { return E.chip; }
const pc_synth_t *inst_engine_synth(void) { return inst_chip(E.chip); }
void inst_engine_set_volume(int v) { E.vol = v; printf("SIM inst vol %d\n", v); }
void inst_engine_set_map(pc_map_t m) { E.map = m; }
void inst_engine_set_channel(int ch) { E.ch = ch; }
void inst_engine_set_stable(bool on) { E.stable = on; }
void inst_engine_set_layer(pc_layer_t l) { E.layer = (int)l; }
void inst_engine_set_click(bool on) { E.click = on; }
void inst_engine_set_clock_ext(bool ext) { E.ext = ext; }
int inst_engine_kit_voices(void) { return 0; }
void inst_engine_set_kit_voices(int n) { (void)n; }
void inst_engine_peak_reset(void) {}
void inst_engine_worst_reset(void) {}
void inst_engine_stats(inst_stats_t *s) { memset(s, 0, sizeof *s); s->chip_ok = true; }
void inst_engine_log_poll(void) {}

/* ---- metronome ---- */
void inst_engine_metro_run(int on)
{
    bool want = on == 2 ? !E.run : on != 0;
    if (want == E.run) return;
    if (!want) s_beats_done += beats_now();
    E.run = want;
    if (want) s_run_t0 = sim_now_us();
    print_metro();
}
void inst_engine_set_bpm(int bpm) { E.bpm = bpm; print_metro(); }
void inst_engine_set_bpb(int bpb) { E.bpb = bpb; print_metro(); }
void inst_engine_metro(inst_metro_t *m)
{
    memset(m, 0, sizeof *m);
    m->running = E.run; m->ext = E.ext; m->bpm = (float)E.bpm; m->bpb = E.bpb; m->beats = s_beats_done + beats_now();
    m->loop = L.state != PC_LP_EMPTY;          /* the real engine: a loop fixes the tempo and the bar (its own tempo = the one it was taken at here) */
}
uint32_t fake_inst_engine_beat(uint8_t *beat, uint8_t *accent)
{
    uint32_t n = beats_now();
    *beat = n ? (uint8_t)((n - 1) % (uint32_t)E.bpb) : 0;
    *accent = *beat == 0;
    return s_beats_done + n;
}
