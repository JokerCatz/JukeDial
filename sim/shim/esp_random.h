// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdint.h>
uint32_t esp_random(void);   /* xorshift32 seeded by sim_seed(); shim_esp.c */
