# AGENTS.md

JukeDial: firmware for an ESP32-S31 music player (360x360 round board with a knob; second target Fake Pod Nano Pro ("FakePod" in the code and docs), 460x460 square, two keys). ESP-IDF v6.1, LVGL 9.5, Bluedroid. One code base, two boards.
User docs: `README.md`, `docs/guide/`. Developer notes: `docs/dev/`.
What is tested and what was measured: `docs/dev/testing.md`, `docs/dev/test-results.md`; feature map: `docs/dev/features.md`.

## Directory map
- `app/` firmware application (`app/main` UI, audio glue, drivers; `app/partitions.csv`)
- `components/` libraries: `player_core` (portable player logic with host tests), codecs (`libldac`, `libxmp`, `gme` ...), board support
- `sim/` host simulator (SDL2) of both boards and its tests and screenshots
- `tools/` scripts: `check/` lint checks, `gen/` generators, `simtools/` simulator helpers, `bench/`, `board/` serial-port helpers, `fonts/` font tool, `web/` browser tool
- `docs/guide/` illustrated user guide (en, zh-TW, zh-CN); `docs/dev/` how-tos for contributors
- `fonts/` the four built UI fonts (tracked); `fonts-src/` the original Noto and icon sources they are built from

## Build, test, simulate
- `make help` lists everything. Firmware: `make build` (round board), `make build-fakepod`; ESP-IDF v6.1 must be installed (the Makefile sources it; do not activate it in your shell first).
- Host tests: `make test` (`FAST=1 make test` skips the sanitizers). Lint: `make lint`, `make lint-app`, `make webcheck`.
- Simulator: `make sim-run` (round) / `make sim-run-fp` (FakePod); `make sim-test`, `make sim-diff` (screenshot comparison; the diff must be 0 unless you changed the look).

## Rules that matter
- Never flash a board without a backup of its flash (`make backup`). Set your serial port once in `.local/board.mk` (`PORT=/dev/serial/by-id/...`); always use by-id paths, never a bare `ttyACM`. Do not flash, erase or write to a serial port unless the user asked.
- Vendored code is not reformatted or restyled, so it can be diffed against upstream: `components/{emu2413,gme,libadplug,libcrsid,libfreeaptx,libhvl,libldac,libmpcdec,libtta,libxmp}`, `components/player_core/third_party/`, `app/main/third_party/`, `app/components/esp_tinyusb`, `tools/web/vendor/`, `tools/fatfs_exfat`. A needed fix is small and recorded in that library's `patches/` or `UPSTREAM.txt`.
- Generated files are regenerated, never hand-edited: `app/main/ui/icons_gen.h` (`tools/gen/make_icons.py --header`), the i18n tables (`tools/gen/i18n_gen.py`), the theme template (`tools/gen/gen_theme_template.py`), the FM tables (`tools/gen/gen_fmopl_tables.py`), `tools/web/` generated files (`tools/gen/web_*_gen.py`), the fonts in `fonts/` (`make fonts-all`). `make gen-check` finds stale ones.
- Credentials (Wi-Fi, tokens) live in `.local/` and on the card; never read, print or commit them.

## Common tasks
1. **Add missing characters to the fonts:** `python tools/fonts/add_glyphs.py --text "your text"` (also `--file`, `--scan <music folder>`, `--miss <fontmiss output>`); then copy `out/fonts/ui-*.ttf` to the card folder `_player/fonts/` and reboot. Details, limits and the rebuild: `docs/dev/fonts.md`.
2. Run the player without hardware: `make sim-run`.
3. Change a UI string or language: the i18n sources in `tools/gen/i18n/`, then `python tools/gen/i18n_gen.py`; new characters may need fonts (task 1; `make fonts-check`).
4. Check a change before sending it: `FAST=1 make test && make lint && make lint-app && make sim-test && make sim-diff`.
