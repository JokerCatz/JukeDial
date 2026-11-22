// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes charge_mode (the FakePod's charge-only boot): the page's model from the simulator's state keys. */
/* Behaviour: batt=<0-100> (default 87), charging=<0|1> (default 1), lowbat=<0|1> (the boot refusal's wording); "full" when charging=2. Start / power off only print a line: the simulator never restarts. */
#include <stdio.h>
#include "charge_mode.h"
int sim_state_int(const char *key, int def);

bool charge_mode_wanted(void) { return false; }
void charge_mode_run(void) { for (;;) {} }
void charge_mode_set_lowbat(int pct) { (void)pct; }
void charge_mode_status(charge_status_t *s)
{
    const int b = sim_state_int("batt", 87), c = sim_state_int("charging", 1);
    s->pct = b; s->charging = c == 1; s->full = c == 2; s->plugged = c >= 1; s->lowbat = sim_state_int("lowbat", 0) == 1;
}
void charge_mode_start_player(void) { printf("APP charge start player\n"); }
void charge_mode_power_off(void) { printf("APP charge power off\n"); }
