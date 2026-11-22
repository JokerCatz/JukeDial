// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* YM2612 (OPN2) patches for the instrument's FM chip (stage 5): a built-in bank, TFI files from the card, and
 * the register values one channel needs. Operators are kept in logical order OP1..OP4.
 * TFI (TFM Music Maker / DefleMask, 42 bytes): algorithm, feedback, then 4 x 10 bytes per operator in register
 * order OP1, OP3, OP2, OP4: MUL, DT (0..6, 3 = none), TL, RS, AR, DR, SR, RR, SL, SSG-EG. */
typedef struct { uint8_t mul, dt, tl, rs, ar, dr, sr, rr, sl, ssg; } pc_fm_op_t;   /* dt as in TFI: 3 = 0 */
typedef struct { char name[20]; uint8_t alg, fb; pc_fm_op_t op[4]; } pc_fm_patch_t;

#define PC_FM_TFI_SIZE 42
#define PC_FM_BANK_N 16                  /* programs 0..15; card patches follow from 16 */
extern const pc_fm_patch_t pc_fm_bank[PC_FM_BANK_N];

int  pc_fm_tfi_parse(const uint8_t *b, size_t n, pc_fm_patch_t *p);   /* 0 ok, -1 not a TFI (values clamped) */
void pc_fm_tfi_write(const pc_fm_patch_t *p, uint8_t out[PC_FM_TFI_SIZE]);
unsigned pc_fm_carriers(int alg);       /* bit i = OP(i+1) is a carrier */

/* One channel's patch as (register, value) pairs, register relative to the channel (part 0 channel 0):
 * 0x30..0x9C per operator, 0xB0, 0xB4. Carriers get the velocity attenuation on top of their TL. */
typedef struct { uint8_t reg, val; } pc_fm_reg_t;
#define PC_FM_REGS 30
int  pc_fm_patch_regs(const pc_fm_patch_t *p, int vel, pc_fm_reg_t out[PC_FM_REGS]);   /* count */
int  pc_fm_tl_regs(const pc_fm_patch_t *p, int vel, pc_fm_reg_t out[4]);               /* carriers' TL only */
int  pc_fm_op_offset(int op);           /* logical op 0..3 -> register offset 0, 8, 4, 12 */
int  pc_fm_vel_att(int vel);            /* extra TL (0.75 dB steps) for a velocity, 0 at 127 */

/* frequency -> block (octave, 0..7) and 11-bit F-number for a 7.670453 MHz chip */
void pc_fm_fnum(float hz, int *block, int *fnum);

int  pc_fm_gm_patch(int program);       /* GM program -> built-in patch */
/* channel 10: the drum patch and the pitch it plays at */
const pc_fm_patch_t *pc_fm_drum(int note, int *play_note);
#ifdef __cplusplus
}
#endif
