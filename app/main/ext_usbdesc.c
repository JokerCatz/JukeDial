// SPDX-License-Identifier: LGPL-2.1-or-later
/* 外接模式 T4: ext_usbdesc.h */
#include "ext_usbdesc.h"
#include "bsp/bsp_audio.h"
#include "cfg.h"
#include "ext_audio.h"
#include "pc/pc_dacmax.h"
#include "pc/pc_exthid.h"

int ext_usbdesc_combo_norm(int stored) { return stored >= 0 && stored < PC_EXTCOMBO_N ? stored : PC_EXTCOMBO_ALL; }

int ext_usbdesc_build(int combo, uint8_t *buf, size_t cap, pc_extdesc_info_t *info, pc_extdesc_fifo_t *fifo)
{
    pc_extdesc_cfg_t c = { 0 };
    c.msc = pc_extdesc_combo_msc(combo);
    c.uac = pc_extdesc_combo_uac(combo);
    c.max_rate = pc_dacmax_norm(cfg_int("dac_max_rate", 0), BSP_DAC_RATE_DEFAULT);   /* the wired ceiling: the mixer path does not read the cap, so it is enforced here */
    c.drop_mask = ext_audio_drop_mask();                                              /* rates the DAC clock refused or read back wrong in an earlier entry (RTC note) */
    c.bits_mask = PC_EXTDESC_BITS_16 | PC_EXTDESC_BITS_24 | PC_EXTDESC_BITS_32;
    c.hid_report_len = pc_exthid_map_len;
    c.str_msc = 4; c.str_hid = 5; c.str_audio = 6;
    pc_extdesc_info_t in;
    int n = pc_extdesc_build(&c, buf, cap, &in);
    if (n > 0) {
        if (info) *info = in;
        if (fifo) pc_extdesc_fifo(&c, &in, fifo);
    }
    return n;
}
