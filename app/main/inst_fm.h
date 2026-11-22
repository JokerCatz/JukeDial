// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* YM2612 FM chip (inst_chip_fm.cpp): patch names and the program every channel starts from */
int  inst_fm_patch_count(void);                 /* built-in 16 + the card's .tfi */
const char *inst_fm_patch_name(int prog);       /* prog wraps around the count */
int  inst_fm_program(int ch);
void inst_fm_set_program_all(int prog);         /* Auto / Direct programs of all channels (settings row) */
int  inst_fm_fast(int on);                      /* measurement: 1 = sine + envelope tables in internal RAM (24 KB) */
#ifdef __cplusplus
}
#endif
