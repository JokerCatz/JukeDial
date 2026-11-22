// SPDX-License-Identifier: LGPL-2.1-or-later
/* esp_restart for the host: the firmware's "restart into the other app" (the instrument's exit to the player). */
#include <stdio.h>
#include "esp_system.h"

void esp_restart(void) { printf("SIM restart\n"); }   /* unlike the board's it returns: the simulator goes on, tests read the line */
