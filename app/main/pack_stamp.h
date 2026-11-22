// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "pc/pc_pack.h"
/* The TF card content pack's stamp (_player/pack.txt): read once at boot after the card mounts (pack_stamp_boot), logged as
 * "APP pack state=<missing|ok|old> rev=<n> need=<PC_PACK_MIN> version=<v>"; the Status page shows the warning (pack_stamp_warning). */
void pack_stamp_boot(void);
pc_pack_state_t pack_stamp_state(void);
const char *pack_stamp_version(void);
