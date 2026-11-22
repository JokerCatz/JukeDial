// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_guard.h"
uint32_t pc_guard_count(uint32_t prev, pc_boot_t kind)
{
    switch (kind) {
    case PC_BOOT_CRASH: return prev + 1;
    case PC_BOOT_POWER: return prev;       /* flat battery: not the firmware's fault, and not proof it is healthy */
    default: return 0;
    }
}
