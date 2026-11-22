// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* esp_system.h for the host: only esp_restart (shim_restart.c; it prints "SIM restart" and returns, the simulator never restarts). */
void esp_restart(void);
