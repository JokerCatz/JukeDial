// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pc/pc_plug.h"

bool pc_plug_tick(pc_plug_t *p, bool usb_power, bool debug_port)
{
    if (!usb_power || debug_port) {          /* unplugged, or on the debug side: the next OTG plug-in asks again */
        p->otg_ticks = 0;
        p->asked = false;
        return false;
    }
    if (p->asked || ++p->otg_ticks < PC_PLUG_ASK_AFTER) return false;
    p->asked = true;
    return true;
}

int pc_usb_where(bool mounted, bool ejected)
{
    if (!mounted) return -1;                 /* no computer (also after an eject and the unplug) */
    return ejected ? 0 : 1;                  /* mounted: the computer has the card until it ejects it */
}
