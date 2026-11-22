// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fakes instrument (the BLE-MIDI instrument app entered by reboot; the simulator has no instrument mode). */
/* Behaviour: none; a request is only printed, so tests see it was made. */
/* Empty: everything (no instrument request is ever pending). */
#include <stdio.h>
#include "instrument.h"

bool instrument_take_request(void) { return false; }
void instrument_request(void) { printf("APP instrument request\n"); }
void instrument_run(void) {}
