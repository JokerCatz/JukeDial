# 20. Building and flashing from source

This chapter is for people who want to build the firmware themselves. If you only want to flash a released image, use the web tool's Flash tab (chapter 13).

## Prerequisites
- **ESP-IDF v6.1.** The ESP32-S31 is currently a preview target in IDF, so every `idf.py` call needs `--preview` (the Makefile adds it). The Makefile expects to activate IDF with `$HOME/.espressif/tools/activate_idf_v6.1.sh` (installing v6.1 with Espressif's installer creates it); the IDF checkout is expected at `$HOME/.espressif/v6.1/esp-idf` by default (override with `IDF_PATH`). **Do not** source the activation script yourself before running make; the Makefile sources it.
- Python 3, CMake, a C / C++ compiler and `make`.
- The first build needs the network: the IDF component manager downloads the dependencies named in `app/main/idf_component.yml` and `app/dependencies.lock` into `app/managed_components/`.
- Optional: the simulator needs the SDL2 and FreeType development files (found through pkg-config); the card pack, boot pictures and icons need Pillow; rebuilding fonts needs fontTools and brotli (put them in your own venv, not into IDF's environment).

## What is not in git
| What | How it is produced |
|---|---|
| The patched Bluedroid (for LDAC and aptX) | `make build` and `make build-fakepod` generate it automatically from your IDF (`tools/bt_ldac/make_bt.sh`, in `.local/bt_ldac/`); nothing to do by hand |
| FatFs with exFAT | Generated automatically before every build (`tools/fatfs_exfat/`) |
| `app/managed_components/` | Downloaded automatically by the first build |
| The font partition image `app/build/fonts.bin` | Needs four files, `fonts/ui-tc.ttf`, `ui-sc.ttf`, `ui-ext.ttf` and `ui-icon.ttf`, then `make fontpak` (`make flash-fonts` runs it for you). The released merged image already contains the fonts; this step is only for your own builds. To rebuild all four fonts from the original Noto and icon files in `fonts-src/`: `make fonts-all` (details and how to add missing characters: `docs/dev/fonts.md`) |
| The card pack | Released as `jukedial-<version>-card.zip`; for your own builds `make card-zip` (the HUD faces `faces.zip` come from `make faces-zip`, which needs Pillow, Noto CJK and IBM Plex Sans Condensed); unzip it into the card's root |
| The white-noise pack | `make noise-pack` (pure Python, reproducible); `make card-zip` puts it in the card pack |

The firmware boots without a font partition, but text falls back to a built-in font and Chinese characters and icons may be missing; flash the font image for the full interface.

## Building
```
make build            # round board (app/build)
make build-fakepod    # FakePod (app/build-fakepod)
make size             # memory use (FakePod: make size-fakepod)
```
`make build-stock` and `make build-fakepod-stock` are comparison images without LDAC / aptX; you normally do not need them.

## First flash of the round board
1. Plug the board in and set the port in `.local/board.mk`: `PORT = /dev/serial/by-id/<your board>` (see `.local/board.mk.example`). It must be a `/dev/serial/by-id/` path; the Makefile does not accept changeable names like `ttyACM0`.
2. **Back up first:** `make backup` saves the whole 16 MB flash to `backup/factory_full_*.bin`. The Makefile refuses every flash target (`make flash`, `make flash-all`, `make flash-fonts`) while there is no backup.
3. `make flash-all` writes the bootloader, the partition table and the app (it rewrites the partition table, which is why the backup matters).
4. `make flash-fonts` writes the partition table and the font partition.
5. Unzip the card pack into the TF card's root and insert the card.
Afterwards use `make flash` (app only) for code changes. If the board is not found, enter download mode: hold the BOOT pin-hole beside the USB-C while plugging in (chapter 15).

## Flashing the FakePod
The Makefile has no FakePod flash target; write it with esptool. The FakePod has a single USB port, and you enter download mode by holding both keys while powering on. Back up first:

```
python -m esptool --port <port> read-flash 0 0x1000000 backup/fakepod_full.bin
```

Run these in the IDF environment (`source $HOME/.espressif/tools/activate_idf_v6.1.sh`). After `make build-fakepod` and `make fontpak`, inside `app/build-fakepod/`:

```
python -m esptool --chip esp32s31 --port <port> --before default-reset --after watchdog-reset \
  write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x2000   bootloader/bootloader.bin \
  0x8000   partition_table/partition-table.bin \
  0x10000  jc_player.bin \
  0xC00000 ../build/fonts.bin
```

| Offset | File | What it is |
|---|---|---|
| 0x2000 | `bootloader/bootloader.bin` | the bootloader (the ESP32-S31 bootloader is at 0x2000, not 0) |
| 0x8000 | `partition_table/partition-table.bin` | the partition table (`app/partitions.csv`) |
| 0x10000 | `jc_player.bin` | the application (partition size 0xBF0000) |
| 0xC00000 | `fonts.bin` | the font partition (the last 4 MB) |

Settings and pairing data live in the nvs partition at 0x9000, which these commands do not touch. Always end with `--after watchdog-reset`: a normal reset stays in download mode. A released image is these four merged into one file and written at offset 0 (chapter 13).

## Simulator and tests
- `make sim-run` (round) and `make sim-run-fp` (FakePod) open the desktop simulator window. Run `make build` once first (the simulator needs `app/sdkconfig`). Headless and screenshot usage is in `sim/README.md`.
- `make test`: host unit tests (`FAST=1 make test` is quicker). `make sim-test` and `make sim-test-fp`: screen and behaviour tests in the simulator. `make lint`: static checks.
- The pictures in this guide are produced from the simulator by `tools/simtools/guide_shots.py` (`docs/guide/shotlist.md` has the list).

Previous: [FakePod-specific operation](19-fakepod.md). Back to [First boot](01-first-boot.md).
