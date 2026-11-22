// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
/* The audio half of the fx service (spec §3.3). The board's fx_audio.c puts pc_fxchain_process into the post hook of audio_svc; the
 * simulator's sim/fake_fx_audio.c runs the same chain from a 10 ms timer on silence and prints what it takes from the mailbox. */
#include "pc/pc_fxchain.h"
void fx_audio_start(pc_fxchain_t *c);   /* once, before the first block; the chain outlives the firmware */
/* Board only (console `fxbench [id]`): CPU cost of each app alone at the running clock, spec §11 M1. Refuses (playing|bt|cast) while anything streams and
 * aborts at the end of the current run if a stream starts. Prints per app and rate (48000, 44100, 96000, 192000, 384000; calls of rate/100 frames, at most PC_FX_BLOCK = 480 as the chain splits them: blk_max_us is one such call)
 * "APP fxbench app=<id> rate=<hz> def=<%> p07=<%> int=<%> tail=<%> tail_reach_s=<s|>12> tail_min_db=<dB> blk_max_us=<n> mhz=<clock> blk_us=<duration of that block at the rate, us>"; a field that was not measured is "-"
 * (int=- state/buffer does not fit internally; tail=- not a reverb; tail_reach_s = silence until the output peak is below -700 dBFS, at most 12 s; tail = the next 2 s of silence). blk_max_us = slowest block of the noise runs, the first block of each run excluded.
 * Then a heap line and "APP fxbench done". */
void fx_bench(const char *id);   /* NULL or "" = every app of the registry; leading/trailing blanks ignored */
