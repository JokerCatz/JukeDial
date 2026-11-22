// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_instr.h"
#include "pc/pc_ledfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PERC 9

void pc_instr_init(pc_instr_t *r, const pc_synth_t *chip, pc_map_t mode, int channel)
{
    memset(r, 0, sizeof *r);
    r->chip = chip;
    r->mode = mode;
    r->channel = channel;
    r->bend_range = 2;
    r->volume = 100;
    r->last_note = -1;
}

static void clear_state(pc_instr_t *r)
{
    memset(r->held, 0, sizeof r->held);
    memset(r->pedal, 0, sizeof r->pedal);           /* a pedal-up lost with the link never comes */
    for (int ch = 0; ch < 16; ch++) {
        if (r->bend[ch] && r->chip && r->chip->pitch_bend) r->chip->pitch_bend(ch, 0);
        r->bend[ch] = 0;
    }
}

void pc_instr_all_off(pc_instr_t *r)
{
    if (r->chip) r->chip->all_off();
    clear_state(r);
}

void pc_instr_set_chip(pc_instr_t *r, const pc_synth_t *chip)
{
    pc_instr_all_off(r);
    r->chip = chip;
}

void pc_instr_set_mode(pc_instr_t *r, pc_map_t mode)
{
    if (mode == r->mode) return;
    pc_instr_all_off(r);
    r->mode = mode;
}

void pc_instr_set_channel(pc_instr_t *r, int channel)
{
    if (channel == r->channel) return;
    pc_instr_all_off(r);
    r->channel = channel;
}

static bool held(const pc_instr_t *r, int ch, int n) { return r->held[ch][n >> 3] >> (n & 7) & 1; }
static void set_held(pc_instr_t *r, int ch, int n, bool on)
{
    if (on) r->held[ch][n >> 3] |= (uint8_t)(1 << (n & 7));
    else r->held[ch][n >> 3] &= (uint8_t)~(1 << (n & 7));
}

static int direct_voice(const pc_instr_t *r, int ch)
{
    int melodic = r->chip->voice_count() - 1;           /* the last voice is percussion */
    return ch >= 0 && ch < melodic ? ch : -1;
}

/* the actual chip calls, after the pedal */
static void chip_off(pc_instr_t *r, int ch, int note)
{
    const pc_synth_t *c = r->chip;
    if (ch != PERC && r->mode == PC_MAP_DIRECT) {
        int v = direct_voice(r, ch);
        if (v >= 0) c->note_off_direct(v, note);
    } else c->note_off(ch, note);
}

static int voice_of(const pc_instr_t *r, int ch, int note)
{
    int n = r->chip->voice_count();
    if (ch == PERC) return n - 1;
    if (r->mode == PC_MAP_DIRECT) return direct_voice(r, ch);
    for (int i = 0; i < n - 1; i++)
        if (r->chip->voice_note && r->chip->voice_note(i) == note) return i;
    return -1;
}

static int chip_on(pc_instr_t *r, int ch, int note, int vel)
{
    const pc_synth_t *c = r->chip;
    if (ch == PERC) c->note_on(ch, note, vel);
    else if (r->mode == PC_MAP_DIRECT) {
        int v = direct_voice(r, ch);
        if (v < 0) return -1;
        c->note_on_direct(v, note, vel);
    } else if (r->mode == PC_MAP_GM) c->note_on_gm(ch, note, vel);
    else c->note_on(ch, note, vel);
    r->last_note = note;
    return voice_of(r, ch, note);
}

static void pedal_up(pc_instr_t *r, int ch)
{
    r->pedal[ch] = false;
    for (int n = 0; n < 128; n++)
        if (held(r, ch, n)) { set_held(r, ch, n, false); chip_off(r, ch, n); }
}

int pc_instr_event(pc_instr_t *r, const pc_midi_ev_t *e)
{
    if (!r->chip || e->type >= PC_MIDI_CLOCK || e->type == PC_MIDI_NONE) return -1;
    int ch = e->ch & 15;
    if (e->a > 127 || e->b > 127) return -1;          /* not MIDI data bytes (a bad file): would index past tables */
    if (r->mode != PC_MAP_DIRECT && r->channel >= 0 && ch != r->channel) return -1;
    switch (e->type) {
    case PC_MIDI_NOTE_ON:
        if (held(r, ch, e->a)) { set_held(r, ch, e->a, false); chip_off(r, ch, e->a); }
        return chip_on(r, ch, e->a, e->b);
    case PC_MIDI_NOTE_OFF:
        if (r->pedal[ch] && ch != PERC) set_held(r, ch, e->a, true);
        else chip_off(r, ch, e->a);
        return -1;
    case PC_MIDI_PROGRAM:
        if (r->mode == PC_MAP_AUTO) r->chip->program_change(ch, e->a);
        else if (r->mode == PC_MAP_GM) r->chip->program_change_gm(ch, e->a);
        return -1;
    case PC_MIDI_BEND: {
        int cents = e->bend * r->bend_range * 100 / 8192;
        r->bend[ch] = (int16_t)cents;
        if (r->chip->pitch_bend) r->chip->pitch_bend(ch, cents);
        return -1;
    }
    case PC_MIDI_CC:
        switch (e->a) {
        case 7: r->volume = e->b; break;
        case 64: if (e->b >= 64) r->pedal[ch] = true; else pedal_up(r, ch); break;
        case 120: case 123: pc_instr_all_off(r); break;
        case 121:
            if (r->pedal[ch]) pedal_up(r, ch);
            if (r->bend[ch]) { r->bend[ch] = 0; if (r->chip->pitch_bend) r->chip->pitch_bend(ch, 0); }
            break;
        default: break;
        }
        return -1;
    default: return -1;
    }
}

static const char *const MAP[PC_MAP_COUNT] = { "auto", "direct", "gm" };
const char *pc_instr_map_name(pc_map_t m) { return m >= 0 && m < PC_MAP_COUNT ? MAP[m] : "auto"; }

void pc_instr_cfg_default(pc_instr_cfg_t *c)
{
    memset(c, 0, sizeof *c);
    strcpy(c->chip, "nes");
    c->map = PC_MAP_AUTO;
    c->channel = -1;
    c->volume = 40;                              /* PC_VOL_DEFAULT; the app starts from the player volume */
    strcpy(c->kit, "demo");
    c->bpm = 120;
    c->bpb = 4;
    c->click = 1;
    c->beat_haptic = 1;
    c->loop_bars = 2;
}

static const char *const LAYER[PC_LAYER_COUNT] = { "auto", "chip", "kit", "both" };
const char *pc_layer_name(pc_layer_t l) { return l >= 0 && l < PC_LAYER_COUNT ? LAYER[l] : "auto"; }

int pc_layer_route(pc_layer_t layer, int channel, int ch, bool kit)
{
    bool pass = channel < 0 || ch == channel;
    switch (layer) {
    case PC_LAYER_CHIP: return pass ? PC_ROUTE_CHIP : 0;
    case PC_LAYER_KIT: return pass && kit ? PC_ROUTE_KIT : 0;
    case PC_LAYER_BOTH: return pass ? PC_ROUTE_CHIP | (kit ? PC_ROUTE_KIT : 0) : 0;
    default:
        if (ch == 9 && kit) return PC_ROUTE_KIT;
        return pass ? PC_ROUTE_CHIP : 0;
    }
}

#define NR_OPEN 0x80
void pc_noteroute_reset(pc_noteroute_t *nr) { memset(nr, 0, sizeof *nr); }

int pc_noteroute(pc_noteroute_t *nr, const pc_midi_ev_t *e, int now)
{
    bool on = e->type == PC_MIDI_NOTE_ON && e->b, off = e->type == PC_MIDI_NOTE_OFF || (e->type == PC_MIDI_NOTE_ON && !e->b);
    if (!on && !off) return now;
    if (e->a > 127) return 0;
    uint8_t *x = &nr->r[e->ch & 15][e->a];
    if (on) { *x = (uint8_t)(NR_OPEN | now); return now; }
    int r = *x & NR_OPEN ? *x & ~NR_OPEN : now;
    *x = 0;
    return r;
}

void pc_instr_cfg_parse(const char *s, pc_instr_cfg_t *c)
{
    pc_instr_cfg_default(c);
    if (!s) return;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", s);
    for (char *save = NULL, *t = strtok_r(buf, " ,", &save); t; t = strtok_r(NULL, " ,", &save)) {
        char *v = strchr(t, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(t, "chip") && *v && strlen(v) < sizeof c->chip) strcpy(c->chip, v);
        else if (!strcmp(t, "map")) { for (int i = 0; i < PC_MAP_COUNT; i++) if (!strcmp(v, MAP[i])) c->map = (pc_map_t)i; }
        else if (!strcmp(t, "ch")) { int n = atoi(v); c->channel = n >= 1 && n <= 16 ? n - 1 : -1; }
        else if (!strcmp(t, "vol")) { int n = atoi(v); c->volume = n < 0 ? 0 : n > 100 ? 100 : n; }
        else if (!strcmp(t, "timing")) c->stable = !strcmp(v, "stable");
        else if (!strcmp(t, "knob")) c->knob_volume = !strcmp(v, "volume");
        else if (!strcmp(t, "kit") && strlen(v) < sizeof c->kit) {
            strcpy(c->kit, v);
            for (char *p = c->kit; *p; p++) if (*p == '|') *p = ' ';
            if (!strcmp(c->kit, "-")) c->kit[0] = 0;     /* none */
        }
        else if (!strcmp(t, "layer")) { for (int i = 0; i < PC_LAYER_COUNT; i++) if (!strcmp(v, LAYER[i])) c->layer = (pc_layer_t)i; }
        else if (!strcmp(t, "clock")) c->clock_ext = !strcmp(v, "ext");
        else if (!strcmp(t, "bpm")) { int n = atoi(v); if (n >= 30 && n <= 300) c->bpm = n; }
        else if (!strcmp(t, "bpb")) { int n = atoi(v); if (n >= 2 && n <= 7) c->bpb = n; }
        else if (!strcmp(t, "click")) c->click = strcmp(v, "off") != 0;
        else if (!strcmp(t, "beathap")) c->beat_haptic = strcmp(v, "off") != 0;
        else if (!strcmp(t, "fm")) { int n = atoi(v); if (n >= 0 && n <= 127) c->fm = n; }
        else if (!strcmp(t, "lbars")) { int n = atoi(v); if (n == 0 || n == 1 || n == 2 || n == 4 || n == 8) c->loop_bars = n; }
        else if (!strcmp(t, "lq")) { int n = atoi(v); if (n >= 0 && n <= 2) c->loop_q = n; }
        else if (!strcmp(t, "fx")) c->fx = (int)pc_fx_parse(v);
    }
}

void pc_instr_cfg_format(const pc_instr_cfg_t *c, char *out, int cap)
{
    char kit[sizeof c->kit];
    snprintf(kit, sizeof kit, "%s", c->kit[0] ? c->kit : "-");
    for (char *p = kit; *p; p++) if (*p == ' ' || *p == ',') *p = '|';
    snprintf(out, (size_t)cap, "chip=%s map=%s ch=%d vol=%d timing=%s knob=%s kit=%s layer=%s clock=%s bpm=%d bpb=%d click=%s beathap=%s fm=%d lbars=%d lq=%d fx=%s",
             c->chip, pc_instr_map_name(c->map), c->channel + 1, c->volume, c->stable ? "stable" : "now",
             c->knob_volume ? "volume" : "page", kit, pc_layer_name(c->layer), c->clock_ext ? "ext" : "int", c->bpm, c->bpb,
             c->click ? "on" : "off", c->beat_haptic ? "on" : "off", c->fm, c->loop_bars, c->loop_q, pc_fx_name((pc_fx_mode_t)c->fx));
}
