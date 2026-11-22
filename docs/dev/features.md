# Feature map

What exists in JukeDial and where the code is. Each section gives two lines on what it does, the main files, how it is tested (the numbers refer to sections of [testing.md](testing.md)) and a status:

- **stable**: implemented and exercised; no known blocker.
- **experimental**: behind a setting that is off by default and says so on screen.
- **board-limited**: the hardware it needs is missing on one of the boards, or it has not been verified on the board it targets.

"Status" is a statement about what has been tried, not a guarantee; [test-results.md](test-results.md) has the evidence behind it. The repository layout is in `AGENTS.md`; user-facing descriptions are in `README.md` and `docs/guide/`.

## Code layout in one paragraph

`components/player_core/` is portable C (no ESP-IDF calls) with host tests: decoders' glue, pipeline rules, effects, game rules, parsers, state machines. `app/main/` is the firmware: services (`*_svc.c`), drivers' glue, the console, and `app/main/ui/` (every page, `scr_*.c`, built from shared pieces `ui_*.c`). `components/jc3636f118_bsp/` and `components/fakepod_bsp/` are the two boards' support packages behind the same `bsp/*.h` API; only one is compiled per image. `sim/` builds the UI and the core for Linux. Vendored libraries are under `components/` and are not restyled (list in `AGENTS.md`).

## Playback core and formats

Plays files from the TF card through one audio task that decodes, applies effects and volume, and writes to the wired I2S DAC or a Bluetooth sink. The sample-rate plan (native, halved, resampled, refused) is a pure function.

Main files: `components/player_core/src/pc_backend_*.c` (one per format family), `pc_pipe_rules.c`, `pc_rate.c`, `pc_halve.c`, `pc_rsq.c` (resampler), `pc_gapless.c`, `pc_speed.c`, `pc_burst.c`; `app/main/audio_svc.c`, `mp3_ctl.c`, `chip_ctl.c`, `speed_svc.c`; `components/jc3636f118_bsp/src/bsp_audio.c`.

Decoders:

| Format | Source | Licence family |
|---|---|---|
| MP3 | `components/player_core/third_party/minimp3` | CC0 |
| FLAC | `components/player_core/third_party/dr_flac` | public domain / MIT-0 |
| WAV, AIFF / AIFF-C | `pc_backend_wav.c`, `pc_backend_aiff.c` (own code) | LGPL-2.1-or-later |
| AAC (M4A), Ogg Vorbis, Opus | managed component `esp_audio_codec` (registered in `app/main/backend_sdec.c`; it also provides SBC for Bluetooth) | Espressif modified MIT |
| Musepack SV7 / SV8 | `components/libmpcdec` | BSD-3-Clause |
| TTA | `components/libtta` | LGPL |
| Game and chip music (NES, SNES, Game Boy, Genesis, many more) | `components/gme` (Game_Music_Emu; only the GENS YM2612 core is compiled), `components/emu2413` for the FM chips | LGPL-2.1-or-later / MIT |
| Tracker modules (MOD, XM, S3M, IT and about 45 more, packed Amiga formats) | `components/libxmp` (full version, accurate mode) | MIT |
| HivelyTracker / AHX | `components/libhvl` | BSD-3-Clause |
| C64 SID | `components/libcrsid` (cRSID) | author's informal licence |
| DOS AdLib music (RAD, HSC, IMF, DRO, CMF, A2M and others) | `components/libadplug` | LGPL-2.1-or-later |
| PICO-8 cartridges | `pc_backend_p8.c` (own port) | LGPL-2.1-or-later |
| Atari ST YM, ADX / VAG / BRSTM | `pc_backend_ym.c`, `gme` | own / LGPL |

The noise scenes use `app/main/third_party/stb_vorbis.c` (public domain / MIT) to read their sound pack.

Notes: a format tier table (`pc_chipseq.c`, array `FMT[]`) marks each extension none, stable or experimental. The DOS OPL players and some tracker formats are **experimental** and need the "experimental formats" setting. Files of 2 GiB or more are skipped. The Amiga sound mode (Paula emulation for 4-channel modules) sits behind the same setting. A crash-safety net remembers the track playing at a crash and avoids it next time (`pc_crashnet.c`).

Tested: section 1 (backend tests with hostile inputs, rate rules, gapless, speed, fuzzers), section 4 (hi-res rate changes in a leak run). Measured on boards: see test-results section 2. Status: **stable** for the common formats and wired playback up to 96 kHz on the round board and to 384 kHz on the FakePod; **experimental** for the formats named above; round-board 192 / 384 kHz output is **board-limited** (not verified through the PCM5102A).

## Effects apps

Fourteen effects, each shown as its own app page with a generated parameter page and a description: gain, balance, stereo widener, crossfeed, loudness, limiter, bass, equalizer, tone, freeverb, tube, compressor, velvet widener, velvet room (a reverb). They run in a fixed chain order (stereo difference first, reverb late, limiter last). Effects are classed light or heavy by measured CPU cost; heavy ones pause on battery, during CPU-hungry Bluetooth codecs, broadcast and above 48 kHz, with the reason on screen.

Files: `components/player_core/src/pc_fxapp*.c`, `pc_fxchain.c`, `pc_fxstate.c`; `app/main/fx_audio.c`, `fx_state.c`; `app/main/ui/scr_fx.c`, `scr_fxapp.c`, `ui_fxparam.c`. Settings can be exported and imported as a TOML file in the web tool.

Tested: section 1 (each effect, the chain order, reference renders). Cost classes: test-results 2.3. Status: **stable**.

## Bluetooth, Auracast and radio

- Headphone (source) mode: SBC, AAC, aptX, aptX HD and LDAC (the default image), LDAC adapting between 990, 660, 492 and 330 kbps; codec stepping down on persistent loss; AVRCP headset keys and absolute volume. Encoders run on core 1 (`bt_venc`).
- Speaker mode (a phone sends to the player): SBC / AAC receive, phone metadata and cover through AVRCP 1.6, exclusive source.
- Auracast broadcast and listening (LE Audio, quality steps), and a broadcast-radio mode that sends a station over Auracast.
- Internet radio over http and https (ICY metadata, HLS and Ogg streams), station lists editable in the web tool.

Files: `app/main/bt_svc.c`, `bt_svc_a2dp.c`, `bt_svc_cast_tx.c`, `bt_svc_pair.c`, `bt_meta*.c`, `bt_remote.c`, `cast_mode.c`, `radio_svc.c`, `wifi_svc.c`; `components/player_core/src/pc_btcodec.c`, `pc_castq.c`, `pc_btmeta*.c`, `pc_hls.c`, `pc_icy.c`, `pc_radio.c`; codecs `components/libldac` (Apache-2.0) and `components/libfreeaptx` (LGPL-2.1-or-later); the patched Bluedroid is generated by `tools/bt_ldac/make_bt.sh` (not stored). LDAC and aptX are for personal use and carry no vendor certification (`LICENSING.md`).

Tested: section 1 (codec state machines, metadata, volume rules); section 4 (Bluetooth audio pages cycles). Measured on boards: test-results sections 5 and 6. Status: **stable** for LDAC / AAC / SBC with the tested headset and for radio; **experimental** is not applied to Bluetooth, but real aptX headsets and many headsets and phones are untested; Auracast needs Wi-Fi off for the full player (memory).

## UI framework

Every page is composed from shared pieces so both panels look consistent: the ring (a ring of cells on the round panel, a 2 x 4 grid on the square one), lists with 40 items per page, the nine-grid (invisible 3 x 3 tap zones on the FakePod's playing pages), axis buttons (tap an axis, then the knob or keys change its value), pop-up rings, the page frame (content full-bleed; title and options appear on touch), the shared exit, shared play fields for games (arena, dial, cards, board, sectors). Gestures: flick down quick settings, flick up menu or back, left/right page turn, hold 3 s go home. There are no transitions.

Files: `app/main/ui/ui.c`, `ui_ring.c`, `ui_axis.c`, `ui_frame.c`, `ui_popring.c`, `ui_ninegrid.c`, `ui_kit.c`, `ui_theme.c`, `ui_tokens.*`, `ui_geom.h` (all panel-dependent numbers), `i18n*.c` (three languages: Traditional Chinese, Simplified Chinese, English; tables generated by `tools/gen/i18n_gen.py`), `icons.def` (icon registry). Ten colour themes are card files with a template. Fonts: four built fonts in `fonts/`, rebuilt from `fonts-src/`.

Guarded by: `ui_lint.py` (pages may not hand-build widgets), `ui_colour_lint.py`, `check_icons.py`, `check_font_glyphs.py`, `ui_boxes_check.py`. Tested: sections 3 and 4 (pages, pictures, leak cycles). Status: **stable**.

## Apps

Main menu entries (round panel ring / square grid): Songs, Chip music, Noise, Radio, Clock, Photos, Tools, Instrument, Favourites, Effects, Bluetooth, Settings.

| Area | What | Files |
|---|---|---|
| Clock and timers | clock faces (including HUD faces from an optional card pack), alarm (plays an MP3), stopwatch, countdown, pomodoro, drink reminder, habit check-in, sun and moon data, time zones, manual time setting | `clock_svc.c`, `timers_svc.c`, `drink_svc.c`, `ui/scr_clock.c`, `scr_timers.c`, `scr_drink.c`, `scr_habits.c`, `scr_timeset.c`, `pc_clock*.c`, `time_*.c` |
| Noise and ambient | white / pink / brown noise scenes, a scene editor with layers, ambient screens, breathing guide | `noise_engine.c`, `color_noise.c`, `ui/scr_noise.c`, `scr_ambient.c`, `scr_breath.c` |
| Photos | album view of the card's pictures | `album_svc.c`, `ui/scr_album.c` |
| Tools ring | dice, slot machine, fortune, lunch picker, people picker, ideas, vocabulary quiz, bubbles, games hub, sound bank, counters, ambient, torch, tuner (microphone); a moon page is reachable from the console | `ui/scr_tools.c` and the `scr_*.c` named after each tool; `tuner_svc.c`; data in `app/main/assets/` |
| Games | the fifteen games of `app/main/ui/games.def`: Beat tap, Ring breakout, Connect four, Deflector, Dominoes, Guess the song, Knob toys, Game of Life, 15 puzzle, Radio hunt, Safe cracker, Simon, Spirograph, Stack tower, Tic-tac-toe. Each has rule variants and art styles; sounds are synthesised, no sound files. Playable with taps plus knob or keys | `ui/scr_game_*.c`, `components/player_core/src/pc_game_*.c`, `ui/game_*.c`, `sfx.c`, `pc_sfx*.c` |
| Instrument app | a separate boot mode with drum pads, metronome, looper, sampler kits, a MIDI clock, and chip / FM voices (NES, Game Boy, SMS, SCC, VRC6, N163, PCE, POKEY, AY, Sunsoft 5B, and an FM chip), BLE MIDI | `instrument.c`, `inst_*.c`, `inst_chip_*.cpp`, `ui/scr_inst.c`, `inst_ui.c` |
| Favourites, statistics, sleep timer | | `stats_svc.c`, `sleep_timer.c`, `ui/scr_favs.c` |
| Quick settings and settings | brightness, volumes per output, wired-output page (rate ceiling, rate test page), Bluetooth audio page, card diagnostics, cover cache page, language, theme, experimental switches | `ui/ui_qs.c`, `scr_settings.c`, `scr_wiredout.c`, `scr_btaudio.c`, `scr_cardtest.c`, `scr_covers.c`, `scr_ratetest.c` |

Tested: section 1 (game rules, clock, timers), section 4 (a leak cycle for every game and most pages), pictures. Status: **stable**; the tuner needs the microphone and was observed in use; the tilt-based games need the IMU.

## Settings, configuration store and card layout

Settings live in a key-value table (`app/main/cfg.c`, 160 slots; the permitted keys are listed in `tools/check/cfg_keys.txt` and the check script rejects any other) and are saved to the card. Per-output volumes are separate keys: wired, one value per Bluetooth headset (by address), speaker mode. The web tool can edit most settings over the serial connection.

Card layout: music under the top-level folders you choose (for example `export/<album>/`); everything the device creates is in `_player/` (settings, library index, cover pack, fonts, loops, logs, splash pictures, theme files, station lists). Paths are defined in one header (`components/player_core/include/pc/pc_paths.h`, guarded by `check_card_paths.py`). A ready-made card template is built by `make card-zip`. Supported filesystems: FAT32 and exFAT with an MBR partition table (GPT cards do not mount). The card can be formatted from the device only after a failed mount, behind a warning and several deliberate taps.

Files: `cfg.c`, `pc_kv.c`, `pc_settings.c`, `card_hw.c`, `sd_format.c`, `card_test.c`, `thumb_svc.c`, `library_svc.c`, `pc_library.c`, `pc_thumbpak.c`, `tools/fatfs_exfat/`. Tested: section 1 (settings, library, cover pack, card paths). exFAT: measured on the FakePod only (test-results 8). Status: **stable**.

## USB modes (external mode)

A reboot mode where the player becomes a USB drive (the card as mass storage, with protections against pulling the card), a USB sound card (16 / 24 / 32 bit), and a USB / Bluetooth media keypad with remappable key layers. One USB port serves all of them.

Files: `app/main/ext_mode.c`, `ext_usbdesc.c`, `ext_uac.c`, `ext_audio.c`, `ext_ble.c`, `ext_hid_send.c`, `usb_mode.c`; `app/components/esp_tinyusb` (vendored with UAC2 enabled and a write-coalescing option, changes in `UPSTREAM.txt`); `components/player_core/src/pc_ext*.c`. Tested: section 4 (cycles of the external pages), host tests of descriptors and key parsing. Hardware: the sound card's rate switching and the drive-mode bulk copy were measured (test-results 2.1, 8); data-side card protections and host-OS behaviour are not verified. Status: **stable** for the drive copy and sound card as measured; the card protections and the three-in-one device on every host OS are **board-limited** (unverified).

## Web tool

A browser page (Chrome or Edge, Web Serial) that edits settings and station lists, builds and writes font packs, exports and imports effect settings, and flashes firmware images with size and hash checks. The page hosts no firmware; release files come from the release process.

Files: `tools/web/` (`index.html`, `conn.js`, `flasher.js`, `fonttab.js`, `lists.js`, `page.js`, ...), vendored libraries in `tools/web/vendor/` (esptool-js, harfbuzz), generators `tools/gen/web_*_gen.py`, `tools/web_publish.py`. Tested: section 5 (`make webcheck`, Node tests, headless Chrome flows, security checks). Not tested against a real board's flash from a hosted page in an automated way. Status: **stable** for settings and fonts; flashing of the FakePod image is **board-limited** until the FakePod's remaining checks pass.

## Simulator

A desktop (SDL2) build of the real UI and core for both panels, with fakes for hardware; headless scripting through the same console commands as the board's serial console; screenshot, diff and leak tools. Files: `sim/`, `tools/simtools/`. Tested by itself in section 4 (its own self test, red-light tests for the leak checker). Status: **stable**; it cannot judge audio quality, CPU, memory use or radio.

## Boards and what differs

| | Round (JC3636F118) | FakePod |
|---|---|---|
| Display | 360 x 360 round LCD (ST77916, QSPI) | 460 x 460 AMOLED (CO5300) |
| Input | touch (CST816), knob (two reed contacts, decoded in software), boot button | touch (CST816), two keys (power key via the PMU, boot key), no knob |
| DAC and volume | PCM5102A over I2S; no volume control on the chip, so volume is a software attenuation (100 = 0 dB) | CS43131; hardware volume only, digital side at unity, 32 kHz and above passed through, 16 to 32 kHz upsampled, below 16 kHz refused |
| IMU | QMI8658C (gravity maze, tilt games) | QMI8658C (axes swapped and inverted by the board support code) |
| RTC | none (time lost at power-off) | PCF85063 (time kept) |
| Haptics | DRV2605L motor | none |
| LED ring | drawn on the screen rim in software (no LED hardware) | drawn on the screen rim as a rounded box |
| Microphone | PDM microphone (tuner) | PDM microphone, same driver (observed in use: tuner and spectrum input) |
| Power and battery | supply voltage through an ADC only; no gauge | AXP2101 PMU: gauge, charge screen, auto power-off |
| Card | SDMMC 4-bit | SDMMC 4-bit |
| Flashing | `make flash` (needs a flash backup first) | no Make flash target; image written by hand or from the web tool; both keys at power-on enter download mode |

Files: `components/jc3636f118_bsp/`, `components/fakepod_bsp/` (it reuses the round board's audio, card and microphone sources), `sim/fp/` (square simulator geometry). Status of each board: the round board is **stable**; the FakePod is **stable** for wired audio, Bluetooth, library and UI as tested, with the battery gauge screens, touch accuracy, key feel and several other items **board-limited** (not yet verified on hardware); see test-results.

## Safety and recovery

A boot-loop guard (three consecutive crashes enter a safe mode that starts only the display, on core 0), a crash-safety net for tracks, a rule that every park of the second core goes through one module that disables its branch predictor, flash-write routing from internal-stack tasks only, an optional experimental run-time CPU clock setting that returns to 320 MHz after a crash, and power-on muting of the DAC before any output. Guarded by `check_safe_mode.py`, `check_flash_park.py`, `check_flash_writes.py`. Tested: test-results section 9. Status: **stable**; the CPU clock setting is **experimental**.
