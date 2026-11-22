# FatFs with exFAT (generated, never committed)

ESP-IDF v6.1 builds FatFs with `#define FF_FS_EXFAT 0` (`components/fatfs/src/ffconf.h`, hard-coded, no Kconfig switch), so a card formatted
exFAT (every 64 GB and larger card as sold) does not mount. `make_fatfs.sh` copies `$IDF_PATH/components/fatfs` to
`.local/fatfs_exfat/fatfs` (git-ignored) and makes four edits. Each must match exactly once, otherwise the script exits 1 with a message
(an IDF upgrade must fail loudly, never build without exFAT):

| file | edit |
|---|---|
| `src/ffconf.h` | `FF_FS_EXFAT` 0 -> 1 |
| `src/ffconf.h` | `FF_USE_LABEL CONFIG_FATFS_USE_LABEL` -> `1` (cardfmt 2026-11-14: the card format calls `f_setlabel` for the volume label `JUKEDIAL`; our sdkconfig leaves the Kconfig option off, and a sdkconfig default would not reach an existing `app/sdkconfig`). Before that it was `#ifdef` 1 / 0: with exFAT on, `ff.c` `dir_read` uses `FF_USE_LABEL` in a C `if`, where an undefined `CONFIG_` name does not build. |
| `vfs/vfs_fat.c` `vfs_fat_fstat` | `st->st_size = f_size(file);` -> saturated at 0xFFFFFFFF |
| `vfs/vfs_fat.c` `update_stat_struct` (`stat`) | `st->st_size = info->fsize;` -> saturated at 0xFFFFFFFF |

Why the last two: with exFAT a file size is 64 bit, the target's `off_t` is a 32-bit signed `long`. Unpatched, a 5 GiB file would
look like 1 GiB. Saturated, 0xFFFFFFFF is -1 and 2 GiB to 4 GiB are negative: all of them read as "too big". `vfs_fat_lseek` is not patched:
no offset of 2^31 or more ever reaches it (`PC_FILE_MAX` = 2 GiB - 1, `pc_reader_file` refuses a bigger file, the library hides it).

`app/CMakeLists.txt` adds `.local/fatfs_exfat` to `EXTRA_COMPONENT_DIRS` (a project component named `fatfs` replaces IDF's) and stops if it is
missing. The Makefile runs the script before every `idf.py` call; a stamp (hash of the IDF fatfs files + the script) makes it a no-op when
nothing changed, and the copy is regenerated after an IDF upgrade or an edit of the script. Nothing under `$HOME/.espressif` is touched.
`make_fatfs.sh --selftest` (part of `make lint`) runs the edits on fixtures: good lines patch, a missing or duplicated line fails.

FatFs stays at `FF_LBA64 0` (cards up to 2 TB): GPT is not read (it needs `FF_LBA64`), so a card must have an MBR ("msdos") table with one
partition, or none. The mount never formats (`format_if_mount_failed = false`); the only format is the owner's 記憶卡 page (cardfmt: `bsp_sdcard_format`,
only for a card whose mount failed with `gpt` / `nofs`, behind three confirmations), which uses this copy's `f_mkfs` (`FF_USE_MKFS 1`, IDF's own value) and `f_setlabel`.

## After an IDF upgrade
1. `make build`. If the script says "expected exactly one line matching ...", open the new `components/fatfs/src/ffconf.h` and
   `vfs/vfs_fat.c` in the IDF tree, find where `FF_FS_EXFAT` and the two `st_size` assignments went (or whether IDF now has a Kconfig switch
   or an int64 `off_t`), and update the patterns in `make_fatfs.sh`.
2. Check `diff -r $IDF_PATH/components/fatfs .local/fatfs_exfat/fatfs` shows only those four edits (plus `.jc_stamp`).
3. `bash tools/fatfs_exfat/make_fatfs.sh --selftest`, then the exFAT lines of `docs/human-test-checklist.md`.
