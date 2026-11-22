# How JukeDial is tested

This page explains the test layers, the commands that run them, what a pass looks like, and how long each takes. Everything here can be reproduced from the repository on a PC; the layers that need a board are listed separately at the end. For what the tests measured on hardware, see [test-results.md](test-results.md). For a map of the features being tested, see [features.md](features.md).

Evidence levels used in these documents:

- **verified in simulator / host tests**: automated and repeatable from the repository on a PC (this page).
- **measured on hardware**: numbers read from a real board (see [test-results.md](test-results.md)).
- **observed in use**: a person used the feature and saw no problem; no instrument was involved.

Timings below were taken on a 12-thread desktop PC with a warm build cache, unless marked "not timed".

## The layers at a glance

| Layer | Command | What it covers | Typical time |
|---|---|---|---|
| Host unit tests of the portable core | `make test` (`FAST=1 make test` skips the sanitizers) | `components/player_core`: decoders' glue, rate and pipeline rules, effects, games' rules, parsers, settings store, Bluetooth codec state machines | about 30 s with `FAST=1`; the sanitizer build takes longer (not timed) |
| Static analysis of the core | `make lint` | `gcc -fanalyzer` over every file of `components/player_core/src` at `-O0` and `-O2` | a few minutes (not timed) |
| Static checks and analysis of the firmware | `make lint-app` | the guards listed below, plus `gcc -fanalyzer` over `app/main` (cross-compile, nothing is run) | about a minute for the analyzer half; needs ESP-IDF and a prior `make build` |
| Desktop simulator, scripted tests | `make sim-test` | the real UI code and the portable core running on Linux against a fake board | about 54 s |
| Simulator, pixel comparison of pages | `make sim-base`, then `make sim-diff` | every page of the player, the instrument app, the external mode and safe mode | about 13 s together |
| Simulator, leak cycles | `make sim-leak` | repeated operations under AddressSanitizer / LeakSanitizer | about 3 minutes (180 s measured, 6 parallel jobs) |
| FakePod simulator | `make sim-test-fp`, `make sim-diff-fp`, `make sim-leak-fp` | the same, 460 x 460 panel, two keys | not timed |
| Web tool | `make webcheck` | the browser tool against a pretend serial port | about a minute (not timed) |
| Build matrix and RAM ceiling | `make build`, `make build-fakepod`, `make size`, `make size-fakepod` | all images link; the internal-RAM image stays under its ceiling | one image takes minutes (not timed) |

## 1. Host unit tests (`make test`)

`components/player_core` is plain C with no ESP-IDF dependency, so it is built and tested on the PC. The tests live in `components/player_core/host_test/` (one `test_*.c` per area; 184 test programs at the time of writing, run by CTest in parallel).

```
make test            # ASan + LeakSanitizer + UBSan: any leak, bad access or undefined behaviour fails
FAST=1 make test     # the same tests without the sanitizers (about 30 s)
```

Expected output: CTest ends with

```
100% tests passed, 0 tests failed out of 184
```

then a short "fx reference gate" block (`fx ref ... max_err=...` lines: the effects chain compared with reference renders; it prints `SKIP` lines when the reference renders are not present on your machine) and the Python helper tests (`test_ui_baseline`, `test_sim_diff`, `check_face_centering`, and the glyph tool's test, which skips itself when `fontTools` is not installed).

What the 184 programs cover, by group: audio backends and their hostile-input cases (WAV, AIFF, FLAC, MP3, MP4, Musepack, TTA, HVL, SID, DOS OPL players, tracker modules, PICO-8, Atari YM); the pipeline rules (sample-rate planning, halving, the burst FIFO, resampling, gapless, crossfade, speed); effects (each effect app, the chain order, the reference vectors); Bluetooth logic (codec choice and the LDAC bit-rate steps, AVRCP metadata, the volume rules); the settings store, the key-value parser and the card-path rules; library index and scan; the rules and scoring of every game; clock, alarm, timers, time-zone code; the web protocol's upload and zip handling; boot-loop guard and crash-safety logic.

Fuzzing is separate from the CTest run and is not part of the quick gates:

- A zip-directory fuzz inside `test_zip.c`; `PC_ZIP_FUZZ_ITERS` lengthens it.
- `tools/bench/opl_fuzz/` (coverage-guided, GCC trace-pc, ASan, fatal UBSan) over the DOS OPL player backend and `tools/bench/xmp_fuzz/` over the tracker backend. Both write findings outside git. Out-of-bounds reads that they found are fixed in small patches recorded under each library's `patches/` folder.

## 2. Static analysis of the core (`make lint`)

`make lint` compiles `components/player_core/src` with `gcc -fanalyzer` at `-O0` and `-O2` in two from-scratch builds; any `warning:` or `error:` fails. It also runs the "only one file may use the APLL clock" scan, the serial-port helper checks and the exFAT generator's self-test. The last line says which levels passed:

```
lint: ok at -O0 -O2 fatfs-selftest
```

## 3. Static checks of the firmware (`make lint-app`)

`make lint-app` runs a set of Python and shell guards in front, and the analyzer cross-compile of `app/main` in the background. The last line of the analyzer half is `lint-app: ok (N files)`. Each guard exists because a defect class bit the project once; the guard turns the class into a build failure.

| Guard (`tools/check/`) | Defect class it stops |
|---|---|
| `check_diram.py` | the internal-RAM image (code and static data that must live in internal RAM) growing past its ceiling; see "Build matrix" below |
| `check_flash_park.py` | stalling or parking the second CPU core without disabling its branch predictor; sleep modes enabled without the stall hook |
| `check_safe_mode.py` | safe mode (entered after three consecutive crashes) starting anything on core 1, or Bluetooth, audio, Wi-Fi, USB or card code |
| `check_flash_writes.py` | flash or NVS writes from a task whose stack is in external RAM (the cache is frozen during a flash write) |
| `check_cfg_keys.py` | a settings key that is not in the key list (a typo would silently create a new key) |
| `check_card_paths.py` | card paths spelled outside the one header that defines them |
| `check_zip_index.py` | indexing a zip entry array with a number that came from a file |
| `check_card_zip.py` | the card-template zip containing a path the firmware does not use or anything credential-like |
| `check_icons.py` | icon registry mistakes (duplicate code points, wrong class of icon in a ring cell, stale generated header) |
| `check_games.py`, `check_help.py` | the game registry and the help texts being well formed and complete |
| `check_font_glyphs.py` | a character in a UI string that no font file can draw |
| `check_palette.py`, `ui_colour_lint.py` | spectrum colour ramps unreadable on light themes; colour literals appearing on pages (a ratchet that may only shrink) |
| `ui_lint.py` | pages hand-building widgets instead of composing the shared UI pieces (a ratchet) |
| `check_vendor.py` | vendored third-party code drifting from the recorded hashes |
| `check_paths.py` | documents, scripts or comments naming a repository path that does not exist |
| `check_fakepod_macro.py` | a FakePod source compiled without the FakePod board macros (it would silently build the round board's code) |
| `check_chiptest_untracked.py` | copyrighted chip-music test files being committed |
| `apll_guard.sh`, `ext_audio_guard.sh` | any second user of the audio PLL; the USB sound-card path bypassing the volume and limiter rules |
| `tools/check_sdkconfig.py` | a build directory whose sdkconfig does not match the image it claims to be (default vs stock) |
| `make gen-check` | committed generated files (FM tables, theme template, icon header, string tables, web font sets) out of date |
| `make notices-check`, `make spdx-check` | the third-party notices table missing a vendored component; a source file without an SPDX line |

Four of them run inside `make size` / `make size-fakepod` instead, because they read the linked ELF: `check_diram.py`, `check_flash_park.py`, and the FakePod macro check.

## 4. The desktop simulator (`make sim`, `make sim-test`)

`sim/` builds the real firmware UI (`app/main/ui/*.c`), `components/player_core` and the portable parts of `app/main` for Linux with SDL2. Hardware services are replaced by fakes (`sim/fake_*.c`): audio, Bluetooth, the card, the clock, the IMU, the radio and so on. Virtual time makes tests fast and deterministic. The simulator is built with AddressSanitizer, UBSan and LeakSanitizer; any sanitizer output makes a test fail.

```
make build           # once: creates app/sdkconfig and app/managed_components, which the simulator reads
make sim             # builds sim/build/sim (about 23 s on 12 threads)
make sim-run         # a window; mouse = touch, scroll wheel = knob
make sim-test        # the scripted tests
```

`make sim-test` first checks the help texts, then runs the pieces in `sim/tests/` (39 numbered shell pieces plus a helper file) as separate processes, `min(CPU count, 8)` at a time; the pieces contain about 1,600 named checks. Pass line:

```
SIM TEST ok
```

On failure it prints `FAIL <name> | <evidence>` lines and `SIM TEST N failed:`. Useful variants: `make sim-test ONLY=viz` (only pieces whose name contains `viz`), `CASE=<regex>` (only matching cases), `SIM_TEST_JOBS=1` (one process, in order). Console commands can also be sent by hand: `sim/build/sim --headless --root DIR --cmd "uipush menu; @1; shot out.png"`.

The simulator is the tool for UI behaviour, page layout, rules and sanitizer coverage. It does not measure audio quality, CPU load, real memory use, Bluetooth, USB or radio behaviour; those need a board.

### Pixel comparison of pages

`tools/simtools/ui_baseline.py` walks a fixed list of pages with fixture data, a fixed clock and a fixed random seed, and takes a screenshot of each. The comparison counts differing pixels per page.

- Round panel: 220 pages (the player, the instrument app, the external mode and the safe-mode screen).
- FakePod panel: 232 pages.

Baselines are generated locally; they are not stored in the repository. Create your own before a refactor, then compare after each step:

```
make sim-base        # writes .local/sim/base (round)
make sim-diff BASE=.local/sim/base
make sim-base-fp     # writes .local/sim/base_fp
make sim-diff-fp
```

Each page prints `<name> diff_px=<n> bbox=<x,y,w,h>`; changed pixels are drawn in red over the dimmed baseline in `.local/sim/diff/`. Pass means every page has `diff_px=0`. If you intentionally changed a page, look at the difference image and refresh the baseline. A page present on one side only also fails the run.

### Leak cycles

A leak cycle repeats one user-level operation (open and close a page, run a game round, change the sample rate, connect and disconnect a fake sink, and so on) 20 times after a warm-up that lets the glyph and cover caches fill. `make sim-leak` runs 59 independent runs (`tools/simtools/leak_lines.txt`), each covering one area or a group of related pages: screens, tools, vocabulary, themes, the spectrum pages, choosers, alarm and clock pages, settings axes, every game, the instrument app, the external mode's pages, the song tree, the gestures and quick settings, Bluetooth audio pages, and more. `make sim-leak-fp` runs 10 runs on the FakePod simulator (one of them, `fpkeys`, uses a warm-up of 80 cycles because its caches fill more slowly; see [test-results.md](test-results.md)).

A run passes when all of these hold:

- the process exits with code 0;
- stderr has no AddressSanitizer, LeakSanitizer or UBSan output;
- the UI's own leak check prints no `APP ui leak`;
- the LVGL byte count (`lv_bytes`) sampled after each cycle returns to the same value;
- the number of live timers is the same after each cycle.

The last line is `leak_par: 59 runs, -P <jobs>, <seconds> s wall, all ok` (for example `-P 6, 180 s wall`). The checker itself has red-light tests in `sim/tests`: two deliberately leaking actions must make it fail.

A single area can be run by hand: `python3 tools/simtools/leak_cycles.py next:100 --port sim --every 1 --warm 5`.

The same tool can drive a real board over a serial port, which is how board leak loops were run once (see [test-results.md](test-results.md)). Do not run it against a board you have not backed up.

### Other simulator-based tools

- `make lib-stress`: fake libraries of 2,000 to 50,000 tracks through the host harness and the simulator, printing one table of load time, list-page time and memory. Not a gate; times are PC times, not board times.
- `make sim-perf`: relative render time of the heaviest pages before and after a change.
- `tools/check/ui_boxes_check.py`: tap-target size rules over every page.

## 5. Web tool (`make webcheck`)

`tools/web/check.sh` needs Node.js and a headless Chrome. It runs syntax checks, Node tests of the serial connection, flasher, font builder and font writer (`conn_test.mjs`, `flasher_test.mjs`, `fontbuild_test.mjs`, `fontput_test.mjs`), static security checks of the page (a content-security policy without inline scripts, no `eval`, only relative script sources), the release and web-publish script tests, and 13 headless Chrome runs of the real page against a pretend serial port (connect, disconnect, station buttons, list upload, effects import and export, radio search, time zone, moved settings, pause and resume of the shared connection, page languages, the flash flow with its refusals, the font builder, a request at connect time). It prints `PASS` or the reason for the first failure.

## 6. Build matrix and the internal-RAM ceiling

Both boards build from the same sources. The default image on each board includes the vendor Bluetooth codecs (LDAC, aptX HD, aptX) through a patched copy of the Bluedroid component that `make build` generates.

| Target | Image | Notes |
|---|---|---|
| `make build` | round board, default (vendor codecs) | creates `app/sdkconfig`, `app/build` |
| `make build-stock` | round board, stock Bluedroid (SBC, AAC only) | comparison build |
| `make build-fakepod` | FakePod, default (vendor codecs) | `app/build-fakepod`, its own sdkconfig |
| `make build-fakepod-stock` | FakePod, stock Bluedroid | comparison build |
| `make build-peer` | the app on a bare development board with the console on UART0 | bench use only |
| `make build-trace` | round board with heap tracing | for leak hunts, not a gate |

Every build target first checks that the build directory's sdkconfig belongs to the image (`tools/check_sdkconfig.py`).

### Why there is an internal-RAM ceiling

The chip has 512 KB of internal RAM. The Bluetooth controller (about 100 KB), the Wi-Fi driver (about 59 KB), DMA buffers and the stacks take most of it, and the code and static data that must live in internal RAM come first. Everything else (frame buffers, caches, decoder state, effect state, big tables) is placed in the 16 MB of external RAM. `tools/check/diram_budget.txt` records a ceiling, in bytes, for the "DIRAM" image (instruction RAM plus data RAM, as printed by `idf.py size`) of each target, and `make size` / `make size-fakepod` fail when a build exceeds it, printing the largest new symbols. Raising a ceiling means editing the file and writing the reason on the same line.

Current ceilings (all four are at 0 bytes of slack at the time of writing):

| Target | Ceiling (bytes) |
|---|---|
| round, default image | 201,548 |
| FakePod, default image | 194,910 |
| round, stock image | 201,452 |
| FakePod, stock image | 194,814 |

The default image costs 96 bytes more than the stock image, which is the size of the LDAC library's constant tables and a few Bluetooth variables. The rule that follows: new static buffers go to external RAM (`EXT_RAM_BSS_ATTR`); anything that must be internal needs a saving found elsewhere first.

## 7. What is not covered by any automated layer

- Audio quality as heard, Bluetooth with real headsets and phones, USB on real computers, the touch panel's feel, haptics, LEDs, battery behaviour.
- Anything that needs a board: CPU load, real memory use, frame rates, underruns, boot behaviour. These were measured by hand; see [test-results.md](test-results.md).
- `make test` cannot see timing of the audio task; `make sim-test` has virtual time. Neither says whether a real SD card is fast enough. The card diagnostics page on the device measures that.

## 8. Before you send a change

1. `FAST=1 make test` (and `make test` once, for the sanitizers, if you touched `components/player_core`).
2. `make lint` if you touched `components/player_core`; `make lint-app` if you touched `app/` or the UI.
3. `make sim-test`; add a piece or a case in `sim/tests/` for a new behaviour.
4. If you touched a page's look: `make sim-base` before, `make sim-diff BASE=.local/sim/base` after. The difference must be 0 unless the look change was the point.
5. If you added a page, game, tool or dialog: add its cycle to `tools/simtools/leak_lines.txt` and run `make sim-leak`.
6. If you added a settings key: add it to `tools/check/cfg_keys.txt`. If you added a user-visible string: the string tables are generated, see `AGENTS.md`.
7. `make build` and `make build-fakepod`, then `make size` and `make size-fakepod`. A new internal-RAM static needs a reason or a saving.
8. `make webcheck` if you touched `tools/web/`.
9. `python3 tools/check/check_paths.py` if you touched documents or moved files.
10. Do not reformat or restyle vendored libraries (listed in `AGENTS.md`); a needed fix is small and recorded in that library's `patches/` or `UPSTREAM.txt`.
