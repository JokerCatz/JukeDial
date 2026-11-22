// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes sysmode (mode switches by reboot). */
/* Behaviour: none; the simulator never restarts. It writes the fx state as the board does, then says so on the console: tests see that a reboot was asked for. */
/* Empty: sysmode_resume. */
#include <stdio.h>
#include "fx_state.h"
#include "gamesave_svc.h"
#include "sysmode.h"

void sysmode_reboot_msg(const char *msg)
{
    fx_state_save_now();                    /* as the board: the sound is written before the restart */
    gamesave_flush();                       /* and games.txt */
    printf("APP sysmode reboot\n");
    printf("APP reboot msg=%s\n", msg);    /* what the board shows while restarting (it is not pushed here: the simulator stays on its page) */
}
void sysmode_reboot(void) { sysmode_reboot_msg("mode"); }
void sysmode_resume(void) {}
