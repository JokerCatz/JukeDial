// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_voicepool.h"
#include <string.h>

void pc_vp_init_n(pc_vp_t *p, int harmony, int bass, pc_vp_trigger_fn t, pc_vp_silence_fn s)
{
    memset(p, 0, sizeof *p);
    p->harmony = harmony;
    p->bass = bass;
    p->trigger = t;
    p->silence = s;
}

void pc_vp_init(pc_vp_t *p, pc_vp_trigger_fn t, pc_vp_silence_fn s) { pc_vp_init_n(p, 2, 1, t, s); }


static int total(const pc_vp_t *p) { return p->harmony + p->bass; }

static int pick_harmony(const pc_vp_t *p)
{
    int oldest = -1;
    for (int i = 0; i < p->harmony; i++) {
        if (!p->slot[i].active) return i;
        if (oldest < 0 || p->slot[i].order < p->slot[oldest].order) oldest = i;
    }
    return oldest;
}

static bool lowest_of_harmony(const pc_vp_t *p, int note)
{
    for (int i = 0; i < p->harmony; i++)
        if (p->slot[i].active && note >= p->slot[i].note) return false;
    return true;
}

static int free_bass(const pc_vp_t *p)
{
    for (int b = 0; b < p->bass; b++)
        if (!p->slot[p->harmony + b].active) return p->harmony + b;
    return -1;
}

static void assign(pc_vp_t *p, int slot, int ch, int note, int vel, int timbre)
{
    if (slot < 0) return;
    pc_vp_slot_t *s = &p->slot[slot];
    s->active = true;
    s->note = note;
    s->channel = ch;
    s->velocity = vel;
    s->timbre = timbre;
    s->order = p->next_order++;
    p->trigger(slot, note, vel, timbre);
}

void pc_vp_note_on(pc_vp_t *p, int ch, int note, int vel, int timbre)
{
    int slot = lowest_of_harmony(p, note) ? free_bass(p) : -1;
    assign(p, slot >= 0 ? slot : pick_harmony(p), ch, note, vel, timbre);
}

void pc_vp_note_on_gm(pc_vp_t *p, int ch, int note, int vel, pc_gm_group_t g, int harmony_timbre, int bass_timbre)
{
    int slot = -1;
    if (g == PC_GM_BASS || (g == PC_GM_EITHER && lowest_of_harmony(p, note))) slot = free_bass(p);
    if (slot >= 0) assign(p, slot, ch, note, vel, bass_timbre);
    else assign(p, pick_harmony(p), ch, note, vel, harmony_timbre);
}

void pc_vp_note_off(pc_vp_t *p, int ch, int note)
{
    for (int i = 0; i < total(p); i++)
        if (p->slot[i].active && p->slot[i].note == note && p->slot[i].channel == ch) {
            p->silence(i);
            p->slot[i].active = false;
            return;
        }
}

void pc_vp_note_on_direct(pc_vp_t *p, int voice, int note, int vel, int timbre)
{
    if (voice < 0 || voice >= total(p)) return;
    assign(p, voice, voice, note, vel, timbre);
}

void pc_vp_note_off_direct(pc_vp_t *p, int voice, int note)
{
    if (voice < 0 || voice >= total(p)) return;
    if (p->slot[voice].active && p->slot[voice].note == note) {
        p->silence(voice);
        p->slot[voice].active = false;
    }
}

void pc_vp_all_off(pc_vp_t *p)
{
    for (int i = 0; i < total(p); i++)
        if (p->slot[i].active) {
            p->silence(i);
            p->slot[i].active = false;
        }
}

int pc_vp_harmony_count(const pc_vp_t *p) { return p->harmony; }

static const pc_vp_slot_t *live(const pc_vp_t *p, int slot)
{
    return slot >= 0 && slot < total(p) && p->slot[slot].active ? &p->slot[slot] : NULL;
}
int pc_vp_voice_note(const pc_vp_t *p, int slot) { const pc_vp_slot_t *s = live(p, slot); return s ? s->note : -1; }
int pc_vp_voice_timbre(const pc_vp_t *p, int slot) { const pc_vp_slot_t *s = live(p, slot); return s ? s->timbre : -1; }
int pc_vp_voice_channel(const pc_vp_t *p, int slot) { const pc_vp_slot_t *s = live(p, slot); return s ? s->channel : -1; }
