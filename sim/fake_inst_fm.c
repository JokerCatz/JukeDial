// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes inst_fm (the YM2612 patch list: the built-in bank, then the card's .tfi files). */
/* Behaviour: the 16 built-in names (the real pc_fm_bank), the program every channel starts on; the card's patches are not read. */
/* Empty: the chip's tables (inst_fm_fast reports what was asked). */
#include "inst_fm.h"
#include "pc/pc_fmpatch.h"

static int s_prog;
int inst_fm_patch_count(void) { return PC_FM_BANK_N; }
const char *inst_fm_patch_name(int prog) { return pc_fm_bank[((prog % PC_FM_BANK_N) + PC_FM_BANK_N) % PC_FM_BANK_N].name; }
int inst_fm_program(int ch) { (void)ch; return s_prog; }
void inst_fm_set_program_all(int prog) { s_prog = prog & 127; }
int inst_fm_fast(int on) { return on; }
