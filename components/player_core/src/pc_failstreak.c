// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_failstreak.h"

bool pc_failstreak_record(pc_failstreak_t *f, bool ok)
{
    if (ok) { f->fails = 0; return false; }
    return ++f->fails >= PC_FAILSTREAK_MAX;
}
