// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_fmpatch.h"
#include <string.h>

/* Built-in bank: hand-made OPN2 patches in the spirit of Mega Drive sound sets (not copies of any game's).
 * Per operator: MUL, DT (3 = 0), TL, RS, AR, DR, SR, RR, SL, SSG; operators OP1..OP4.
 * Carrier TLs leave headroom: five voices at full velocity must not clip the mix. */
#define OP(mul, dt, tl, rs, ar, dr, sr, rr, sl) { mul, dt, tl, rs, ar, dr, sr, rr, sl, 0 }
#define OFF OP(1, 3, 127, 0, 31, 0, 0, 15, 15)
const pc_fm_patch_t pc_fm_bank[PC_FM_BANK_N] = {
    { "E.Piano", 4, 3, { OP(1, 3, 30, 2, 31, 8, 0, 6, 3),  OP(1, 4, 10, 2, 31, 4, 2, 6, 2),
                         OP(14, 3, 48, 1, 31, 14, 0, 8, 15), OP(1, 2, 14, 2, 31, 5, 2, 6, 2) } },
    { "Brass", 4, 5, { OP(1, 3, 22, 1, 20, 6, 0, 7, 2), OP(1, 3, 12, 1, 22, 3, 1, 7, 1),
                       OP(1, 5, 26, 1, 18, 6, 0, 7, 2), OP(1, 1, 14, 1, 22, 3, 1, 7, 1) } },
    { "Strings", 4, 4, { OP(1, 3, 32, 0, 14, 2, 0, 6, 1), OP(1, 4, 16, 0, 13, 2, 0, 6, 1),
                         OP(2, 3, 40, 0, 14, 2, 0, 6, 1), OP(1, 2, 16, 0, 13, 2, 0, 6, 1) } },
    { "Bass", 0, 5, { OP(0, 3, 40, 1, 31, 10, 4, 8, 5), OP(1, 3, 42, 1, 31, 12, 4, 8, 6),
                      OP(1, 4, 24, 1, 31, 9, 3, 8, 4),  OP(1, 3, 8, 1, 31, 4, 2, 9, 2) } },
    { "Organ", 7, 0, { OP(1, 3, 22, 0, 31, 0, 0, 10, 0), OP(2, 3, 24, 0, 31, 0, 0, 10, 0),
                       OP(4, 3, 30, 0, 31, 0, 0, 10, 0), OP(1, 4, 24, 0, 31, 0, 0, 10, 0) } },
    { "Bell", 4, 0, { OP(7, 3, 34, 1, 31, 6, 3, 5, 4),  OP(2, 3, 12, 1, 31, 4, 2, 4, 4),
                      OP(11, 6, 40, 1, 31, 6, 4, 5, 5), OP(4, 3, 18, 1, 31, 3, 2, 4, 3) } },
    { "Lead", 4, 7, { OP(1, 3, 18, 0, 31, 0, 0, 8, 0), OP(1, 3, 12, 0, 31, 0, 0, 8, 0),
                      OP(2, 3, 30, 0, 31, 0, 0, 8, 0), OP(1, 5, 18, 0, 31, 0, 0, 8, 0) } },
    { "Marimba", 4, 0, { OP(4, 3, 34, 1, 31, 18, 0, 9, 15), OP(1, 3, 8, 1, 31, 10, 6, 8, 6),
                         OP(10, 3, 50, 1, 31, 20, 0, 9, 15), OP(1, 4, 16, 1, 31, 9, 6, 8, 6) } },
    { "Flute", 4, 0, { OP(1, 3, 48, 0, 16, 0, 0, 6, 0), OP(1, 3, 12, 0, 18, 0, 0, 6, 0),
                       OFF,                            OP(2, 3, 32, 0, 18, 0, 0, 6, 0) } },
    { "Clav", 4, 6, { OP(3, 3, 24, 1, 31, 14, 6, 9, 5), OP(1, 3, 10, 1, 31, 8, 6, 9, 5),
                      OP(1, 3, 30, 1, 31, 16, 0, 9, 8), OP(2, 3, 16, 1, 31, 8, 6, 9, 5) } },
    { "Pad", 4, 3, { OP(1, 3, 36, 0, 8, 0, 0, 4, 0), OP(1, 4, 14, 0, 9, 0, 0, 4, 0),
                     OP(1, 3, 36, 0, 8, 0, 0, 4, 0), OP(1, 2, 14, 0, 9, 0, 0, 4, 0) } },
    { "Guitar", 4, 7, { OP(1, 3, 14, 1, 31, 2, 1, 8, 1), OP(1, 3, 12, 1, 31, 2, 3, 8, 2),
                        OP(3, 3, 26, 1, 31, 4, 1, 8, 2), OP(2, 5, 16, 1, 31, 2, 3, 8, 2) } },
    { "Harp", 4, 0, { OP(2, 3, 38, 1, 31, 12, 0, 8, 15), OP(1, 3, 10, 1, 31, 6, 4, 7, 5),
                      OP(4, 3, 50, 1, 31, 14, 0, 8, 15), OP(1, 4, 14, 1, 31, 6, 4, 7, 5) } },
    { "Slap Bass", 4, 6, { OP(1, 3, 20, 2, 31, 12, 4, 9, 6), OP(1, 3, 6, 2, 31, 6, 3, 9, 3),
                           OP(5, 3, 38, 2, 31, 18, 0, 9, 15), OP(1, 3, 12, 2, 31, 6, 3, 9, 3) } },
    { "Kalimba", 4, 0, { OP(5, 3, 40, 1, 31, 16, 0, 9, 15), OP(1, 3, 10, 1, 31, 9, 5, 8, 6),
                         OP(8, 3, 56, 1, 31, 18, 0, 9, 15), OP(2, 3, 22, 1, 31, 10, 6, 8, 6) } },
    { "Sine", 7, 0, { OFF, OFF, OFF, OP(1, 3, 8, 0, 31, 0, 0, 8, 0) } },
};

/* channel 10 */
static const pc_fm_patch_t k_drum[3] = {
    { "Kick", 4, 0, { OP(0, 3, 20, 3, 31, 20, 0, 15, 15), OP(1, 3, 4, 3, 31, 12, 10, 12, 3), OFF, OFF } },
    { "Snare", 4, 7, { OP(15, 3, 0, 3, 31, 16, 0, 14, 15), OP(1, 3, 8, 3, 31, 14, 12, 13, 5),
                       OFF,                               OP(3, 3, 16, 3, 31, 18, 0, 14, 8) } },
    { "Hat", 4, 7, { OP(15, 3, 0, 3, 31, 22, 0, 15, 15), OP(15, 3, 10, 3, 31, 22, 20, 15, 8), OFF, OFF } },
};

static uint8_t clamp(int v, int hi) { return (uint8_t)(v < 0 ? 0 : v > hi ? hi : v); }
static const uint8_t k_file_slot[4] = { 0, 2, 1, 3 };   /* logical op -> TFI slot (OP1, OP3, OP2, OP4) */

int pc_fm_tfi_parse(const uint8_t *b, size_t n, pc_fm_patch_t *p)
{
    if (n != PC_FM_TFI_SIZE || b[0] > 7) return -1;
    memset(p, 0, sizeof *p);
    p->alg = b[0];
    p->fb = clamp(b[1], 7);
    for (int i = 0; i < 4; i++) {
        const uint8_t *s = b + 2 + k_file_slot[i] * 10;
        pc_fm_op_t *o = &p->op[i];
        o->mul = clamp(s[0], 15); o->dt = clamp(s[1], 6); o->tl = clamp(s[2], 127); o->rs = clamp(s[3], 3);
        o->ar = clamp(s[4], 31); o->dr = clamp(s[5], 31); o->sr = clamp(s[6], 31); o->rr = clamp(s[7], 15);
        o->sl = clamp(s[8], 15); o->ssg = clamp(s[9], 15);
    }
    return 0;
}

void pc_fm_tfi_write(const pc_fm_patch_t *p, uint8_t out[PC_FM_TFI_SIZE])
{
    out[0] = p->alg;
    out[1] = p->fb;
    for (int i = 0; i < 4; i++) {
        const pc_fm_op_t *o = &p->op[i];
        uint8_t *s = out + 2 + k_file_slot[i] * 10;
        s[0] = o->mul; s[1] = o->dt; s[2] = o->tl; s[3] = o->rs; s[4] = o->ar;
        s[5] = o->dr; s[6] = o->sr; s[7] = o->rr; s[8] = o->sl; s[9] = o->ssg;
    }
}

unsigned pc_fm_carriers(int alg)
{
    static const uint8_t c[8] = { 0x8, 0x8, 0x8, 0x8, 0xA, 0xE, 0xE, 0xF };
    return c[alg & 7];
}

int pc_fm_op_offset(int op) { static const uint8_t o[4] = { 0, 8, 4, 12 }; return o[op & 3]; }

int pc_fm_vel_att(int vel)
{
    int d = 127 - clamp(vel, 127);
    return d * d / 600;                          /* 0 .. 26 (about 20 dB): a squared curve */
}

static uint8_t dt_reg(int dt)                    /* TFI 0..6 (3 = 0) -> 0..3 up, 4..7 down */
{
    int v = dt - 3;
    return (uint8_t)(v >= 0 ? v : 4 - v);
}

static int tl_of(const pc_fm_patch_t *p, int i, int vel)
{
    int tl = p->op[i].tl;
    if (pc_fm_carriers(p->alg) & (1u << i)) tl += pc_fm_vel_att(vel);
    return tl > 127 ? 127 : tl;
}

int pc_fm_patch_regs(const pc_fm_patch_t *p, int vel, pc_fm_reg_t out[PC_FM_REGS])
{
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const pc_fm_op_t *o = &p->op[i];
        int f = pc_fm_op_offset(i);
        out[n++] = (pc_fm_reg_t){ (uint8_t)(0x30 + f), (uint8_t)(dt_reg(o->dt) << 4 | (o->mul & 15)) };
        out[n++] = (pc_fm_reg_t){ (uint8_t)(0x40 + f), (uint8_t)tl_of(p, i, vel) };
        out[n++] = (pc_fm_reg_t){ (uint8_t)(0x50 + f), (uint8_t)((o->rs & 3) << 6 | (o->ar & 31)) };
        out[n++] = (pc_fm_reg_t){ (uint8_t)(0x60 + f), (uint8_t)(o->dr & 31) };
        out[n++] = (pc_fm_reg_t){ (uint8_t)(0x70 + f), (uint8_t)(o->sr & 31) };
        out[n++] = (pc_fm_reg_t){ (uint8_t)(0x80 + f), (uint8_t)((o->sl & 15) << 4 | (o->rr & 15)) };
        out[n++] = (pc_fm_reg_t){ (uint8_t)(0x90 + f), (uint8_t)(o->ssg & 15) };
    }
    out[n++] = (pc_fm_reg_t){ 0xB0, (uint8_t)((p->fb & 7) << 3 | (p->alg & 7)) };
    out[n++] = (pc_fm_reg_t){ 0xB4, 0xC0 };      /* both speakers, no LFO */
    return n;
}

int pc_fm_tl_regs(const pc_fm_patch_t *p, int vel, pc_fm_reg_t out[4])
{
    int n = 0;
    for (int i = 0; i < 4; i++)
        if (pc_fm_carriers(p->alg) & (1u << i)) out[n++] = (pc_fm_reg_t){ (uint8_t)(0x40 + pc_fm_op_offset(i)), (uint8_t)tl_of(p, i, vel) };
    return n;
}

void pc_fm_fnum(float hz, int *block, int *fnum)
{
    /* f = fnum * 2^(block-1) * clock / (144 * 2^20): the lowest block whose F-number fits 11 bits */
    float f = hz * (144.0f * 1048576.0f / 7670453.0f) * 2.0f;   /* block 0 */
    int b = 0;
    while (f > 2047.0f && b < 7) { f *= 0.5f; b++; }
    int n = (int)(f + 0.5f);
    *block = b;
    *fnum = n > 2047 ? 2047 : n < 1 ? 1 : n;
}

int pc_fm_gm_patch(int program)
{
    /* by GM family (8 programs each) */
    static const uint8_t fam[16] = { 0, 5, 4, 11, 3, 2, 2, 1, 1, 8, 6, 10, 5, 14, 7, 5 };
    int p = program & 127;
    if (p == 6 || p == 7) return 9;              /* harpsichord, clavinet */
    if (p == 46) return 12;                      /* orchestral harp */
    if (p == 36 || p == 37) return 13;           /* slap bass */
    if (p >= 8 && p <= 15) return p == 12 || p == 13 ? 7 : 5;   /* marimba, xylophone; other chromatic -> bell */
    return fam[p / 8];
}

const pc_fm_patch_t *pc_fm_drum(int note, int *play_note)
{
    switch (note) {
    case 35: case 36: *play_note = 33; return &k_drum[0];
    case 37: case 38: case 39: case 40: *play_note = 50; return &k_drum[1];
    case 41: case 43: case 45: case 47: case 48: case 50: *play_note = note; return &k_drum[0];   /* toms: pitched kick */
    case 42: case 44: *play_note = 84; return &k_drum[2];
    case 46: *play_note = 80; return &k_drum[2];
    default: *play_note = note < 35 ? 33 : 88; return note < 35 ? &k_drum[0] : &k_drum[2];   /* cymbals and the rest */
    }
}
