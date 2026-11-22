// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* Serial commands over USB Serial/JTAG, one per line, so the host can drive tests without touching
 * the screen: scan | mp3 all | mp3 <folder> | chips | chip <system> [file] [sub] | chip mode loop|random |
 * scenes | noise <scene> | fxs | fx state|stat|on <id> 0|1|set <id> <param|mix> <v>|mode <bits> (not the battery bit)|import <file>|export|off | sleep <min> | next | stop | back | vol <0-100> | screen on|off | mem */
#include <stdbool.h>
void console_start(void);
/* One console line, dispatched like a serial line ("{" = settings protocol, else a command); runs on the LVGL task. */
void console_exec(const char *line);
/* another app (the instrument): its own commands; besides them only shot / tasks / mem, no settings protocol */
extern bool (*console_app_cmd)(const char *c);
