// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Low-memory degradation (pc_lowmem): sampled twice a second; any task may read the level. */
void lowmem_start(void);
int lowmem_level(void);          /* PC_LOWMEM_* */
void lowmem_force(int level);    /* console 'lowmem N' (test); -1 = measure again */
