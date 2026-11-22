# Fonts: what they are and how to add missing characters

This page is for people and for AI coding assistants. Every command is run from the repository root. Python 3.9 or newer is enough.

## What the four font files are

The player draws all text with TrueType files, read through FreeType. Four files form a chain; a character is looked up in this order and the first file that has it wins:

| File | Content | Built from |
|---|---|---|
| `fonts/ui-tc.ttf` | Noto Sans TC subset: ASCII, kana, Big5 level 1 (5401 Traditional characters), GB2312 (6763 Simplified), every character in the firmware's strings, plus `tools/gen/font_sets/*.txt` | `fonts-src/noto-cjk/NotoSansTC-Regular.otf` |
| `fonts/ui-sc.ttf` | Noto Sans SC, only what `ui-tc.ttf` lacks (mostly Simplified forms) | `fonts-src/noto-cjk/NotoSansSC-Regular.otf` |
| `fonts/ui-ext.ttf` | Fallback set: Hangul, combining marks for kaomoji, symbols, maths, monochrome emoji. The characters are listed in `tools/gen/font_sets/ext.txt` | seven Noto fonts in `fonts-src/noto-extra/` |
| `fonts/ui-icon.ttf` | The icons (private-use code points) | Tabler Icons and Material Symbols in `fonts-src/icons/`, the list is `app/main/ui/icons.def` |

All four are tracked in git, so a clean clone builds and packs the firmware's font partition without any font step. `fonts-src/README.md` has the upstream URL, version, SHA-256 and licence of every source file.
A character that is in none of the first three files is shown as a blank, and the firmware remembers it in a missing-glyph record (below).

Where the player reads them: the flash partition `fonts` (4 MiB at 0xC00000, packed by `make fontpak`) and the card folder `_player/fonts/` (`/sdcard/_player/fonts/ui-tc.ttf` and so on).
At boot, for each name: if the card has a valid copy that differs from the flash copy, the card copy is used; otherwise the flash copy. So **copying a file to the card is enough; reboot the player**
(nothing else to configure). Limit: one file at most 4 MiB (the web tool's upload limit and the size of the partition); the whole flash image at most 4 MiB. The build keeps 200 KiB of the partition free.

## Three ways to add missing characters

### 1. Chinese text: the web tool

The web tool's Fonts tab (see `docs/guide/en/13-web-tool.md`) rebuilds `ui-tc.ttf` and `ui-sc.ttf` in the browser for characters of your library, the player's missing-glyph record and any text you type, and
gives you the files to copy to the card. It cannot change `ui-ext.ttf`. Use way 2 for anything that is not Chinese.

### 2. Anything: `tools/fonts/add_glyphs.py`

```
python -m venv .local/fontvenv                      # once; any Python environment works
.local/fontvenv/bin/pip install -r tools/requirements-fonts.txt        # Windows: .local\fontvenv\Scripts\pip
.local/fontvenv/bin/python tools/fonts/add_glyphs.py --text "Привет 한글 ☃"
```

(`make fonts-venv` does the first two lines on Linux and macOS.) Inputs, any number combined:
`--text "…"`, `--file titles.txt` (UTF-8), `--scan /music` (file and folder names, and the ID3v2 text frames of `.mp3` files; tags of FLAC, Ogg and M4A are not read: put those titles in a `--file`),
`--miss fontmiss.txt` (the player's missing-glyph record, see below). Options: `--fonts DIR` (the fonts the player has now; default `fonts/`; you may point at a copy of the card's `_player/fonts/`),
`--out DIR` (default `out/fonts/`), `--pack` (also a flash image), `--dry-run`, `--no-save-lists`.

What it does: finds the characters that none of `ui-tc/ui-sc/ui-ext` has; takes each from the first source that has it (Noto Sans TC, then SC, then the fallback fonts in the order NotoSans, KR, Devanagari, Symbols,
Symbols2, Math, Emoji); appends them to `tools/gen/font_sets/extra_chars.txt` (CJK) or `ext.txt` (fallback) so that a later `make fonts-all` reproduces the result; rebuilds only the files that change;
lists the characters **no source font has** (they stay missing; the run still succeeds); refuses, with the number of bytes over, to write a file above 4 MiB. Example run (real output, abridged):

```
14 distinct character(s) asked for, 13 not in ui-tc / ui-sc / ui-ext of fonts
-> ui-tc / ui-sc (9, Noto Sans TC first, then SC): П(U+041F) в(U+0432) … ☃(U+2603) 瘂(U+7602)
-> ui-ext from NotoEmoji.ttf (1): 🐉(U+1F409)
-> ui-ext from NotoSansKR.ttf (1): 글(U+AE00)
NOT AVAILABLE: no source font in fonts-src/ has 1 character(s): ཀ(U+0F40)
wrote out/fonts/ui-ext.ttf (75736 bytes)
wrote out/fonts/ui-sc.ttf (605008 bytes)
wrote out/fonts/ui-tc.ttf (2508468 bytes)
```

Put the result on the player, either:

* **Card (simplest):** copy the written `ui-*.ttf` files into `_player/fonts/` on the TF card (external mode lets you do it without removing the card), reboot the player.
* **Flash:** `--pack` also writes `out/fonts/fonts.bin` (all four fonts) and prints the line to flash it, for example
  `python -m esptool --chip esp32s31 --port <PORT> --baud 460800 --no-stub write-flash 0xC00000 out/fonts/fonts.bin`
  (put the board in download mode first; this writes only the fonts partition). Never flash without a backup of the board (`make backup`); see `AGENTS.md`.

**The missing-glyph record.** The firmware keeps the first 16 distinct code points it could not draw since boot, in RAM only: nothing is written to the card, and it is lost at reboot. Read it while the text is on screen:
(a) on the serial console type `fontmiss`; it prints `APP fontmiss total=3 list=U+7DB2,U+1F600 more=0`; save that line (or the whole log) to a file and pass `--miss file`;
(b) the web tool's Fonts tab shows the same list. `fontmiss clear` empties it. If `more=` is not 0, fix the listed ones, reboot, look again.

### 3. Full rebuild from the original fonts

```
make fonts-venv                                     # once
make fonts-all                                      # builds into .local/fontsets/out/
cp .local/fontsets/out/ui-*.ttf fonts/
make fonts-check                                    # a fresh build must equal fonts/*.ttf
make fontpak                                        # app/build/fonts.bin, prints the share of the partition (expected: about 80 %)
```

`make fonts-all` runs the four builders with no arguments, then the checks "every character in the firmware's strings has a glyph" (`ok`-style line:
`check_font_glyphs: 1603 characters in string literals, 0 without a glyph`) and "the four files fit the partition with 200 KiB to spare". It never writes into `fonts/`.
The tools are deterministic (the build date is pinned), so `make fonts-check` compares byte for byte; it prints one `ok   ui-tc.ttf identical (…)` line per file.
Choose another Python with `make fonts-all FONT_PY=/path/to/python`; another source with `FONT_SRC_TC=…`, `FONT_SRC_SC=…`, `FONT_SRC_EXTRA=…`.
Smaller image: `FONT_GB_LEVEL2=0` drops the 3008 rarer Simplified characters of GB2312 level 2.

To list by hand: characters for `ui-ext.ttf` go in `tools/gen/font_sets/ext.txt` (`U+XXXX` or `U+XXXX-U+YYYY` per line, `#` comments); CJK ones in `tools/gen/font_sets/extra_chars.txt` (just the characters).
To see what a music folder would need: `python tools/gen/make_font_fallback.py --scan /music` prints list lines to append to `ext.txt` (it changes nothing).

## Instructions for an AI coding assistant

Task: "add missing characters to the fonts". Do this:
1. Collect the characters (the user's text, a file, a music folder, or the console line from `fontmiss`).
2. `python tools/fonts/add_glyphs.py --dry-run <inputs>` and read the report. Characters under `NOT AVAILABLE` cannot be added without a new source font (add it to `fonts-src/`, `fonts-src/README.md`, and `EXT_SOURCES` in `tools/gen/make_font_fallback.py`; its licence must allow redistribution; tell the user).
3. Run again without `--dry-run`. If it says `REFUSED`, the size limit would be exceeded: report the byte count, do not work around it.
4. Tell the user which files to copy where (card `_player/fonts/`, then reboot), or use `--pack` and show the esptool line. **Do not flash or write to a serial port yourself.** Do not edit the files in `fonts/` by hand.
5. `make fonts-all && make fonts-check` after changing the lists, and commit the lists together with the rebuilt `fonts/*.ttf`.

## Licence

Noto fonts: SIL Open Font License 1.1 (`fonts-src/OFL.txt`; the copyright lines are in the fonts and in `fonts-src/licenses/`). Subsetting, merging and renaming are allowed; the built files must keep the licence
(`fonts-src/OFL.txt`) with them when you distribute them, and may not be sold on their own. The Noto name is a trademark of Google LLC; the built files are called `ui-*.ttf`.
Icons: Tabler Icons (MIT), Material Symbols (Apache-2.0): `docs/licenses/ICONS.md`.
