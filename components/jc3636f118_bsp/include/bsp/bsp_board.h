// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* "Is this the board this image was built for?" One cheap probe, before anything else touches a pin (guard_board_ok in app_main). The FakePod's PMU (AXP2101, 0x34)
 * sits on GPIO48 / GPIO47; the round board has nothing there. The FakePod image needs the PMU to answer; the round image refuses when one does. Returns true to go on.
 * *what (optional) gets a short word for the log: "ok", "axp2101 absent" / "axp2101 present". */
bool bsp_board_ok(const char **what);
#ifdef __cplusplus
}
#endif
