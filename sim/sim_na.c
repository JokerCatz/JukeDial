// SPDX-License-Identifier: LGPL-2.1-or-later
/* Console commands that exist only on the board (their code is #ifndef SIM in app/main/console.c): the simulator says so. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "sim.h"

/* first word of every command that console.c leaves out under SIM */
static const char *const NA[] = {
    "tasks", "stack", "heapwalk", "heapsnap", "heapdiff", "heaptrace", "hapticlen", "btscan", "btdevs", "btconnect", "wget", "loglvl",
    "usbmode", "inst", "sysmode", "btmode", "tone", "pair", "btforget", "ldac", "encbench", "fxbench", "crashlog", "crashtest", "safeshow", "power", "tfread",
    "put", "get", "sha", "pput", "ping", "serstat", "rm", "ls",   /* the serial file-transfer half */
};

bool sim_is_na(const char *cmd)
{
    if (!strncmp(cmd, "inst ", 5) && !strcmp(g_sim_mode, "inst")) return false;   /* the instrument's console is real in --mode inst */
    size_t n = 0;
    while (cmd[n] && !isspace((unsigned char)cmd[n])) n++;
    for (size_t i = 0; i < sizeof NA / sizeof *NA; i++)
        if (strlen(NA[i]) == n && !strncmp(cmd, NA[i], n)) return true;
    return false;
}
