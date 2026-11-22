// SPDX-License-Identifier: LGPL-2.1-or-later
/* YM2612 FM (stage 5): GME's GENS core (Ym2612_Emu) run straight at the output rate, no Blip buffer.
 * 5 melodic voices (FM channels 1..5) through the sticky pool + channel 6 for channel 10 (FM drums).
 * Patches: pc_fm_bank (programs 0..15) then the card's .tfi files in /sdcard/instrument/fm, in name order (16..).
 * The core's state and its 160 KB tables are malloc'd: PSRAM (above SPIRAM_MALLOC_ALWAYSINTERNAL). */
#include "inst_chip.h"
#include "pc/pc_paths.h"
#include "inst_fm.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "Ym2612_GENS.h"          /* the GENS core directly: gme picks it with a private define */
#include "esp_heap_caps.h"
#include "pc/pc_fmpatch.h"

#define FM_CLOCK 7670453.0
#define FM_MELODIC 5
#define FM_DRUM 5                                 /* the FM channel channel 10 plays on */
#define FM_VOICES 6
#define PERC 9
#define USER_MAX 32
#define USER_DIR PC_FM_DIR

static Ym2612_GENS_Emu s_ym;
static bool s_ready;
static pc_vp_t s_pool;
static volatile int s_prog[16];                  /* Auto / Direct: program per MIDI channel */
static int s_gm[16];                              /* GM: program per channel */
static int s_loaded[FM_VOICES];                   /* patch in each FM channel: index, 1000 + drum, -1 */
static float s_hz[128], s_bend = 1.0f;
static pc_fm_patch_t *s_user;                     /* PSRAM, USER_MAX */
static int s_nuser;

static void w(int ch, int reg, int val)
{
    int part = ch / 3, a = reg >= 0x30 ? reg + ch % 3 : reg;
    if (part) s_ym.write1(a, val); else s_ym.write0(a, val);
}
static void key(int ch, bool on) { s_ym.write0(0x28, (on ? 0xF0 : 0) | (ch / 3 ? 4 : 0) | ch % 3); }

static int total(void) { return PC_FM_BANK_N + s_nuser; }
static int prog_index(int prog) { int t = total(); return t ? ((prog % t) + t) % t : 0; }
static const pc_fm_patch_t *patch_at(int i) { return i < PC_FM_BANK_N ? &pc_fm_bank[i] : &s_user[i - PC_FM_BANK_N]; }

static void load(int ch, int id, const pc_fm_patch_t *p, int vel)
{
    pc_fm_reg_t r[PC_FM_REGS];
    int n;
    if (s_loaded[ch] == id) n = pc_fm_tl_regs(p, vel, r);   /* same patch: only the carriers' level */
    else { n = pc_fm_patch_regs(p, vel, r); s_loaded[ch] = id; }
    for (int i = 0; i < n; i++) w(ch, r[i].reg, r[i].val);
}

static void freq(int ch, int note)
{
    int b, f;
    pc_fm_fnum(s_hz[note & 127] * s_bend, &b, &f);
    w(ch, 0xA4, b << 3 | f >> 8);
    w(ch, 0xA0, f & 0xFF);
}

/* pool callbacks: slot = FM channel 0..4; timbre = patch index */
static void trigger(int slot, int note, int vel, int timbre)
{
    key(slot, false);                             /* a stolen voice restarts its envelope */
    load(slot, timbre, patch_at(timbre), vel);
    freq(slot, note);
    key(slot, true);
}
static void silence(int slot) { key(slot, false); }

static void drum_on(int note, int vel)
{
    int play;
    const pc_fm_patch_t *p = pc_fm_drum(note, &play);
    key(FM_DRUM, false);
    load(FM_DRUM, 1000 + p->name[0], p, vel);    /* Kick / Snare / Hat */
    freq(FM_DRUM, play);
    key(FM_DRUM, true);
}

static void fm_note_on(int ch, int note, int vel)
{
    if (ch == PERC) { drum_on(note, vel); return; }
    pc_vp_note_on(&s_pool, ch, note, vel, prog_index(s_prog[ch & 15]));
}
static void fm_note_off(int ch, int note) { if (ch != PERC) pc_vp_note_off(&s_pool, ch, note); }
static void fm_program(int ch, int prog) { if (ch >= 0 && ch < 16) s_prog[ch] = prog & 127; }
static void fm_note_on_gm(int ch, int note, int vel)
{
    if (ch == PERC) { drum_on(note, vel); return; }
    pc_vp_note_on(&s_pool, ch, note, vel, pc_fm_gm_patch(s_gm[ch & 15]));
}
static void fm_program_gm(int ch, int prog) { if (ch >= 0 && ch < 16) s_gm[ch] = prog & 127; }
static void fm_on_direct(int v, int note, int vel) { if (v < FM_MELODIC) pc_vp_note_on_direct(&s_pool, v, note, vel, prog_index(s_prog[v])); }
static void fm_off_direct(int v, int note) { if (v < FM_MELODIC) pc_vp_note_off_direct(&s_pool, v, note); }

static void fm_all_off(void)
{
    pc_vp_all_off(&s_pool);
    for (int c = 0; c < FM_VOICES; c++) key(c, false);
}

static void bend(int ch, int cents)
{
    (void)ch;                                     /* the whole chip, like every adapter here */
    s_bend = exp2f(cents / 1200.0f);
    for (int i = 0; i < FM_MELODIC; i++) {
        int n = pc_vp_voice_note(&s_pool, i);
        if (n >= 0) freq(i, n);
    }
}

static int name_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

/* the card's .tfi patches, in name order (ones that do not parse are skipped) */
static void scan_user(void)
{
    s_nuser = 0;
    if (!s_user) s_user = (pc_fm_patch_t *)heap_caps_calloc(USER_MAX, sizeof *s_user, MALLOC_CAP_SPIRAM);
    if (!s_user) return;
    DIR *d = opendir(USER_DIR);
    if (!d) return;
    static char names[USER_MAX][64];              /* PSRAM (.bss) */
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < USER_MAX) {
        size_t l = strlen(e->d_name);
        if (l > 4 && l < sizeof names[0] && !strcasecmp(e->d_name + l - 4, ".tfi")) snprintf(names[n++], sizeof names[0], "%s", e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof names[0], name_cmp);
    for (int i = 0; i < n; i++) {
        char path[96];
        snprintf(path, sizeof path, USER_DIR "/%s", names[i]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        uint8_t b[PC_FM_TFI_SIZE + 1];
        size_t got = fread(b, 1, sizeof b, f);
        fclose(f);
        pc_fm_patch_t *p = &s_user[s_nuser];
        if (pc_fm_tfi_parse(b, got, p) != 0) { printf("INST fm skip %s (%u bytes)\n", names[i], (unsigned)got); continue; }
        names[i][strlen(names[i]) - 4] = 0;
        snprintf(p->name, sizeof p->name, "%s", names[i]);
        s_nuser++;
    }
}

static int fm_reset(uint32_t rate)
{
    if (!s_ready) {
        if (s_ym.set_rate((double)rate, FM_CLOCK)) return -1;   /* allocates and builds the tables */
        s_ready = true;
        inst_fm_fast(1);                          /* the sine table in internal RAM: -26 % CPU for 8 KB (spec 16) */
    }
    s_ym.reset();
    s_ym.write0(0x22, 0);                         /* LFO off */
    s_ym.write0(0x27, 0);                         /* channel 3 normal mode, timers off */
    s_ym.write0(0x2B, 0);                         /* DAC off: channel 6 is FM */
    for (int n = 0; n < 128; n++) s_hz[n] = 440.0f * exp2f((n - 69) / 12.0f);
    s_bend = 1.0f;
    for (int c = 0; c < FM_VOICES; c++) { s_loaded[c] = -1; key(c, false); }
    memset(s_gm, 0, sizeof s_gm);
    pc_vp_init_n(&s_pool, FM_MELODIC, 0, trigger, silence);
    scan_user();
    printf("INST fm patches=%d (card %d)\n", total(), s_nuser);
    return 0;
}

static void fm_render(int16_t *st, size_t frames)
{
    memset(st, 0, frames * 4);
    s_ym.run((int)frames, st);
}

static int fm_voice_count(void) { return FM_VOICES; }
static const char *fm_voice_name(int i)
{
    static const char *const n[FM_VOICES] = { "FM 1", "FM 2", "FM 3", "FM 4", "FM 5", "FM Drum" };
    return i >= 0 && i < FM_VOICES ? n[i] : NULL;
}
static void fm_voice_state(int i, uint8_t *level, int *active)
{
    int v = s_ready ? s_ym.level(i) : 0;
    *level = (uint8_t)v;
    *active = v > 0;
}
static int fm_voice_note(int i) { return pc_vp_voice_note(&s_pool, i); }
static int fm_harmony(void) { return FM_MELODIC; }

static const pc_synth_t k_fm = {
    .id = "fm",
    .name = "YM2612 FM",
    .reset = fm_reset,
    .note_on = fm_note_on,
    .note_off = fm_note_off,
    .program_change = fm_program,
    .all_off = fm_all_off,
    .note_on_direct = fm_on_direct,
    .note_off_direct = fm_off_direct,
    .note_on_gm = fm_note_on_gm,
    .program_change_gm = fm_program_gm,
    .pitch_bend = bend,
    .render = fm_render,
    .voice_count = fm_voice_count,
    .voice_name = fm_voice_name,
    .voice_state = fm_voice_state,
    .voice_note = fm_voice_note,
    .harmony_count = fm_harmony,
};

extern "C" const pc_synth_t *midi_chip_ym2612(void) { return &k_fm; }

extern "C" int inst_fm_patch_count(void) { return total(); }
extern "C" const char *inst_fm_patch_name(int prog) { return patch_at(prog_index(prog))->name; }
extern "C" int inst_fm_program(int ch) { return s_prog[ch & 15]; }
extern "C" void inst_fm_set_program_all(int prog)
{
    for (int c = 0; c < 16; c++) s_prog[c] = prog & 127;   /* int stores: the audio task picks them up per note */
}

/* Copies of the core's tables in internal RAM. 1 (default): the sine table, 8 KB, kept once made (freeing it
 * under a render would need a handshake). 2: + the envelope table, 16 KB more (measured: no gain; A/B only).
 * 0: the core's PSRAM tables. Measured with all 6 voices: 30.6 % -> 22.6 % CPU (sine), 30.2 % (envelope). */
extern "C" int inst_fm_fast(int on)
{
    static void *sin_mem, *env_mem;
    if (!on || !s_ready) { s_ym.fast_tables(NULL, NULL); return 0; }
    if (!sin_mem) sin_mem = heap_caps_malloc(Ym2612_GENS_Emu::sin_table_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (on == 2 && !env_mem) env_mem = heap_caps_malloc(Ym2612_GENS_Emu::env_table_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!sin_mem) return -1;                      /* no room: the PSRAM tables, slower */
    s_ym.fast_tables(sin_mem, on == 2 ? env_mem : NULL);
    return on == 2 && env_mem ? 2 : 1;
}
