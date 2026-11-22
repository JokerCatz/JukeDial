# Test results

What was measured and verified, with the conditions. Read [testing.md](testing.md) first for how the layers work.

Every row carries one of three evidence levels:

- **measured on hardware**: numbers read from a board with the firmware's own counters or a console.
- **verified in simulator / host tests**: automated and repeatable from the repository on a PC.
- **observed in use**: a person used it and saw or heard no problem; no instrument.

Boards: **round** = JC3636F118 (360 x 360 round panel, knob, PCM5102A DAC with no hardware volume); **FakePod** = 460 x 460 square AMOLED, two keys, CS43131 DAC with hardware volume; **bench** = a second, bare ESP32-S31 development board (no screen, no DAC wired) used as a Bluetooth receiver, a USB test target and a benchmark host. The CPU clock default became 320 MHz on 2026-11-13; rows dated before that (2026-11-08 to 2026-11-12) ran at 240 MHz, so their CPU percentages are higher than today's. "Volume 0" means the DAC output was muted or at zero during the run, so the run measured the pipeline, not the sound. Counters named in rows: an *underrun* is an I2S DMA ring that ran empty; `blk_max` is the longest audio block time; cpu0 / cpu1 are the load of the two cores.

Nothing here is a guarantee for other cards, headsets, phones or computers. Repeat counts are given where they were recorded; where a row says "one run" it was one run.

## 1. Automated checks (PC)

| What | Conditions | Result | Evidence | Date |
|---|---|---|---|---|
| Host unit tests | `FAST=1 make test`, 12-thread PC | 184 test programs, 0 failed; about 30 s | **verified in simulator / host tests** | 2026-11-22 |
| Simulator scripted tests | `make sim-test` | `SIM TEST ok`; 39 pieces, about 1,600 named checks; 54 s | **verified in simulator / host tests** | 2026-11-22 |
| Leak cycles, round simulator | `make sim-leak`: 59 runs of 20 cycles each (after warm-up), ASan + LSan + UBSan | all ok: exit 0, no sanitizer output, LVGL byte count and timer count identical after every cycle; 180 s wall | **verified in simulator / host tests** | 2026-11-22 |
| Page pictures, round | `make sim-base`, `make sim-diff BASE=.local/sim/base` | 220 pages, 0 differ | **verified in simulator / host tests** | 2026-11-22 |
| Page pictures, FakePod | `make sim-base-fp`, `make sim-diff-fp`; also `make sim-test-fp` (all passed) | 232 pages, 0 differ | **verified in simulator / host tests** | 2026-11-22 |
| Static analysis of the portable core | `make lint`, `gcc -fanalyzer` at `-O0` and `-O2` | no warnings at either level | **verified in simulator / host tests** | 2026-11-22 |
| Static checks and analyzer of the firmware | `make lint-app` | passed at the last full run before this document | **verified in simulator / host tests** | 2026-11-22 |
| Zip-directory fuzz (host) | mutation fuzz inside `test_zip.c`, ASan + UBSan | 12 million mutated inputs, clean | **verified in simulator / host tests** | 2026-11-18 |
| Library scale (host) | `make lib-stress`: fake indexes of 2,000 to 50,000 tracks; PC times, not board times | after the tree-shaped library rework, the folder list for 50,000 tracks in 5,000 folders opens in 1.6 ms (was 5,293 ms) and every list page stays at 60-158 KB of LVGL memory; the index of 50,000 tracks parses in 2.5-3.1 ms and takes 8.5-10 MB of card | **verified in simulator / host tests** | 2026-11-17 |
| Format tiers and crash-safety logic | host tests and simulator; no board | in `test_tier`, `test_crashnet`, scan tests | **verified in simulator / host tests** | 2026-11-18 |
| Chip-music decoder determinism | same decode, host vs board (checksum of the PCM) | after a fix round every listed file's checksum was identical on the board and on the PC (5 DOS OPL files, 2 Atari YM files, 1 PICO-8 set, 1 tracker module, rerun twice) | **measured on hardware** | 2026-11-18 |

## 2. Audio pipeline

### 2.1 Formats and sample rates played natively

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Wired native rates 44.1 to 384 kHz | FakePod | ten synthetic 24-bit FLAC files (a 1 kHz tone on the left, a sweep on the right) at 44.1, 48, 88.2, 96, 176.4, 192, 352.8, 384 kHz and two 4,096-sample-frame variants; volume 0 | pitch correct on all ten (checked by ear on a unit session), 0 underruns in every steady window; at 384 kHz source rate = output rate, core 1 at 66 % | **measured on hardware**; the pitch check is **observed in use** | 2026-11-22 |
| Wired native 96 kHz | round | 24-bit FLAC and WAV, volume 0 | 0 underruns, core 1 up to 27 % | **measured on hardware** | 2026-11-14 |
| Wired 192 kHz and 384 kHz sources | round | the default wired ceiling is 48 kHz; higher sources are reduced by a halving filter | 192 kHz source reduced to 96 kHz: 0 underruns; native 192 and 384 kHz output through the PCM5102A was **not verified on hardware** (the checklist item is open) | **measured on hardware** (reduction only) | 2026-11-14 |
| Wired 16 to 32 kHz sources | FakePod | 22.05, 16 kHz sources; 11.025 kHz source | 22.05 kHz played upsampled to 44.1, 16 kHz to 32 kHz, 32 kHz native, 11.025 kHz refused with a reason | **measured on hardware** | 2026-11-20 |
| Rate switching | FakePod | back-to-back 48, 44.1, 96, 48, 32, 22.05, 88.2 kHz, 4 s apart | each switch 241-265 ms, DAC clock source flips correctly, no error line | **measured on hardware** | 2026-11-20 |
| Rate switching | bench | USB sound-card mode, 9 switches between 48 and 44.1 kHz | all clean (`underruns=0 overruns=0 drops=0` after the first switch; before a fix: 5 overruns, 149 drops) | **measured on hardware** | 2026-11-18 |
| I2S clock generation to 384 kHz | bench | no DAC; the I2S clock pins looped to a counter | XTAL path works to 96 kHz; the audio PLL is needed at 176.4 kHz and above (mclk 128 only at 352.8 / 384 kHz); clock error inside the measurement noise (about 2-9 ppm); 10 round trips 44.1 to 192 kHz each 120-165 us | **measured on hardware** (the DAC locking and the sound were not part of this test) | 2026-11-13 |

### 2.2 Underruns: steady state vs track start

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Steady-state underruns, wired hi-res | FakePod | 60 s windows, repeat-one, screen off, volume 0, default encoder frame sizes | 176.4 / 192 / 352.8 / 384 kHz: 0 in every steady window; FIFO minimum fill 210-230 ms of 250 ms | **measured on hardware** | 2026-11-21 |
| Underruns in the start window of a track | FakePod | 10 starts of each pair, per-source counter 4.5 s after start | 44.1 kHz MP3 to 44.1 kHz MP3: 0 (10 of 10); MP3 to 96 kHz FLAC: 12 every time; 96 kHz to 384 kHz FLAC: 61 every time (one separate 384 kHz start gave 98). The counts occur while the first FLAC frame is decoded and the DMA ring is empty. Whether the DAC output was audibly muted during them could not be separated at volume 0 | **measured on hardware** | 2026-11-21 |
| Pause and resume at 192 kHz | FakePod | 10 repeats | up to 250 ms of buffered audio dropped on pause (9 x 250 ms, 1 x 240 ms) and on resume, as documented; no errors | **measured on hardware** | 2026-11-21 |
| 130 s of 384 kHz FLAC with the largest frame size | FakePod | 32,768-sample frames, 65 s screen on then 65 s screen off | 0 underruns except in the window containing the track start (91) | **measured on hardware** | 2026-11-20 |
| 5 minutes of 192 kHz | FakePod | decode-ahead FIFO of 250 ms | steady windows 0 underruns | **measured on hardware** | 2026-11-20 |
| 21-minute MP3 soak | FakePod | 48 kHz, repeat-one, volume 0, screen on 5 min then off | 130 stats windows: 0 underruns in 129; 12 in the first (track start); `blk_max` 8.3-10.0 ms; cpu0 20 %, cpu1 11 %; internal free RAM flat (278.7 to 278.4 KB) | **measured on hardware** | 2026-11-20 |
| 61-minute MP3 with effects | round, early firmware | volume 0 | 0 underruns, internal free RAM flat at about 335 KB (earlier firmware than today's) | **measured on hardware** | 2026-11-09 |

### 2.3 CPU load by content (round board)

| What | Conditions | Result | Evidence | Date |
|---|---|---|---|---|
| MP3, 48 kHz stereo | volume 10, screen off, 10 s windows | cpu0 13-21 %, cpu1 10 %, `blk_max` 4.7-5.6 ms, 0 underruns; unchanged before and after forced flash operations (a check that the second core's branch predictor is back on after flash writes) | **measured on hardware** | 2026-11-19 |
| CPU clock 320 / 240 / 160 MHz (experimental setting), MP3 | screen off, volume 0, 10 s windows | cpu1 11 / 13-14 / 18-19 %; `blk_max` 5.0 / 6.2 / 7.3 ms; 0 underruns | **measured on hardware** | 2026-11-20 |
| same, 96 kHz FLAC reduced to 48 kHz | same | cpu1 24-26 / 30-33 / 42-47 %; `blk_max` 18 / 21 / 26 ms; 0 underruns | **measured on hardware** | 2026-11-20 |
| same, a Mega Drive chip file | same | cpu1 14-17 / 17-21 / 23-30 %; 0 underruns | **measured on hardware** | 2026-11-20 |
| same, MP3 with four heavy effects | same | cpu1 33-42 / 41-54 % at 320 / 240 MHz; 0 underruns | **measured on hardware** | 2026-11-20 |
| FLAC decode cost, round-equivalent code on the bench board | 4,096-sample frames, per 10 ms of audio, level 5 / level 8 encodings | 44.1 kHz 4.3 / 5.8 % of a core; 96 kHz 10.0 / 13.7 %; 192 kHz 19.6 / 22.2 %; 192 kHz with a 2:1 reduction 32.6 / 35.2 %; 384 kHz 40.3 / 44.5 %; 384 kHz with a 2:1 reduction 71.0 / 75.2 % | **measured on hardware** | 2026-11-13 |
| FLAC at 352.8 / 384 kHz, wired | FakePod, screen on | 0 underruns after a read-ahead task was added (before: 200-570 per 10 s because the card read, about 3.5 MB/s, blocked the decoder); core 1 up to 86-89 % on a worst-case noise file, 72-80 % on typical content; core 0 about 90 % on the playing page | **measured on hardware** | 2026-11-20 |
| Playback speed 0.75 to 2.0x | round, core 1 | 11 % at 1.00x, 16-18 % at 1.25x, 19-20 % at 1.50x, 20-23 % at 2.00x; 0 underruns | **measured on hardware** | 2026-11-17 |
| Chip-music decode cost (decode only; the pipeline adds about 16-20 points with the screen on) | round, 10 s runs | SID 24-31 % (two chips 43 %); DOS OPL 22 files 7-44 % (median 17); PICO-8 6-17 %; tracker modules 2-23 %; HVL / AHX 3-4 %; Atari YM 2 %; AIFF 6-7 %; TTA 5-10 %; Musepack 3 %. Real playback, the heaviest OPL file 57-64 % on core 1 with the screen on | **measured on hardware** | 2026-11-18 |
| Amiga sound modes (experimental setting) | round | 4-channel modules 20-39 % decode only, 23-55 % in real playback with the screen on; `blk_max` up to 12.9 ms; 0 underruns. 6- and 8-channel files (generated test files) 4-15 % and unchanged by the mode | **measured on hardware** | 2026-11-18 |
| Effect cost classes | board bench (`fxbench`), 48 kHz, parameter 0.7 | gain, balance, widener, crossfeed, loudness 0.14-1.4 % of a core; limiter 2.0 %; bass 3.9, equalizer 4.5, tone 6.1 %; freeverb 11.2, tube 13.0, compressor 13.7, velvet widener 15.2 %; the room reverb about 66 % | **measured on hardware** | 2026-11-14 |

### 2.4 Gapless, crossfade, speed

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Crossfade, 6 s | round | natural end of a 48 kHz track into a 48 kHz track | console shows the crossfade start 6,000 ms before the end and the early change of title and cover; the join was clean on the console. Not triggered on a manual skip or when the sample rate differs (by design). The early title and cover change has not been confirmed by eye, and no listening test of crossfade was done | **measured on hardware** | 2026-11-22 |
| Crossfade cost, 4 s | round | core 1 | 19 %; `blk_max` 13-18 ms; 0 underruns | **measured on hardware** | 2026-11-17 |
| Gapless join | round, simulator | start near the end of a track, pre-open of the next, repeated | join every cycle in the leak run; board gapless runs and ReplayGain were exercised once (2026-11-10) | **verified in simulator / host tests** | 2026-11-22 |

## 3. Memory

All free-memory figures are internal RAM (the scarce kind) unless named PSRAM (16 MB external).

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Static internal-RAM image ceiling | both | `make size`, `make size-fakepod` | round 201,548 B, FakePod 194,910 B; 0 B slack; stock-image ceilings 201,452 / 194,814 B | **verified in simulator / host tests** (a link-time check) | 2026-11-22 |
| Player idle, menu | round | screen off | 269.5 KB free, largest block 131.1 KB, lowest point 163.4 KB | **measured on hardware** | 2026-11-17 |
| Bluetooth speaker mode idle | round | | 173.2 KB free, largest 75.8 KB, lowest 108.2 KB | **measured on hardware** | 2026-11-17 |
| Auracast broadcast with 48 kHz MP3 | round | | 162.4 KB free, largest 63.5 KB; the lowest point after cycling speaker, headphone and broadcast modes in one boot was 53.5 KB | **measured on hardware** | 2026-11-17 |
| Headphone mode (Bluetooth up) | round | | about 171 KB free, largest block 63.5 KB | **measured on hardware** | 2026-11-11 |
| Cost of single features (internal RAM) | bench and round | | Bluetooth LE + broadcast 97 KB; Wi-Fi driver and TCP/IP stack 76 KB, 59 KB after the static transmit buffers were reduced from 16 to 6; audio service and card 25 KB; display, LVGL and panel 40.8 KB at boot | **measured on hardware** | 2026-11-16 |
| Lowest internal free RAM over a night | round | an unattended session with soaks, scans, card tests | 108,976 B, unchanged throughout | **measured on hardware** | 2026-11-21 |
| Lowest internal free RAM | FakePod | the same night | 34.8 KB, set by the full-cover playing style and hi-res runs; with the style before it was 73.7 KB | **measured on hardware** | 2026-11-21 |
| PSRAM cost of a 20,000-track index | round | fake library, load time 349 ms | 5.3 MB (the host estimate was 3.3 MB, too low) | **measured on hardware** | 2026-11-17 |
| Library rescan memory | round | 3,026 tracks, `mem` every 0.7 s | internal free flat at 259,567 B, lowest unchanged | **measured on hardware** | 2026-11-21 |
| Cover thumbnail pack | both | per cover | card 115,200 B pixels + 32 B slot; PSRAM 24 B per table slot; internal RAM 0 | **measured on hardware** | 2026-11-21 |
| Screen-off "free RAM jumps by 30 KB" | round | explained by display driver buffers: the last two queued frame chunks (2 x about 15 KB) stay allocated until another command is sent; a fix returns them when the screen goes dark | before: 240.6 KB idle in the dark; after: 274.0 KB; lit and drawing: 254.7 KB. Compare readings in the same UI state | **measured on hardware** | 2026-11-11 |
| Memory-pressure degradation | round | forced levels at 100 / 48 / 32 KB free | level 1 skips cover decoding, level 2 freezes spectrum and cover rotation, level 3 defers statistics saving; each level confirmed on the board; normal playback stayed at level 0 | **measured on hardware** | 2026-11-10 |

### 3.1 LVGL bytes over long sessions: cache or leak?

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| LVGL object memory in leak cycles | round, simulator | every cycle of section 4 | returns to the same value after every cycle once the glyph cache has warmed up | **verified in simulator / host tests** | 2026-11-22 |
| LVGL bytes during 120 track changes | round | Auracast broadcast test | one rise from 78.6 KB to about 130 KB during the first changes, then flat for the remaining changes: judged a font-glyph and cover cache filling, not a leak | **measured on hardware** | 2026-11-13 |
| Growth during a long session | FakePod | one overnight session after many page visits | LVGL bytes grew from 53 KB at boot to 251 KB and free PSRAM fell from 12.5 MB to 10.4 MB. The growth is plausibly caches (the cover table alone is 219 KB), but it was **not checked for leaks**: no leak loop was run on this board and the growth was not shown to stop. Treat as unresolved | **measured on hardware** (the growth); the explanation is not verified | 2026-11-21 |

## 4. Leak testing

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Simulator cycles, round | simulator | 59 runs (screens, tools, every game, instruments, external-mode pages, song tree, gestures, Bluetooth audio pages and others), 20 cycles each | all ok: no sanitizer output, LVGL bytes and timers identical after every cycle | **verified in simulator / host tests** | 2026-11-22 |
| Simulator cycles, FakePod | simulator | 10 runs, 20 cycles each (the 2 x 4 menu grid, keys, status strip, paging, two games) | 10 of 10 runs ok. The key-navigation run (`fpkeys`) needs a warm-up of 80 cycles: with 20 it failed the exact rule by 554 bytes that repeat exactly (caches still filling); with 80 the count is constant and the sanitizers are silent. The run list uses 80 since 2026-11-22. | **verified in simulator / host tests** | 2026-11-22 |
| Board leak loops | round | one session over serial, 28 action loops: idle, screen on/off, 14 pages, tools, clock (30, 80, 40 cycles), noise, theme, next-track (60, 120 cycles), play/stop (50), pause (40), seek (50), gapless (30), spectrum (40), cover (40), Wi-Fi on/off (25), radio start/stop (25), photo album (80, 40, 40), headphone mode with the peer reset (20 cycles, then 15 + 30 with playback; about 70 reconnects of 10.6-12.2 s each), instrument app pages (40), chips (44), kits (30), looper (30 + 40) | internal-RAM trend zero in every loop; every non-zero slope was a one-off step or a cache that flattened (clock glyph cache, first use of each of 11 instrument chips); the largest free block never shrank. Verdict recorded: no leak in any action | **measured on hardware** | 2026-11-11 |
| Window of HUD clock faces | round | 9 groups of 10 open/close cycles | internal RAM, PSRAM and LVGL bytes returned exactly to their original values after every group | **measured on hardware** | 2026-11-18 |
| Speaker-mode off/on | round | 8 off/on attempts with the phone side lost | memory flat across all (internal free 175.1 KB, LVGL 36.2 KB) | **measured on hardware** | 2026-11-19 |

Not done: board leak loops since the second half of November (the games, tools, external mode and instrument pages have simulator cycles only), and no board leak loop on the FakePod.

## 5. Long runs and soaks

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Soak over all radio formats, MP3 and noise | round | 53 minutes | 0 underruns, 0 resets | **measured on hardware** | 2026-11-10 |
| LDAC 990 kbps, MP3, Wi-Fi off, reduced effects | round, bench as receiver | 35 minutes | 0 lost packets, 0 receiver gaps, 0 bad frames; 0 underruns; core 1 54 % (encoder 31 %, audio 19 %); internal free RAM 160 to 157.5 KB; 0 watchdog events | **measured on hardware** | 2026-11-10 |
| LDAC top 660 kbps, Wi-Fi on, radio, reduced effects | same | 35 minutes | 0 lost, 0 gaps; rate adaptation changed 17 times, all between 660 and 492 kbps and back within 10 s; core 1 59 % | **measured on hardware** | 2026-11-10 |
| LDAC 990 kbps with full effects | same | 35 minutes | 0 lost, 0 gaps; core 1 75-76 %; 0 watchdog events | **measured on hardware** | 2026-11-10 |
| LDAC 660 kbps with full effects, Wi-Fi on, radio | same | 35 minutes | 0 lost, 0 gaps; rate adaptation 16 times (once down to 330 kbps), back to 660 quickly; core 1 average 75 %, maximum 79 % | **measured on hardware** | 2026-11-10 |
| aptX HD 576 kbps, Wi-Fi on, radio | same | about 25 minutes | **492 packets lost (about 0.3 %)**; 144 gaps in the receiver, about 4.4 s of audio in total; losses concentrated in 8 of the 25 one-minute intervals; core 1 73 % (maximum 84 %); not a CPU problem (about 22 % idle). aptX has a fixed bit rate and no adaptation. After this run a step-down to aptX and then SBC on persistent loss was added; the loss rate after that change has not been re-measured | **measured on hardware** | 2026-11-10 |
| Auracast broadcast loop at 320 MHz | round | 24 kHz stereo; 25 min with effects and a chip file (85 track ends), 20 min with a track change every 10 s (120 changes), 10 min steady; 55 minutes in all | 0 controller asserts, 0 unexpected resets, 0 underruns; core 1 up to 61 % | **measured on hardware** | 2026-11-13 |
| 3,026-track library, rescan | round and FakePod | one run each | round 61.5 s, FakePod 60.9-61.8 s (3,040 and 3,036 tracks with test files) | **measured on hardware** | 2026-11-21 |
| Two-minute MP3 soaks, screen on then off | round and FakePod | 3 folders each, volume 0; also on the round board with four effects on | 0 underruns in every run except one FakePod run whose screen-off part had 14 in the track-start window; round cpu0 48-62 % (screen on) / 22-42 % (off), cpu1 11-13 % (41-45 % with effects); FakePod cpu0 26-39 %, cpu1 11-12 %; `blk_max` 5.6-6.0 ms round, 8.3-8.8 ms FakePod; internal free 248-273 KB | **measured on hardware** | 2026-11-21 |
| Unattended overnight session | both | volume 0; frame-rate sweep, hi-res windows, card tests, scans, soaks (rows throughout this page) | no resets; the FakePod printed 141 task-watchdog warnings while the background cover builder ran (see section 8) | **measured on hardware** | 2026-11-21 |
| Speaker-mode switch-off with the link lost | round, bench as phone stand-in | 8 attempts to switch speaker mode off with the link lost at different moments, each preceded by at least 12 s of streaming | 0 panics (before the fix 6 of 7 panicked); speaker mode turned on again every time | **measured on hardware** | 2026-11-19 |

Not done: a single continuous run of several hours on either board with both screen and music on; LDAC continuous listening on a real headset for 30 minutes (queued for a person).

## 6. Bluetooth

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| LDAC with a real headset | round | Sony WH-1000XM5, MP3, Wi-Fi off | codec LDAC at 990 kbps, over 14,000 packets sent with 0 send failures and 0 lost, encoder about 40 % of core 1 (core 1 55 %), 0 underruns, absolute volume on | **measured on hardware** | 2026-11-22 |
| LDAC with a real headset | FakePod | same headset, first run of the default image | 990 kbps, 0 failures, 0 lost, encoder 55.6-55.8 % of core 1 while the rotating-cover page was drawing (38-40 % later with a static page), core 1 69 %, 0 audio underruns, 84 stream underflows that did not grow | **measured on hardware** | 2026-11-22 |
| LDAC bit-rate steps, walk test | FakePod | same headset; walk away and back | 990 to 660 to 492 to 330 kbps in single steps with 0 lost; about 80 s steady at 330; back up one step at a time. A second, sudden drop went 990 to 330 kbps and lost 59 packets in about 20 s, then none; encoder 38-40 %; link never dropped. A listener reported one audible break at the far point, clean again on return | **measured on hardware**; the listening note is **observed in use** | 2026-11-22 |
| LDAC steps with a limited link | round, bench as receiver | link limited by a test setting to 400 kbps (240 MHz firmware) | steps 990, 660, 492, 330 within 2 s; 72 lost packets in total around the drop, then none; after the limit was removed the rate climbed back to 990 kbps in about 95 s, one step at a time | **measured on hardware** | 2026-11-10 |
| Codec choice | round, bench | the receiver offers different subsets | all four offered: LDAC; aptX HD plus aptX: aptX HD; aptX only: aptX; none: SBC. Order is LDAC, aptX HD, aptX, AAC, SBC. A forced failure cascade (LDAC refused for MTU, then aptX HD and aptX for memory) ended in SBC and the receiver kept playing | **measured on hardware** | 2026-11-10 |
| aptX and aptX HD with a decoding receiver | round, bench | 1 kHz test tone at -6 dBFS | aptX HD 48 kHz: 576 kbps, 1000.00 Hz, SNR 79 dB; aptX 48 kHz: 384 kbps, SNR 68 dB; aptX 44.1 kHz: 353 kbps, SNR 61.5 dB (with a 48 to 44.1 resample); 2 packets lost at the moment Wi-Fi starts, none otherwise. No real aptX headset has been tested | **measured on hardware** | 2026-11-10 |
| Encoder CPU (library only) | bench | 320 MHz, music-like signal | LDAC 17 % (44.1 kHz), 19 % (48 kHz), 28 % (96 kHz, 990 kbps) of a core; aptX 37-45 %, aptX HD 38-41 %; decode about 32-34 % | **measured on hardware** | 2026-11-13 |
| Speaker mode (a phone sends to the player) | round | a phone, after a re-scan | AAC 44.1 kHz; title and cover received; first "no sound" report was the shared volume being low | **measured on hardware**; the sound itself **observed in use** | 2026-11-22 |
| Per-output volume | round | headset first connected, then wired | first-connection volume 33; the headset's value is stored by address; the wired volume kept its own value (90, then 74); headset next, previous and volume keys worked | **measured on hardware** for the stored values; the keys **observed in use** | 2026-11-22 |
| Auracast broadcast, receiver side | round, bench as receiver | 2-board run | receiver saw 13,000 packets with 1 lost | **measured on hardware** | 2026-11-09 |
| Auracast broadcast, CPU | round | 48 kHz stereo, MP3 and effects | core 1 44 % at 320 MHz against 54 % at 240 MHz, no overrun; sources above 48 kHz are refused for broadcast | **measured on hardware** | 2026-11-13 |
| Controller assert | round | Auracast at 240 MHz | one controller assert in about 3.4 minutes of broadcast at 240 MHz; not reproduced in 55 minutes at 320 MHz (see section 5); the cause is not established | **measured on hardware** | 2026-11-13 |
| Bluetooth state-machine logic | host | codec stepping, metadata, volume rules, remote-volume | host tests pass | **verified in simulator / host tests** | 2026-11-22 |

## 7. Display

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Spectrum pages | round | all styles, MP3 playing, 360 x 360 | 30.9 fps (the cap), 0 underruns; heaviest styles (ring and phase) about 25 ms of drawing per round and core 0 at 57-65 % | **measured on hardware** | 2026-11-17 |
| Frame rate by page | FakePod | 460 x 460, MP3 playing, screen on, 20 s windows, 0 underruns | bars 21-28 fps; scope 28 fps; waterfall and phase 31 fps; ring styles 11-13 fps; Game of Life 10.5 fps; the old nine-box playing style 13.8 fps; static pages (menu, settings idle, the clocks, playing page A and B) redraw only on change | **measured on hardware** | 2026-11-21 |
| Panel flush | FakePod | the same | frame time follows the changed area, not drawing cost: about 4.0-4.4 MB/s of panel data (71 k pixels = 35 ms, 176 k pixels = 80 ms) | **measured on hardware** | 2026-11-21 |
| Panel SPI clock 40 / 60 / 80 MHz | FakePod | spectrum bars | 38.0 / 36.0 / 35.2 ms per frame; panel throughput 21.8 / 31.0 / 39.2 MB/s; no artefacts at any; 40 MHz kept. The page's own 33 ms update tick sets the frame time, so the clock is not the limit. Not measured on the round board | **measured on hardware** | 2026-11-22 |
| HUD clock face open time | round | | about 125 ms to open (0-45 ms for ordinary faces); PSRAM 644-650 KB while open | **measured on hardware** | 2026-11-18 |
| Cover decode | both | | round: hardware JPEG path 50-100 ms per cover; FakePod: own software decoder about 3.4 s per cover because the hardware decoder refuses that picture size | **measured on hardware** | 2026-11-21 |
| Layout, tap-target size, page look | simulator | 220 round pages, 232 FakePod pages | see section 1 | **verified in simulator / host tests** | 2026-11-22 |
| Touch feel, rotation, readability on the AMOLED | both | | see "Not verified on hardware" below for what remains; the items listed in section 10 were used and accepted by one person | **observed in use** | 2026-11-22 |

## 8. Storage and USB

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Card raw read speed | round | three runs, `cardtest` | start region 13.9, middle 15.8, end 15.8 MB/s; average 15.1; file write 1.97, read 13.13 MB/s with verification | **measured on hardware** | 2026-11-21 |
| Card raw read speed | FakePod | three runs, 128 GB card | start region 2.76, 3.36, 4.44 MB/s (rising run to run); middle 10.9-11.0; end 9.4-11.1; file write 6.49, read 12.07 MB/s with verification. A second session: raw 3.96 / 10.76 / 11.32; file write 5.96, read 9.77; all format rows "yes" | **measured on hardware** | 2026-11-21, 2026-11-22 |
| Card read speed, reference | both | direct reads | FakePod's card gave about 3.5 MB/s at 4 KB to 64 KB read sizes (1.4 MB/s at 512 B); round 10.6 MB/s | **measured on hardware** | 2026-11-20 |
| Format ladder thresholds | logic | seven formats from MP3/AAC/Opus 320 kbps up to FLAC 384 kHz and WAV 384 kHz / 32-bit; need = stream bytes per second x 1.5; FLAC streams counted at 75 % of uncompressed; "marginal" at 75 % of need | the heaviest, WAV 384 kHz / 32-bit, needs 3.07 MB/s of the card; with the ladder, a verdict flipped with the slow start region of the FakePod's card (384k/32 WAV "no" at 2.76 MB/s) | **measured on hardware** (the speeds); the thresholds are **verified in simulator / host tests** | 2026-11-21 |
| Copy of a library through the USB drive mode | round and FakePod | 3,026 tracks, 18.4 GB from a PC | round 76 minutes (about 4.0 MB/s); FakePod 54 minutes (about 5.8 MB/s); sizes all correct; after unmount and remount 60 random files per board compared bit-identical. With a first FakePod cable the PC reset the USB link 14 times in 7 minutes; the card was then reported missing after boot until a full power-off. A different cable gave 0 resets | **measured on hardware** | 2026-11-20 |
| Serial file upload speed | both | console upload | about 0.3 MB/s | **measured on hardware** | 2026-11-20 |
| exFAT | FakePod | a 128 GB card formatted exFAT; 3,036 tracks | mounts, scans and plays; no corruption seen. The round board's exFAT behaviour has not been recorded | **measured on hardware** | 2026-11-20 |
| Cover pack capacity | both | pack sized per track count | 1 GiB limit allows about 9,300 pictures; the pack grows by compaction at the end of a pass | **measured on hardware** (sizes); the logic is **verified in simulator / host tests** | 2026-11-21 |
| Library scan on a small card | round | 70 files | 549-652 ms | **measured on hardware** | 2026-11-14 |
| Drive mode write coalescing and the host-sleep path | both | reads of the code and unit tests | the debug side only was exercised on hardware; the data-side protections are not yet verified on hardware | **verified in simulator / host tests** | 2026-11-21 |

## 9. Boot, recovery and power

There is no measurement of current draw or battery life on either board. The round board's charge current (about 600 mA) is a datasheet estimate, not a measurement.

| What | Board | Conditions | Result | Evidence | Date |
|---|---|---|---|---|---|
| Boot-loop guard and safe mode | round, bench | a deliberate crash test three times in a row | the third consecutive crash enters safe mode (counter 3), which starts only the display and prints one line every 5 s; a host reset leaves it; the guard clears its counter after about 60 s of healthy running. The safe screen itself was not seen by eye in this run | **measured on hardware** | 2026-11-19 |
| Boot after a crash resets the CPU clock setting | round | experimental clock set to 240, crash test | after the crash the clock setting returns to 320 MHz | **measured on hardware** | 2026-11-20 |
| Crash cause: a branch predictor left on in the parked core | round | five boots, forced flash operations | fix verified: 5 of 5 healthy boots, no CPU load change | **measured on hardware** | 2026-11-19 |
| Parking the other core | bench | 100,000 park/release pairs with the predictor handling, plus a control without it | with the handling: 0 failures in 110,000 pairs (hold average 25 us, maximum 31 us); the control locked up the CPU after about 8,000 and about 74,000 pairs in its two long runs | **measured on hardware** | 2026-11-20 |
| Run-time CPU clock switching | bench | 320, 240, 160, 80, 53, 40 MHz, about 330 switches | 0 failures; a switch takes 57-170 us; costs about 2 KB of internal RAM; below 150 MHz flash and PSRAM drop to 20 MHz. Not tested: during a connected Bluetooth stream, with the native USB, under heavy load | **measured on hardware** | 2026-11-20 |
| Panel power-cycle fix | FakePod | soft reboots, garbage on the first rows | before the fix 3 of 5 reboots bad; after 0 of 18; cold boot fine | **measured on hardware** | 2026-11-22 |
| Real-time clock | FakePod | PCF85063 | answers, set from a PC, read back; kept across a reflash reboot (3 s off the PC) and across a full power-off | **measured on hardware** for the read back; the retention is **observed in use** | 2026-11-22 |
| Settings retained across boots | both | | settings and per-headset volumes retained | **observed in use** | 2026-11-22 |
| Boot picture time | FakePod | boot picture of 360 x 360 pixels | the picture itself 2.012 s; the main task starts 2.08 s later than without it | **measured on hardware** | 2026-11-20 |
| Crash found by console fuzzing | round | an out-of-range index to a chip-file command | triggered a crash, three times in a row into safe mode; the code now checks zip indexes and a guard script forbids the pattern | **measured on hardware**; guard **verified in simulator / host tests** | 2026-11-18 |
| FakePod charging | FakePod | battery gauge during a bench round with playback | 4 to 32 % in about two hours of charging in constant-current mode (3,541 to 3,839 mV) | **measured on hardware** | 2026-11-20 |

## 10. Usage observations (a person, no instrument)

On 2026-11-22 one person used both boards and confirmed by eye or ear: the six settings groups; list gap on the square panel; card test icons; the volume pill; the spectrum page title; the pop-up Close/Leave choices; the half-targets (minus / plus) on a selected row; the time rows as rings; the gravity maze as the round board's default toy and the tilt maze on the FakePod in all four directions; velvet widener; playback speed; the four chip systems; the tuner; the LED ring; 180-degree rotation; noise; first sound on the FakePod wired output, left and right correct, no pops on pause or track change; three output drive levels (1.0, 1.41, 1.73 V) audible in order, with the chip's read-back register 0x10, 0x20, 0x30. All **observed in use**.

## Not verified on hardware at the time of release

- Round board: native 192 and 384 kHz output through the PCM5102A (the sample-rate test page on the device is the check); the panel SPI clock at 50 / 80 MHz; the experimental CPU clock during an A2DP or LDAC stream, with native USB and under heavy load; a continuous 30-minute LDAC listen; the rate-adaptation walk test on the round board (done on the FakePod only).
- USB drive mode: the data-side card protections (normal exit, a pulled cable, the "reconnect" cell, computer sleep, the stuck-card guidance text); the write-coalescing setting and the host-sleep release; how the three-in-one USB device behaves on each host operating system.
- Speaker mode: its own volume (default 60), the "leave speaker mode and play" confirmation, the order of volumes when entering speaker mode; headsets without absolute volume; whether the volume ramp when switching outputs is audible.
- Crossfade: the early title and cover change confirmed by eye; CPU while crossfading with two files of the same rate; how it sounds.
- Clock: Wi-Fi time sync and drift of the FakePod's RTC; setting the time from the screen start to finish; the round board's note that time is lost at power-off.
- FakePod: touch accuracy at the 29/30 scale; key feel; the CS43131 shutdown sound; battery gauge fusion; auto power-off, low-battery and charge screens; OLED blanking; two keys at power-on for download mode; finger hit rates on the nine-grid and 64-pixel buttons; readability on bright covers; fonts after an ordinary font flash. The FakePod's headphone-jack detection is not wired on the tested board.
- Both boards: a final flash of the release image and one boot of each; a long multi-hour soak with the screen on; the web tool flashing a FakePod image end to end from a hosted page.
- The bench board still runs an older image without LDAC and without the card-safety changes.
- A list of several hundred person-only checks (listening, handling, pairing, USB on real computers, crash tests) exists in the project's manual checklist; most items are not ticked off.

## Known limits found by testing

- Every FLAC track start shows a burst of counted underruns while the DMA ring is empty and the first frame is decoded: 12 at 96 kHz, 61 at 384 kHz on the FakePod, 0 for MP3 to MP3. It is counted separately from steady-state underruns; whether it is muted at the DAC was not separated.
- A pause above 96 kHz drops up to 250 ms of buffered audio and toys are silent while paused.
- A 384 kHz FLAC whose frame is larger than 32,768 samples is reduced to 192 kHz; 192 kHz FLAC with frames over 16,384 samples is reduced to 96 kHz.
- A slow card (below about 2.5 MB/s sustained) cannot keep up with 384 kHz FLAC; the card diagnostics page tells which formats a card can sustain. On the FakePod's tested card the first region of the card read at only 2.8-4.4 MB/s.
- On the FakePod the background cover builder uses its own decoder (about 3.4 s per cover) on core 0 and starves the UI: the FakePod printed 141 task-watchdog warnings (no reset) and the settings page drew 2 frames in a 20 s window until the builder was paused. A fix (yield or limit the decode size) is open.
- aptX HD over a busy Wi-Fi link lost about 0.3 % of packets in one 25-minute run (see section 5).
- LDAC with Wi-Fi on is limited to 660 kbps; aptX, aptX HD and LDAC are only available at 44.1 and 48 kHz and heavy effects are paused during them.
- Heavy effects (reverb, tube, compressor, and others) are paused above 48 kHz, on battery, and during Bluetooth codecs that need the CPU; the room reverb runs only wired, on mains power and at 48 kHz or below.
- A Bluetooth controller assert was seen once at 240 MHz during Auracast broadcast; none at 320 MHz in 55 minutes.
- FakePod headphone-jack detection reads "plugged" with and without a plug on the tested board.
- Playback has no seek; the progress bar only displays.
- The internal-RAM image has 0 bytes of slack on both boards; new internal-RAM statics need a saving elsewhere.
- The USB drive mode may need a full power-off to recover a card after an unstable cable.
