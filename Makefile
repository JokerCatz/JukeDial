# SPDX-License-Identifier: LGPL-2.1-or-later
# JC3636F118 (ESP32-S31) music player. `make` lists the targets.
# The board is addressed by its by-id path only (another ESP32 may be plugged in; ttyACM numbers swap).
SHELL := /bin/bash
# The boards' serial ports are yours: .local/board.mk (untracked; copy .local/board.mk.example) sets PORT and PEER_ID, or pass PORT=/dev/serial/by-id/... on the command line.
# No default: check-port stops with a message when PORT is empty, so nothing is ever written to a port nobody named.
-include .local/board.mk
PORT ?=
SECS ?= 60
# the peer bench board's console bridge (see the repository rules): check-port refuses a peer target on any other port, and this port for a non-peer target
PEER_ID ?=
IDF_ENV := $(HOME)/.espressif/tools/activate_idf_v6.1.sh
# the patched FatFs (exFAT) is generated before every idf.py run: a no-op when nothing changed (tools/fatfs_exfat)
FATFS := bash tools/fatfs_exfat/make_fatfs.sh
# ccache for every IDF build: IDF_CCACHE_ENABLE puts it in front of the compiler; compiler_check=content because IDF regenerates build/specs/picolibc.specs
# (same bytes, new mtime) on every configure and ccache's default mtime check hashed that mtime into every key = 0 hits on a repeat cold build
CCACHE_ENV := export IDF_CCACHE_ENABLE=1 CCACHE_COMPILERCHECK=content
IDF := $(FATFS) && $(CCACHE_ENV) && source $(IDF_ENV) >/dev/null && cd app && idf.py --preview
PY := $(firstword $(wildcard $(HOME)/.espressif/python_env/*/bin/python))

.PHONY: export export-check icons-header icons-build icons-sheet check-sdkconfig bt-ldac build-stock size-stock build-ldac flash-ldac flash-stock build-fakepod size-fakepod build-fakepod-stock size-fakepod-stock noise-pack noise-pack-test card-zip faces-zip splash fonts-build sim sim-run sim-test sim-shots sim-base sim-diff sim-leak sim-root build-trace flash-trace build-ldac flash-ldac webcheck web-publish release help build flash crash flash-all fontpak flash-fonts fonts-get size menuconfig clean test lint lint-app log log-reset monitor web ctl status backup check-port sim-fp sim-leak-fp sim-base-fp sim-diff-fp sim-run-fp sim-test-fp lib-stress refactor-base refactor-diff check-paths build-peer flash-peer flash-all-peer sim-perf gen-check lint-app-checks notices-check spdx-check fonts-all fonts-check fonts-venv

help:
	@echo "make build          build the app (app/): the default image with the vendor codecs (LDAC, aptX HD, aptX; patched Bluedroid), app/build + app/sdkconfig; make build-stock = stock Bluedroid (comparison, app/build-stock; flash-stock / size-stock)"
	@echo "make flash          build + write the app partition (needs a flash backup in backup/)"
	@echo "make flash-all      bootloader + partition table + app"
	@echo "make build-ldac     alias of build (and flash-ldac of flash) for one release"
	@echo "make build-fakepod  the FakePod image with the vendor codecs (ESP32-S31, 460x460 AMOLED, two keys, IMU; components/fakepod_bsp; app/build-fakepod, own sdkconfig); no flash target yet; size-fakepod prints its memory use; build-fakepod-stock / size-fakepod-stock = stock Bluedroid"
	@echo "make build-trace     on-demand diagnostic: heap-tracing build (CONFIG_HEAP_TRACING_STANDALONE, app/build-trace) for leak hunts; flash-trace writes it"
	@echo "make flash-fonts    partition table + the font pack (fonts/ui-tc.ttf ui-sc.ttf ui-ext.ttf) into the flash 'fonts' partition"
	@echo "make fonts-all           build ui-tc / ui-sc / ui-ext / ui-icon from fonts-src/ into FONTS_OUT (.local/fontsets/out; never writes fonts/) and run the glyph + partition-size checks; make fonts-check: a fresh build must equal fonts/*.ttf; make fonts-venv: the Python environment (docs/dev/fonts.md)"
	@echo "make icons-header | icons-build | icons-sheet   icons as text: icons_gen.h from app/main/ui/icons.def; the icon font ui-icon.ttf (FONT_PY with fontTools+brotli, pinned Tabler + Material Symbols) into FONTS_OUT; the owner's icon maps (ICON_SHEETS=dir, FONT_PY with Pillow)"
	@echo "make splash         the default power-on pictures (one JPEG per boot mode, icons on black, 360x360 = the round panel, artwork up to ~14 px from the circle edge) -> .local/splash/ (Pillow + fonts/ui-icon.ttf; never in git); make card-zip puts them in _player/splash/"
	@echo "make fonts-get      copy /sdcard/_player/fonts/ui*.ttf from the board's card into fonts/ (for flash-fonts)"
	@echo "make noise-pack     our own synthesised noise scene pack -> .local/library_free.muk (CC0, pure Python, deterministic; never in git) + the reader/decoder test; make card-zip puts it in _player/sleep/library.muk"
	@echo "make test           host tests with ASan/LeakSanitizer/UBSan (FAST=1 make test: without)"
	@echo "make lint           gcc -fanalyzer over components/player_core (host build, no sanitizers); any warning fails"
	@echo "make lint-app       tools/check/ui_lint.py (pages may not hand-build UI), tools/check/check_help.py (help texts), tools/check/check_games.py (the game registry), then gcc -fanalyzer over app/main (cross-compile only, ~1 min, app/build-lint); lists every analyzer warning, any one fails"
	@echo "make gen-check       the staleness checks of the committed generated files (fmopl tables, theme template, icons_gen.h, i18n tables); every flash target runs it first, lint-app too"
	@echo "make refactor-base   refactor safety net, step 1 (main checkout, after building everything): snapshot the built ELFs (app/build, -stock, -peer, -fakepod, -fakepod-stock) and simulator binaries (symbols, sections) plus the console-command, settings-key, make -n and source-list inventories into REFACTOR_BASE (default .local/refactor-base of the MAIN checkout); builds nothing, names missing / stale ELFs (tools/refactor_snapshot.py)"
	@echo "make refactor-diff   safety net, step 2 (after each refactor step, rebuild first): snapshot this tree and compare with the base: every function (name + size) must be identical, data/rodata/bss are informational, inventories exact; REFACTOR_ARGS='--allow sources --max-data-delta 4096'; fails on a missing or stale ELF"
	@echo "make check-paths     tools/check/check_paths.py: every docs/ tools/ app/ components/ sim/ path a tracked text file mentions must exist (ratchet: tools/check/paths_known_missing.txt; lint-app runs it too)"
	@echo "make sim            desktop simulator (host build of LVGL + the UI, sim/); sim-test runs its tests"
	@echo "make sim-run [ARGS=...]   the simulator in a window on .local/sim/play (seeded once from .local/sim_root if it exists, else sim/fixture; delete it to re-seed)"
	@echo "make sim-shots      every screen of tools/simtools/ui_baseline.py as PNG into .local/sim/shots (fixture data, fixed clock and seed), the player's, the instrument app's (sim --mode inst, inst_*) and the USB drive mode's (sim --mode usb, usb_*)"
	@echo "make sim-base       the simulator's own baseline (fixture data, fixed clock and seed) into .local/sim/base, the player, the instrument app and the USB drive mode: the refactor net"
	@echo "make sim-diff [BASE=dir]   the simulator's screens (the player, the instrument app: sim --mode inst, the USB drive mode: sim --mode ext) against the board's baseline (.local/ui_kit/base): diff_px and bbox per page, images in .local/sim/diff; then against .local/sim/base (every page identical) when it exists"
	@echo "make sim-leak       screens/tools/vocab/theme/vizpage/vizkinds/vizring_leave/vizplay/vizown/choosers/alarmpage/axisrows/clockfaceaxis/albumaxis/pickers/setaxis/setrows/timeset/msgpage/homering/popring/explain/status/helppages/fxpages/kitpage/scene/toys/bubbles/games/arena/dial/tower/spiro/deflect/life/dominoes/guess/covers/sfxbank/cardfmt/btmeta the instrument app's inst_pages/inst_rings (sim --mode inst) and the USB mode's usb_ring (sim --mode usb), 20 cycles each, in the simulator under ASan/LSan (lv_bytes must come back to the same value)"
	@echo "make sim-fp          the square FakePod simulator (460x460, no knob, keys Up/Down or simkey top|bottom; sim/build-fp); sim-test-fp, sim-base-fp / sim-diff-fp (.local/sim/base_fp, fp_sheet.png), sim-leak-fp, sim-run-fp"
	@echo "make lib-stress     library stress test: fake indexes 2k-50k tracks, host harness + simulator, one table (not a gate)"
	@echo "make sim-test       tools/check/check_help.py (help texts), then the simulator's self test and sim/test.sh (the checks are the pieces in sim/tests/; every piece is its own process and they run min(CPUs, 8) at a time; SIM_TEST_JOBS=1 = one process, one piece after the other, SIM_TEST_JOBS=N = N at a time; ONLY=viz[,clock] runs just the matching pieces, in one process)"
	@echo "make sim-root       settings, index, lists, fonts of the board's TF card into .local/sim_root (uses the serial port; never wifi.txt)"
	@echo "make log [SECS=60]  read the console without resetting the board"
	@echo "make log-reset      reset the board, then read the console"
	@echo "make monitor        idf.py monitor (Ctrl+] quits)"
	@echo "make crash          console crashlog, then the last crash line's pc / ra through addr2line (app/build/*.elf)"
	@echo "make web            settings page on http://localhost:8000/ (Chrome/Edge, Web Serial)"
	@echo "make webcheck       check the page without a board (pretend serial port, headless Chrome)"
	@echo "make web-publish    build dist/ = the flashing/settings page only (it hosts no firmware); WEB_PUBLISH_ARGS='--fonts fonts --noto DIR'"
	@echo "make release        local release (no upload): build both boards -> release/<version>/ (one merged image per board, card pack, SHA256SUMS, notes); NOTES=notes.json"
	@echo "make status         one status request over the settings protocol"
	@echo "make ctl CMD='{\"op\":\"stations_get\"}'   any settings-protocol request"
	@echo "make size / menuconfig / clean"
	@echo "make backup         full 16 MB flash backup (never overwrites an existing one)"
	@echo "PORT=$(PORT)"

# 2026-11-22: the vendor-codec image (LDAC, aptX HD, aptX; patched Bluedroid from tools/bt_ldac, -DJC_LDAC=1, app/sdkconfig.ldac) is THE default on both boards.
# Layout: the build dirs keep their names and change meaning: app/build (+ app/sdkconfig) = round + LDAC, app/build-fakepod = FakePod + LDAC; the stock-Bluedroid
# images (comparison only) are app/build-stock and app/build-fakepod-stock. A build dir made before the switch is stock: tools/check_sdkconfig.py then fails with
# "delete <file> to regenerate" (the LDAC fragment pins CONFIG_BT_A2DP_SEP_NUM_MAX=5, a stock sdkconfig has 2). Firmware says which it is: console `codecs`, status "codecs".
BASE_DEFS := sdkconfig.defaults;sdkconfig.defaults.bt
LDAC_DEFS := -DJC_LDAC=1 "-DSDKCONFIG_DEFAULTS=$(BASE_DEFS);sdkconfig.ldac"
# the patched bt component copy (.local/bt_ldac): made when missing, and again when ldac.patch, make_bt.sh or the IDF checkout changed (stamp, tools/bt_ldac/make_bt.sh --if-stale)
bt-ldac:
	@tools/bt_ldac/make_bt.sh --if-stale

# the components collect their sources with file(GLOB): a new or removed file needs a reconfigure (tools/src_stamp.sh stamps the file list per build dir)
build: bt-ldac
	@test ! -L app/sdkconfig || { echo "STOP: app/sdkconfig is a symlink (tools/worktree_setup.sh links the main checkout's): rm app/sdkconfig, the build writes its own"; exit 1; }
	@$(MAKE) --no-print-directory check-sdkconfig CHECK=build
	@tools/src_stamp.sh app/build '($(IDF) $(LDAC_DEFS) reconfigure >/dev/null)'
	$(IDF) $(LDAC_DEFS) build

# stock Bluedroid (what `make build` was before 2026-11-22): the comparison image, app/build-stock
STOCK_B := -B build-stock -DSDKCONFIG=build-stock/sdkconfig "-DSDKCONFIG_DEFAULTS=$(BASE_DEFS)"
build-stock:
	@$(MAKE) --no-print-directory check-sdkconfig CHECK=build-stock
	@tools/src_stamp.sh app/build-stock '($(IDF) $(STOCK_B) reconfigure >/dev/null)'
	$(IDF) $(STOCK_B) build

# never flash an unknown port or a board without a backup (back up before flashing)
check-port:
	@test -n "$(PORT)" || { echo "STOP: PORT is not set. Create .local/board.mk (copy .local/board.mk.example and put your board's /dev/serial/by-id path in it) or run: make $(MAKECMDGOALS) PORT=/dev/serial/by-id/usb-..."; exit 1; }
	@case "$(PORT)" in /dev/serial/by-id/*) ;; *) echo "STOP: PORT must be a /dev/serial/by-id path"; exit 1;; esac
	@case "$(MAKECMDGOALS)" in *peer*) test -n "$(PEER_ID)" || { echo "STOP: a peer target needs PEER_ID (the peer board's by-id name) in .local/board.mk, and PORT set to the peer's by-id path"; exit 1; };; esac
	@test -z "$(PEER_ID)" || case "$(PORT)" in *$(PEER_ID)*) case "$(MAKECMDGOALS)" in *peer*) ;; *) echo "STOP: PORT is the peer bench board ($(PEER_ID)) but the target is not a peer target"; exit 1;; esac;; esac
	@case "$(MAKECMDGOALS)" in *peer*) case "$(PORT)" in *$(PEER_ID)*) ;; *) echo "STOP: a peer target needs the peer's port: make $(MAKECMDGOALS) PORT=/dev/serial/by-id/usb-$(PEER_ID)-if00 (a bare flash-peer once wrote the peer image to the main board)"; exit 1;; esac;; esac
	@test -e "$(PORT)" || { echo "STOP: $(PORT) is not connected"; exit 1; }
	@ls backup/factory_full_*.bin >/dev/null 2>&1 || { echo "STOP: no backup/factory_full_*.bin - run 'make backup' first"; exit 1; }

flash: gen-check check-port build
	$(IDF) -p $(PORT) app-flash

flash-all: gen-check check-port build
	$(IDF) -p $(PORT) flash

flash-stock: gen-check check-port build-stock
	$(IDF) $(STOCK_B) -p $(PORT) app-flash

# A generated sdkconfig keeps its old values when sdkconfig.defaults changes (a Kconfig choice like MBEDTLS_EXTERNAL_MEM_ALLOC silently stays at the old one):
# every player build checks its own against the pinned keys first and fails with "delete <file> to regenerate" (tools/check_sdkconfig.py; lint-app checks all of them)
check-sdkconfig:
	@python3 tools/check_sdkconfig.py $(CHECK)

# build-ldac / flash-ldac: the old names of the default image, kept for one release (habits, old docs)
build-ldac:
	@echo "note: build-ldac is now the default image: running 'make build' (app/build); this alias goes after one release"
	@$(MAKE) --no-print-directory build
flash-ldac:
	@echo "note: flash-ldac is now the default: running 'make flash' (app/build); this alias goes after one release"
	@$(MAKE) --no-print-directory flash

# Peer bench board with the console on UART0 (the 1a86 bridge): own build dir and sdkconfig (fragment app/sdkconfig.peer_uart on top of the
# defaults), so the normal build and `make flash` stay as they are. PORT must be the peer's by-id path.
PEER_B := -B build-peer -DSDKCONFIG=build-peer/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.bt;sdkconfig.peer_uart"
build-peer:
	@$(MAKE) --no-print-directory check-sdkconfig CHECK=build-peer
	@tools/src_stamp.sh app/build-peer '($(IDF) $(PEER_B) reconfigure >/dev/null)'
	$(IDF) $(PEER_B) build

flash-peer: gen-check check-port build-peer
	$(IDF) $(PEER_B) -p $(PORT) app-flash

flash-all-peer: gen-check check-port build-peer
	$(IDF) $(PEER_B) -p $(PORT) flash

# FakePod (second target, same sources): own build dir and sdkconfig (the defaults + app/sdkconfig.fakepod + app/sdkconfig.ldac), -DJC_FAKEPOD=1 picks components/fakepod_bsp, -DJC_LDAC=1
# the vendor codecs (default since 2026-11-22). No flash target: it needs the owner and the board in hand (the fakepod design notes, manual esptool), and the round board's `make flash` must never take this image.
FAKEPOD_B := -B build-fakepod -DSDKCONFIG=build-fakepod/sdkconfig "-DSDKCONFIG_DEFAULTS=$(BASE_DEFS);sdkconfig.fakepod;sdkconfig.ldac" -DJC_FAKEPOD=1 -DJC_LDAC=1
build-fakepod: bt-ldac
	@$(MAKE) --no-print-directory check-sdkconfig CHECK=build-fakepod
	@tools/src_stamp.sh app/build-fakepod '(export JC_FAKEPOD=1; $(IDF) $(FAKEPOD_B) reconfigure >/dev/null)'
	export JC_FAKEPOD=1; $(IDF) $(FAKEPOD_B) build
	python3 tools/check/check_fakepod_macro.py --require

size-fakepod:
	export JC_FAKEPOD=1; $(IDF) $(FAKEPOD_B) size
	python3 tools/check/check_diram.py fakepod
	python3 tools/check/check_flash_park.py fakepod

# stock Bluedroid FakePod (comparison only): app/build-fakepod-stock
FAKEPOD_STOCK_B := -B build-fakepod-stock -DSDKCONFIG=build-fakepod-stock/sdkconfig "-DSDKCONFIG_DEFAULTS=$(BASE_DEFS);sdkconfig.fakepod" -DJC_FAKEPOD=1
build-fakepod-stock:
	@$(MAKE) --no-print-directory check-sdkconfig CHECK=build-fakepod-stock
	@tools/src_stamp.sh app/build-fakepod-stock '(export JC_FAKEPOD=1; $(IDF) $(FAKEPOD_STOCK_B) reconfigure >/dev/null)'
	export JC_FAKEPOD=1; $(IDF) $(FAKEPOD_STOCK_B) build
	python3 tools/check/check_fakepod_macro.py --require --db app/build-fakepod-stock/compile_commands.json

size-fakepod-stock:
	export JC_FAKEPOD=1; $(IDF) $(FAKEPOD_STOCK_B) size
	python3 tools/check/check_diram.py fakepod-stock

# On-demand diagnostic (not part of the normal build set, rebuilds fine: checked 2026-11-19). Heap tracing with callers (frame pointers; leak hunts, tools/simtools/leak_cycles.py): own build dir and sdkconfig (the defaults + app/sdkconfig.trace: standalone
# tracing, a fragment, no copy), console "heaptrace start|stop|dump". The normal build's sdkconfig stays as it is.
TRACE_B := -B build-trace -DSDKCONFIG=build-trace/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.bt;sdkconfig.trace"
build-trace:
	@$(MAKE) --no-print-directory check-sdkconfig CHECK=build-trace
	@tools/src_stamp.sh app/build-trace '($(IDF) $(TRACE_B) reconfigure >/dev/null)'
	$(IDF) $(TRACE_B) app          # app only: the frame-pointer bootloader would not fit (only the app is flashed)

flash-trace: gen-check check-port build-trace
	$(IDF) $(TRACE_B) -p $(PORT) app-flash

# CJK UI font copy in flash (spec an internal note). fonts/*.ttf are in git (built from fonts-src/, see below).
FONTS_DIR ?= fonts
FONT_FILES := $(FONTS_DIR)/ui-tc.ttf $(FONTS_DIR)/ui-sc.ttf $(FONTS_DIR)/ui-ext.ttf $(FONTS_DIR)/ui-icon.ttf
FONTS_PART_SIZE := $(shell awk -F, '$$1 ~ /^fonts/ {gsub(/ /,"",$$5); print $$5}' app/partitions.csv | sed 's/M$$/*1048576/' | bc)
# icons as text (ICON-A, app/main/ui/icons.def): the header needs nothing; the font and the sheets need fontTools / Pillow (FONT_PY) and the pinned upstream files (ICON_SRC = fonts-src/icons, tracked)
ICON_SRC ?= fonts-src/icons
icons-header:
	python3 tools/gen/make_icons.py --header
icons-build:
	@$(FONT_PY) -c 'import fontTools, brotli' 2>/dev/null || { echo "STOP: FONT_PY=$(FONT_PY) has no fontTools/brotli. Install once into a venv (never into $$HOME/.espressif):  python3 -m venv .local/iconvenv && .local/iconvenv/bin/pip install fonttools brotli pillow ; then  make icons-build FONT_PY=.local/iconvenv/bin/python  (docs/dev/fonts.md)"; exit 1; }
	$(FONT_PY) tools/gen/make_icons.py --font $(FONTS_OUT)/ui-icon.ttf --src $(ICON_SRC)
icons-sheet:
	$(FONT_PY) tools/gen/make_icons.py --sheet $(ICON_SHEETS) --font-file $(FONTS_DIR)/ui-icon.ttf --text-font $(FONTS_DIR)/ui-tc.ttf
fontpak:
	@for f in $(FONT_FILES); do test -f $$f || { echo "STOP: $$f missing ('make fonts-get' copies them from the card)"; exit 1; }; done
	@mkdir -p app/build
	$(PY) tools/gen/make_fontpak.py app/build/fonts.bin $(FONTS_PART_SIZE) $(FONT_FILES)

# esptool through IDF's own flash target (same tool, chip and flash args as app-flash); ESPPORT is always the by-id path
# One retry: right after another flash the 1a86 bridge (peer) can miss the reset into download mode; the first try then fails (make stops with an error)
# and the board keeps its OLD font pack (2026-11-17: it booted with 3 fonts, the run's error was hidden by `| tail`). Writing the same image twice is safe.
flash-fonts: gen-check check-port build fontpak
	source $(IDF_ENV) >/dev/null && cd app/build && { ESPPORT=$(PORT) cmake --build . --target fonts-flash || { echo "flash-fonts: first try failed, retrying in 3 s"; sleep 3; ESPPORT=$(PORT) cmake --build . --target fonts-flash; }; }

# The four UI fonts, all built from the original fonts in fonts-src/ (README there: upstream, version, SHA-256, licence), no arguments needed:
#   make fonts-all [FONT_PY=<python with fontTools>]     -> FONTS_OUT (never straight into fonts/); then: cp $(FONTS_OUT)/ui-*.ttf fonts/
#   make fonts-check                                     -> a fresh build must equal the tracked fonts/*.ttf (byte for byte: the tools are deterministic)
#   make fonts-venv                                      -> the Python environment (tools/requirements-fonts.txt) in .local/fontvenv
# Steps: ui-tc / ui-sc (make_font_subset.py), ui-ext (make_font_fallback.py, list tools/gen/font_sets/ext.txt), ui-icon (make_icons.py, icons.def); then every app/main literal
# must have a glyph (no known-missing list) and the four files must fit the partition with FONT_FREE_MIN bytes to spare. The old overrides FONT_SRC_TC= / FONT_SRC_SC= still work.
FONT_PY ?= $(firstword $(wildcard .local/fontvenv/bin/python .local/iconvenv/bin/python) python3)
FONTS_OUT ?= .local/fontsets/out
FONT_GB_LEVEL2 ?= 1
FONT_FREE_MIN ?= 204800
FONT_SRC ?= fonts-src
FONT_SRC_TC ?= $(FONT_SRC)/noto-cjk/NotoSansTC-Regular.otf
FONT_SRC_SC ?= $(FONT_SRC)/noto-cjk/NotoSansSC-Regular.otf
FONT_SRC_EXTRA ?= $(FONT_SRC)/noto-extra
FONT_SRC_ICONS ?= $(FONT_SRC)/icons
fonts-venv:
	python3 -m venv .local/fontvenv
	.local/fontvenv/bin/pip install -r tools/requirements-fonts.txt
fonts-build fonts-all:
	@test -f "$(FONT_SRC_TC)" || { echo "STOP: $(FONT_SRC_TC) missing (Noto Sans TC 2.004 Regular OTF, OFL: fonts-src/README.md)"; exit 1; }
	@test -f "$(FONT_SRC_SC)" || { echo "STOP: $(FONT_SRC_SC) missing (Noto Sans SC 2.004 Regular OTF, OFL: fonts-src/README.md)"; exit 1; }
	@$(FONT_PY) -c 'import fontTools, brotli' 2>/dev/null || { echo "STOP: FONT_PY=$(FONT_PY) has no fontTools/brotli: make fonts-venv   (or pip install -r tools/requirements-fonts.txt into a venv of your own)"; exit 1; }
	@mkdir -p $(FONTS_OUT)
	FONT_GB_LEVEL2=$(FONT_GB_LEVEL2) $(FONT_PY) -I tools/gen/make_font_subset.py "$(FONT_SRC_TC)" $(FONTS_OUT)/ui-tc.ttf "$(FONT_SRC_SC)" $(FONTS_OUT)/ui-sc.ttf
	$(FONT_PY) -I tools/gen/make_font_fallback.py --tc "$(FONT_SRC_TC)" --sc "$(FONT_SRC_SC)" --extra "$(FONT_SRC_EXTRA)" --out $(FONTS_OUT)/ui-ext.ttf
	$(FONT_PY) -I tools/gen/make_icons.py --font $(FONTS_OUT)/ui-icon.ttf --src "$(FONT_SRC_ICONS)"
	python3 tools/check/check_font_glyphs.py $(FONTS_OUT) /dev/null
	python3 tools/gen/make_fontpak.py $(FONTS_OUT)/fonts.bin $$(( $(FONTS_PART_SIZE) - $(FONT_FREE_MIN) )) $(FONTS_OUT)/ui-tc.ttf $(FONTS_OUT)/ui-sc.ttf $(FONTS_OUT)/ui-ext.ttf $(FONTS_OUT)/ui-icon.ttf
	@rm -f $(FONTS_OUT)/fonts.bin
fonts-check:
	@$(MAKE) --no-print-directory fonts-build FONTS_OUT=.local/fontsets/check
	python3 tools/check/check_fonts_fresh.py .local/fontsets/check $(FONTS_DIR)
	$(FONT_PY) -I tools/fonts/test_add_glyphs.py

fonts-get:
	@case "$(PORT)" in /dev/serial/by-id/*) ;; *) echo "STOP: PORT must be a /dev/serial/by-id path"; exit 1;; esac
	@mkdir -p $(FONTS_DIR)
	$(PY) tools/board/tf_get.py $(PORT) /sdcard/_player/fonts/ui-tc.ttf $(FONTS_DIR)/ui-tc.ttf /sdcard/_player/fonts/ui-sc.ttf $(FONTS_DIR)/ui-sc.ttf /sdcard/_player/fonts/ui-ext.ttf $(FONTS_DIR)/ui-ext.ttf

size:
	$(IDF) size
	python3 tools/check/check_diram.py round          # the DIRAM ceiling (tools/check/diram_budget.txt); fails with the new .bss/.data symbols when over
	python3 tools/check/check_flash_park.py round     # the parked core's predictor hook (core_park.c) and no direct IRAM -> PSRAM-BSS reference (psram-hunt 2026-11-19)

size-stock:
	$(IDF) $(STOCK_B) size
	python3 tools/check/check_diram.py round-stock

menuconfig:
	$(IDF) menuconfig

clean:
	$(IDF) clean

# FXREF: the fx-lab reference renders, outside git (default: .local/fxref of the main checkout; FXREF=/x overrides)
FXREF ?= $(shell d=$$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null) && echo "$$(dirname "$$d")/.local/fxref")
export FXREF
test:
	bash tools/host_test.sh

lint:
	bash tools/check/host_lint.sh

# not part of `make lint`: cross-compiles app/main with -fanalyzer (tools/check/app_lint.sh)

# The checks that catch a stale generated file (committed: fmopl tables, the theme template, icons_gen.h, the i18n tables): `make gen-check`, a prerequisite of every
# flash target (before check-port and before anything is built or written), and part of lint-app. One definition for both.
define GEN_CHECK_CMDS
python3 tools/gen/gen_fmopl_tables.py --check
python3 tools/gen/gen_theme_template.py --check
python3 tools/gen/make_icons.py --check-header
python3 tools/gen/i18n_gen.py --strict
python3 tools/gen/web_fontsets_gen.py --check
endef
gen-check:
	$(GEN_CHECK_CMDS)

# SUBMAKE is $(MAKE) hidden from make's own recursion detection: `make -n lint-app` must only print the recipe, not run app_lint.sh (the refactor net runs make -n).
SUBMAKE := $(MAKE)
# the slow half (cross-compile with -fanalyzer, ~1 min) runs in the background while lint-app-checks (the python / shell checks) runs in front;
# its output is buffered and printed when both are done, so the last line is app_lint.sh's own "lint-app: ok (N files)"
# THIRD_PARTY_NOTICES.md is generated (tools/gen/third_party_notices.py); the check also fails for a vendored folder or managed component the table lacks
notices-check:
	python3 tools/gen/third_party_notices.py --check
# every own source file starts with an SPDX line (tools/release/spdx_headers.py writes them; vendored and generated trees are skipped)
spdx-check:
	python3 tools/release/spdx_headers.py --check

lint-app-checks:
	python3 tools/gen/third_party_notices.py --check
	python3 tools/release/spdx_headers.py --check
	python3 tools/check/check_chiptest_untracked.py --selftest
	python3 tools/check/check_chiptest_untracked.py
	python3 tools/check/check_fakepod_macro.py --selftest
	python3 tools/check/check_diram.py --selftest
	python3 tools/check/check_flash_park.py --selftest
	python3 tools/check/check_safe_mode.py --selftest && python3 tools/check/check_safe_mode.py
	python3 tools/check/check_fakepod_macro.py
	python3 tools/check_sdkconfig.py
	$(GEN_CHECK_CMDS)
	python3 tools/check/check_card_paths.py
	python3 tools/check/check_zip_index.py
	python3 tools/check/check_vendor.py --selftest && python3 tools/check/check_vendor.py
	python3 tools/check/check_paths.py --selftest
	python3 tools/check/check_paths.py
	python3 tools/check/check_cfg_keys.py --selftest && python3 tools/check/check_cfg_keys.py
	python3 tools/check/check_flash_writes.py --selftest
	python3 tools/check/check_flash_writes.py
	python3 tools/check/ui_lint.py
	python3 tools/check/ui_colour_lint.py
	python3 tools/gen/gen_card_themes.py --check
	python3 tools/check/check_palette.py
	@python3 tools/check/check_palette.py --legacy >/dev/null; test $$? -eq 1 || { echo 'check_palette --legacy passed: its rows cannot see the old ramps'; exit 1; }
	python3 tools/check/check_help.py
	python3 tools/check/check_games.py
	python3 tools/check/check_icons.py
	bash tools/check/ext_audio_guard.sh
	$(PY) tools/check/check_font_glyphs.py

lint-app:
	@log=$$(mktemp); trap 'rm -f $$log' EXIT; \
	( $(FATFS) && $(CCACHE_ENV) && source $(IDF_ENV) >/dev/null && bash tools/check/app_lint.sh ) >$$log 2>&1 & pid=$$!; \
	trap 'kill $$pid 2>/dev/null' INT TERM; \
	rc=0; $(SUBMAKE) --no-print-directory lint-app-checks || rc=1; \
	wait $$pid || rc=1; cat $$log; \
	if [ $$rc -ne 0 ]; then echo "lint-app: FAILED (see the output above; the python / shell checks or app_lint.sh)"; fi; exit $$rc

# Refactor safety net (tools/refactor_snapshot.py; the base design notes, 重構防護網). Neither target builds anything and neither
# recipe may use $(MAKE): `make -n` (the dry-run inventory) would still run such a line.
# REFACTOR_BASE: the main checkout's .local, so a worktree compares against the baseline taken in the main directory.
REFACTOR_BASE ?= $(shell d=$$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null) && echo "$$(dirname "$$d")/.local/refactor-base")
REFACTOR_ARGS ?=
refactor-base:
	@test -n "$(REFACTOR_BASE)" || { echo "refactor-base: cannot resolve the main checkout (set REFACTOR_BASE=dir)"; exit 1; }
	python3 tools/refactor_snapshot.py --selftest
	rm -rf "$(REFACTOR_BASE)"
	python3 tools/refactor_snapshot.py snap --out "$(REFACTOR_BASE)"

refactor-diff:
	@test -f "$(REFACTOR_BASE)/meta.json" || { echo "refactor-diff: no baseline at $(REFACTOR_BASE) (run make refactor-base in the main checkout first)"; exit 1; }
	@t=$$(mktemp -d) && trap 'rm -rf "$$t"' EXIT && \
	python3 tools/refactor_snapshot.py snap --out "$$t" && \
	python3 tools/refactor_snapshot.py diff --require-fresh $(REFACTOR_ARGS) "$(REFACTOR_BASE)" "$$t"

check-paths:
	python3 tools/check/check_vendor.py --selftest && python3 tools/check/check_vendor.py
	python3 tools/check/check_paths.py --selftest
	python3 tools/check/check_paths.py

sim:
	@test -f .local/faces.zip || $(MAKE) --no-print-directory faces-zip || echo "NOTICE: no .local/faces.zip (make faces-zip needs Pillow + Noto CJK + IBM Plex): the simulator runs without the HUD faces plug-in"
	@test -f app/sdkconfig || { echo "先跑一次 make build (app/sdkconfig is missing)"; exit 1; }
	python3 tools/simtools/sim_lvconf.py app/sdkconfig sim/build/lv_conf_sim.h
	cmake -S sim -B sim/build -DSIM_LVCONF=$(CURDIR)/sim/build/lv_conf_sim.h >/dev/null
	$(MAKE) -C sim/build -j$(shell nproc)
	mkdir -p .local/sim/play

# the square FakePod panel (460x460, no knob, two keys): its own build dir (LVGL is built again there), the same sources; sim/fp/bsp/bsp_geom.h shadows the board's
sim-fp:
	@test -f .local/faces.zip || $(MAKE) --no-print-directory faces-zip || echo "NOTICE: no .local/faces.zip (make faces-zip needs Pillow + Noto CJK + IBM Plex): the simulator runs without the HUD faces plug-in"
	@test -f app/sdkconfig || { echo "先跑一次 make build (app/sdkconfig is missing)"; exit 1; }
	python3 tools/simtools/sim_lvconf.py app/sdkconfig sim/build-fp/lv_conf_sim.h
	cmake -S sim -B sim/build-fp -DSIM_FP=1 -DSIM_LVCONF=$(CURDIR)/sim/build-fp/lv_conf_sim.h >/dev/null
	$(MAKE) -C sim/build-fp -j$(shell nproc)
	mkdir -p .local/sim/play_fp

# the square simulator's own leak loops: menus (the 2 x 4 grid), the keys, the status strip, drag paging, the popring, and two games by the keys only (20 cycles each, ASan/LSan)
sim-leak-fp: sim-fp
	SIM_FP=1 python3 tools/simtools/leak_par.py tools/simtools/leak_lines_fp.txt

# the square simulator's baseline (.local/sim/base_fp, the same page set as sim-base) and its check; the contact sheet of every square page for the owner is .local/sim/fp_sheet.png
sim-base-fp: sim-fp
	mkdir -p .local/sim/base_fp
	SIM_FP=1 python3 tools/simtools/ui_baseline.py sim .local/sim/base_fp $(SIM_PAGESETS)
	python3 tools/simtools/sim_sheet.py .local/sim/base_fp .local/sim/fp_sheet.png --cols 12 --cell 230

sim-diff-fp: sim-fp
	SIM_FP=1 python3 tools/simtools/sim_diff.py --base .local/sim/base_fp --out .local/sim/diff_fp --shots .local/sim/diff_shots_fp $(SIM_PAGESETS)

# a window of the square simulator: the keyboard's Up / Down arrows are the FakePod's top / bottom key (the console: simkey top|bottom [hold ms])
sim-run-fp: sim-fp
	@test -e .local/sim/play_fp/_player/config/app.txt || { mkdir -p .local/sim/play_fp; cp -r sim/fixture/. .local/sim/play_fp/; }
	@mkdir -p .local/sim/play_fp/_player && { test -e .local/sim/play_fp/_player/fonts || ln -s $(CURDIR)/fonts .local/sim/play_fp/_player/fonts; }
	@test -e .local/sim/play_fp/_player/faces.zip || { test -f .local/faces.zip && ln -s $(CURDIR)/.local/faces.zip .local/sim/play_fp/_player/faces.zip; true; }
	LSAN_OPTIONS=suppressions=$(CURDIR)/sim/lsan.supp sim/build-fp/sim --root .local/sim/play_fp $(ARGS)

# a window on .local/sim/play, never on .local/sim_root (the reference data of sim-diff): seeded once from .local/sim_root if it exists, else from sim/fixture
# (delete .local/sim/play to seed again); the repo's fonts/ (or SIM_FONTS=<dir>, e.g. .local/fontsets/out) is linked in as <root>/_player/fonts (the simulator never edits font.c's paths)
SIM_PLAY = .local/sim/play
sim-run: sim
	@test -e $(SIM_PLAY)/_player/config/app.txt || { if test -d .local/sim_root; then cp -a .local/sim_root/. $(SIM_PLAY)/; else cp -r sim/fixture/. $(SIM_PLAY)/; fi; }
	@test -e $(SIM_PLAY)/_player/fonts || { f="$(or $(SIM_FONTS),fonts)"; mkdir -p $(SIM_PLAY)/_player; test -d "$$f" && ln -s "$$(realpath $$f)" $(SIM_PLAY)/_player/fonts; true; }
	@test -e $(SIM_PLAY)/_player/faces.zip || { test -f .local/faces.zip && ln -s $(CURDIR)/.local/faces.zip $(SIM_PLAY)/_player/faces.zip; true; }
	LSAN_OPTIONS=suppressions=$(CURDIR)/sim/lsan.supp sim/build/sim --root $(SIM_PLAY) $(ARGS)

# the pages beyond the player's: every target that shoots pictures (sim-shots, sim-base, both halves of sim-diff) takes this one list, so the sets cannot drift
SIM_PAGESETS = --inst --ext --safe

sim-shots: sim
	mkdir -p .local/sim/shots
	python3 tools/simtools/ui_baseline.py sim .local/sim/shots $(SIM_PAGESETS)

# the simulator's own baseline: the same run as sim-shots, kept apart in .local/sim/base; sim-diff then checks every page of it (also the 13 that the board baseline excuses)
sim-base: sim
	mkdir -p .local/sim/base
	python3 tools/simtools/ui_baseline.py sim .local/sim/base $(SIM_PAGESETS)

# the screens against the board's baseline (BASE=...), a copy of .local/sim_root when it exists (else the fixture, with a notice); pages listed in
# sim/known_diffs.txt are reported but do not fail. Then, when make sim-base made one, against the simulator's own baseline (fixture data): all pages identical.
BASE ?= .local/ui_kit/base
# both halves always run and print their report; either one failing fails the target
sim-diff: sim
	@rc=0; if test ! -d $(BASE) && test ! -d .local/sim/base; then echo "sim-diff: no baseline yet ($(BASE) is the board's snapshot; yours: run 'make sim-base' once before your change, then make sim-diff)"; exit 1; fi; \
	if test -d $(BASE); then python3 tools/simtools/sim_diff.py --base $(BASE) $(SIM_PAGESETS) || rc=1; fi; \
	if test -d .local/sim/base && test "$$(realpath $(BASE))" != "$$(realpath .local/sim/base)"; then python3 tools/simtools/sim_diff.py --base .local/sim/base --out .local/sim/diff_own $(SIM_PAGESETS) || rc=1; fi; \
	exit $$rc

# leak/sanitizer cycles in the simulator: exit code, no ASan/LSan/UBSan output, no "APP ui leak", lv_bytes the same after every cycle
sim-leak: sim
	python3 tools/simtools/leak_par.py tools/simtools/leak_lines.txt

sim-root:
	$(PY) tools/simtools/sim_root.py $(PORT) .local/sim_root

# host render time of the heavy pages, before/after a change (relative; -O2 without sanitizers in .local/sim/perfbuild)
sim-perf:
	bash tools/simtools/sim_perf.sh $(LABEL)

# library stress test (not a gate): fake indexes of 2k..50k tracks through the host harness and the -O2 simulator, one summary table (tools/bench/lib_stress.py).
# LIBSTRESS_ARGS="--only S1,S3 --no-sim --sim sim/build/sim"
lib-stress:
	@test -f app/sdkconfig || { echo "先跑一次 make build (app/sdkconfig is missing)"; exit 1; }
	python3 tools/simtools/sim_lvconf.py app/sdkconfig .local/sim/perfbuild/lv_conf_sim.h
	cmake -S sim -B .local/sim/perfbuild -DSIM_LVCONF=$(CURDIR)/.local/sim/perfbuild/lv_conf_sim.h -DSIM_PERF=1 >/dev/null
	bash tools/quiet_build.sh .local/sim/perfbuild/build.log $(SUBMAKE) -C .local/sim/perfbuild -j$(shell nproc)
	python3 tools/bench/lib_stress.py $(LIBSTRESS_ARGS)

sim-test: sim
	python3 tools/check/check_help.py
	bash sim/test.sh

sim-test-fp: sim-fp
	bash sim/test_fp.sh

log:
	$(PY) tools/board/serial_capture.py $(PORT) $(SECS) --no-reset

log-reset:
	$(PY) tools/board/serial_capture.py $(PORT) $(SECS)

monitor:
	$(IDF) -p $(PORT) monitor --no-reset

crash:
	@case "$(PORT)" in /dev/serial/by-id/*) ;; *) echo "STOP: PORT must be a /dev/serial/by-id path"; exit 1;; esac
	source $(IDF_ENV) >/dev/null && $(PY) tools/board/crash_decode.py $(PORT)

web:
	tools/web/serve.sh

webcheck:
	tools/web/check.sh

# the publishable site = the page only (no firmware): page + vendored libraries (+ fonts / Noto downloads) into dist/, no upload; see tools/web_publish.py (--fonts / --noto / --fontsrc)
web-publish:
	python3 tools/web_publish.py --out dist $(WEB_PUBLISH_ARGS)

# a release, local only (no upload): builds both boards and writes release/<version>/ = one merged image per board + the card pack + SHA256SUMS + RELEASE_NOTES.md (the files the owner uploads
# as Release assets).  NOTES=<json with zh-TW / zh-CN / en> is required; version = version.txt when present; needs fonts/ui-{tc,sc,ext}.ttf; refuses stale / dirty / mismatched builds (tools/release.py).
release: build build-fakepod
	@test -n "$(NOTES)" || { echo "STOP: make release NOTES=notes.json  (JSON with the keys zh-TW zh-CN en)"; exit 1; }
	python3 tools/web_publish.py --out dist --fonts fonts
	ESPTOOL_PY=$(PY) python3 tools/release.py --out release --fonts fonts --dist dist --notes-json $(NOTES) $(RELEASE_ARGS)

# the TF card template zip (tools/gen/make_card_zip.py) -> .local/card-template.zip, then the host check
# our own synthesised noise scene pack (tools/gen/make_noise_pack.py) -> .local/library_free.muk (CC0; never in git), then the engine-reader test over it
noise-pack:
	python3 tools/gen/make_noise_pack.py
	$(MAKE) -s FAST=1 noise-pack-test

noise-pack-test:
	cmake -S components/player_core/host_test -B components/player_core/build-host -DPC_ASAN=OFF >/dev/null
	bash tools/quiet_build.sh components/player_core/build-host/build.log cmake --build components/player_core/build-host -j$(shell nproc) --target test_noisepack
	components/player_core/build-host/test_noisepack | tail -2

# the designed HUD faces: ONE stored zip, a plug-in on the card (_player/faces.zip), made from tools/gen/make_faces.py (needs Pillow, Noto CJK, IBM Plex Sans Condensed; generated, never in git)
faces-zip:
	python3 tools/gen/make_faces.py

# the default power-on pictures (one JPEG per boot mode, icons on black, 360 x 360): .local/splash/<mode>.jpg, made by tools/gen/make_splash.py (needs Pillow and fonts/ui-icon.ttf from make icons-build; generated, never in git); make card-zip puts them in _player/splash/
splash:
	@test -f $(FONTS_DIR)/ui-icon.ttf || { echo "STOP: $(FONTS_DIR)/ui-icon.ttf missing (make icons-build first: docs/dev/fonts.md)"; exit 1; }
	python3 tools/gen/make_splash.py
	python3 tools/gen/make_splash.py --check

card-zip:
	@test -f .local/faces.zip || $(MAKE) --no-print-directory faces-zip
	python3 tools/gen/make_card_zip.py
	python3 tools/check/check_card_zip.py

status:
	@$(PY) tools/board/ctl.py $(PORT) '{"op":"status"}'

ctl:
	@test -n '$(CMD)' || { echo "usage: make ctl CMD='{\"op\":\"status\"}'"; exit 1; }
	@$(PY) tools/board/ctl.py $(PORT) '$(CMD)'

backup:
	@case "$(PORT)" in /dev/serial/by-id/*) ;; *) echo "STOP: PORT must be a /dev/serial/by-id path"; exit 1;; esac
	tools/flash_backup.sh $(PORT)

