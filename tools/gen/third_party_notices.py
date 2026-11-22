#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Writes THIRD_PARTY_NOTICES.md at the repository root: one table of every third-party part (vendored code, ESP-IDF managed components,
ESP-IDF, fonts, icons, data) with version, licence and where the licence text is, plus the attribution lines some licences require.
The table below is the source of truth for licences; versions of the managed components come from app/dependencies.lock.

  third_party_notices.py            write THIRD_PARTY_NOTICES.md
  third_party_notices.py --check    fail when the file is stale, when a vendored directory / a components/ folder with a SOURCE note /
                                    a managed component of dependencies.lock is missing from the table, or a listed licence text is gone
                                    (`make notices-check`, part of `make lint-app`)
Pure Python, no network."""
import os, re, sys

if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
    print(__doc__); sys.exit(0)

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OUT = os.path.join(ROOT, "THIRD_PARTY_NOTICES.md")
FW, WEB, BUILD, DATA = "firmware", "web tool only", "build / not in the image", "data / card pack"

# name, version, licence (SPDX where one exists), licence text (repo paths, or a note), scope, covered directories (for the coverage check), note
VENDORED = [
    ("Game_Music_Emu (gme)", "0.6.5 + our patches", "LGPL-2.1-or-later", ["components/gme/license.txt"], FW, ["components/gme"],
     "Only the GENS YM2612 core is compiled (VGM_YM2612_GENS). `gme/Ym2612_MAME.cpp` (GPL-2.0-or-later) and its licence file are in the source tree but are not compiled into the image and are excluded from the public tree."),
    ("emu2413", "commit d8ed4d2a (v1.6.4 line)", "MIT", ["components/emu2413/LICENSE", "docs/licenses/emu2413-MIT.txt"], FW, ["components/emu2413"], "Mitsutaka Okazaki; our fork."),
    ("AdPlug + libbinio", "AdPlug 2.4, libbinio 1.5", "LGPL-2.1-or-later", ["docs/licenses/adplug-LGPL-2.1.txt", "docs/licenses/libbinio-LGPL-2.1.txt"], FW, ["components/libadplug"],
     "GPL-licensed players (jbm, adl) and the aPlib depacker are left out."),
    ("cRSID", "1.58", "informal \"WTF\" statement by the author (do what you want, mention the author)", ["docs/licenses/cRSID-WTF.txt"], FW, ["components/libcrsid"],
     "cRSID 1.58 by Hermit (Mihaly Horvath). Not a standard licence; quoted exactly as shipped."),
    ("libfreeaptx", "commit 476e2d56 (2026-03-25)", "LGPL-2.1-or-later", ["components/libfreeaptx/COPYING"], FW, ["components/libfreeaptx"],
     "aptX / aptX HD encoder, a re-implementation. aptX is a Qualcomm trademark and involves patents; this firmware is not certified. See LICENSING.md."),
    ("HivelyTracker replayer (libhvl)", "commit f393ca7c", "BSD-3-Clause", ["components/libhvl/hvl/LICENCE", "docs/licenses/hivelytracker-BSD3.txt"], FW, ["components/libhvl"], "Pete Gordon."),
    ("libldac", "2.0.2 (AOSP external/libldac)", "Apache-2.0", ["components/libldac/LICENSE", "components/libldac/NOTICE"], FW, ["components/libldac"],
     "Sony LDAC encoder. Use in a commercial product needs Sony's certification; this firmware is not certified. See LICENSING.md."),
    ("libmpcdec (Musepack)", "SVN r475 (2011-08-10)", "BSD-3-Clause", ["components/libmpcdec/libmpcdec/COPYING", "docs/licenses/musepack-libmpcdec-BSD3.txt"], FW, ["components/libmpcdec"], "Musepack Development Team."),
    ("libtta", "2.3", "LGPL (no version in the source headers; used under LGPL-2.1)", ["components/libtta/libtta/COPYING", "docs/licenses/libtta-NOTE.md"], FW, ["components/libtta"],
     "COPYING is the LGPL-3 text as shipped upstream; the note explains the choice."),
    ("libxmp", "4.7.2", "MIT (ProWizard MIT; PowerPacker, Stonecracker, MD5 public domain; tinfl MIT)", ["components/libxmp/libxmp/docs/COPYING", "components/libxmp/libxmp/docs/CREDITS"], FW, ["components/libxmp"], ""),
    ("minimp3", "commit ea99364f", "CC0-1.0", ["components/player_core/third_party/minimp3/LICENSE"], FW, ["components/player_core/third_party/minimp3"], "lieff."),
    ("miniz", "3.0.2 (headers say 3.0.0)", "MIT", ["components/player_core/third_party/miniz/LICENSE"], FW, ["components/player_core/third_party/miniz"], "Only tinfl is compiled."),
    ("tomlc99", "commit 62ce93ce", "MIT", ["components/player_core/third_party/tomlc99/LICENSE"], FW, ["components/player_core/third_party/tomlc99"], "CK Tan."),
    ("dr_flac", "0.13.4", "public domain or MIT-0", ["components/player_core/third_party/dr_flac/SOURCE.md"], FW, ["components/player_core/third_party/dr_flac"], "David Reid; the licence is the end of dr_flac.h."),
    ("zepto8 (reference only)", "commit b2929a4f", "WTFPL-2.0", ["docs/licenses/zepto8-WTFPL.txt"], FW, ["components/player_core/third_party/zepto8"],
     "Nothing is vendored; the PICO-8 synth in `pc_backend_p8.c` is our port. Sam Hocevar."),
    ("stb_vorbis", "1.22", "public domain or MIT", ["app/main/third_party/stb_vorbis.c", "app/main/third_party/SOURCE.md"], FW, ["app/main/third_party"], "Sean Barrett; the licence is the end of the file."),
    ("esp_tinyusb (our copy with UAC2 on)", "2.4.0 (TinyUSB 0.21.0~4)", "Apache-2.0", ["app/components/esp_tinyusb/LICENSE", "app/components/esp_tinyusb/UPSTREAM.txt"], FW, ["app/components/esp_tinyusb"],
     "Espressif; changes are listed in UPSTREAM.txt."),
    ("esptool-js", "0.7.1", "Apache-2.0", ["tools/web/vendor/esptool-js/0.7.1/LICENSE", "tools/web/vendor/esptool-js/0.7.1/NOTICE"], WEB, ["tools/web/vendor/esptool-js"], "Espressif; pako is inlined (see NOTICE)."),
    ("harfbuzzjs (hb-subset wasm)", "1.6.4 (HarfBuzz 15.0.0)", "MIT (HarfBuzz \"Old MIT\")", ["tools/web/vendor/harfbuzzjs/1.6.4/LICENSE", "tools/web/vendor/harfbuzzjs/1.6.4/NOTICE"], WEB, ["tools/web/vendor/harfbuzzjs"], ""),
    ("FatFs with exFAT (patched copy of ESP-IDF's fatfs)", "generated from the IDF in use", "FatFs BSD-style licence (ChaN) + Apache-2.0 (IDF glue)", ["tools/fatfs_exfat/README.md"], FW, ["tools/fatfs_exfat"],
     "Only our generator script is in this repository; the patched copy is made at configure time from ESP-IDF's component and is not stored."),
]

# managed components: licence + licence file name inside app/managed_components/<ns>__<name>/ (fetched by the build, not stored here)
MANAGED = {
    "lvgl/lvgl": ("MIT", "LICENCE.txt", FW, "LVGL Kft."),
    "espressif/freetype": ("FreeType License (BSD-style, attribution required; FTL)", "LICENSE", FW, "Attribution line below is required."),
    "espressif/libpng": ("PNG Reference Library License v2", "LICENSE", FW, ""),
    "espressif/zlib": ("Zlib", "LICENSE", FW, "Jean-loup Gailly and Mark Adler."),
    "espressif/esp_audio_codec": ("Espressif Modified MIT (use exclusively with Espressif products)", "LICENSE", FW, "SBC / AAC for Bluetooth."),
    "espressif/esp_new_jpeg": ("Espressif MIT (use on Espressif products)", "LICENSE", FW, ""),
    "espressif/tinyusb": ("MIT", "LICENSE", FW, "hathach."),
    "espressif/cjson": ("MIT", "LICENSE", FW, "Dave Gamble and cJSON contributors."),
    "espressif/qoi": ("MIT", "LICENSE", FW, "Dominic Szablewski."),
    "espressif/esp_lvgl_adapter": ("Apache-2.0", "LICENSE", FW, ""),
    "espressif/esp_lcd_st77916": ("Apache-2.0", "license.txt", FW, ""),
    "espressif/esp_lcd_touch": ("Apache-2.0", "license.txt", FW, ""),
    "espressif/esp_lcd_touch_cst816s": ("Apache-2.0", "license.txt", FW, ""),
    "espressif/esp_lv_decoder": ("Apache-2.0", "license.txt", FW, ""),
    "espressif/esp_lv_fs": ("Apache-2.0", "license.txt", FW, ""),
    "espressif/esp_mmap_assets": ("Apache-2.0", "license.txt", FW, ""),
    "espressif/button": ("Apache-2.0", "license.txt", FW, ""),
    "espressif/knob": ("Apache-2.0", "license.txt", FW, ""),
    "espressif/cmake_utilities": ("Apache-2.0", "license.txt", BUILD, "Build helper only."),
}
OVERRIDDEN = {"espressif/esp_tinyusb"}      # in the lock, but the image uses our copy in VENDORED

OTHER = [
    ("ESP-IDF", "v6.1", "Apache-2.0 (with its own components: FreeRTOS MIT, lwIP BSD, mbedTLS Apache-2.0, Bluedroid Apache-2.0, picolibc BSD, ...)", "the ESP-IDF tree (`LICENSE`, per-component licences)", FW,
     "Not stored here; the build uses the user's ESP-IDF. A patched copy of its Bluedroid is made by `tools/bt_ldac/make_bt.sh` (LDAC / aptX in the default image)."),
    ("Noto Sans CJK, Noto Serif CJK (TC, SC)", "2.004", "SIL OFL 1.1", "`tools/OFL-1.1.txt`, `docs/licenses/noto-sans-cjk-OFL.txt`", DATA, "Font files are not in the repository; the firmware's font pack carries converted glyphs."),
    ("Noto Emoji", "-", "SIL OFL 1.1", "`docs/licenses/noto-emoji-OFL.txt`", DATA, ""),
    ("IBM Plex Sans Condensed", "-", "SIL OFL 1.1", "`tools/OFL-1.1.txt`, `docs/licenses/face-art-fonts.md`", DATA, "Clock face art."),
    ("Tabler Icons", "3.52.0", "MIT", "`docs/licenses/ICONS.md`", FW, "Packed into the icon font from `app/main/ui/icons.def` only."),
    ("Material Symbols Rounded", "0.49.0", "Apache-2.0", "`docs/licenses/ICONS.md`", FW, "Same."),
    ("ECDICT (100-word sample)", "-", "MIT", "`docs/licenses/ECDICT-MIT.txt`", FW, "`app/main/assets/vocab/sample.zip`."),
    ("NightPlayer (FakePod Nano firmware)", "-", "MIT", "`docs/licenses/NightPlayer-MIT.md`", FW, "Register values and sequences used as facts; no code copied."),
    ("zzfx, jsfxr (sound-effect parameter sources)", "-", "MIT, Unlicense", "`docs/licenses/zzfx-MIT.txt`, `docs/licenses/jsfxr-UNLICENSE.txt`, `docs/licenses/sfx-bank-credits.md`", FW, "Parameters only; the sounds are synthesized by our own code."),
]

ATTRIBUTION = [
    "Portions of this software are copyright (c) 2026 The FreeType Project (www.freetype.org). All rights reserved.",
    "Icons: Tabler Icons (MIT) and Material Symbols (Apache-2.0).",
    "cRSID 1.58 by Hermit (Mihaly Horvath), used under the author's informal licence (docs/licenses/cRSID-WTF.txt).",
    "The libpng and zlib notices are the files named in the table above; the zlib licence requires that the origin of the software is not misrepresented.",
    "LDAC is a Sony trademark; aptX is a Qualcomm trademark. This firmware is not certified by Sony or Qualcomm; use is personal and non-commercial (see LICENSING.md).",
]

def lock_components():
    out, cur = {}, None
    for line in open(os.path.join(ROOT, "app", "dependencies.lock"), encoding="utf-8"):
        m = re.match(r"^  ([\w./-]+):\s*$", line)
        if m: cur = m.group(1); continue
        m = re.match(r"^    version: '?([^'\n]+)'?\s*$", line)
        if m and cur and cur not in out: out[cur] = m.group(1)
        if line.startswith("direct_dependencies") or line.startswith("manifest_hash"): cur = None
    return out

def build():
    lock = lock_components()
    rows, problems = [], []
    for name, ver, lic, texts, scope, dirs, note in VENDORED:
        rows.append((name, ver, lic, ", ".join("`%s`" % t for t in texts), scope, note))
        for d in dirs:
            if not os.path.isdir(os.path.join(ROOT, d)): problems.append("listed directory is missing: " + d)
        for t in texts:
            if not os.path.exists(os.path.join(ROOT, t)): problems.append("listed licence text is missing: " + t)
    for dep, ver in sorted(lock.items()):
        if dep in ("idf",) or dep in OVERRIDDEN: continue
        if dep not in MANAGED:
            problems.append("managed component %s (dependencies.lock) is not in MANAGED: add its licence" % dep); continue
        lic, f, scope, note = MANAGED[dep]
        md = os.path.join(ROOT, "app", "managed_components", dep.replace("/", "__"))
        if os.path.isdir(md) and not os.path.exists(os.path.join(md, f)): problems.append("licence file %s is missing in %s" % (f, md))
        rows.append((dep + " (managed)", ver, lic, "`app/managed_components/%s/%s` (fetched by the build)" % (dep.replace("/", "__"), f), scope, note))
    for dep in MANAGED:
        if dep not in lock: problems.append("MANAGED lists %s which is not in dependencies.lock" % dep)
    for name, ver, lic, where, scope, note in OTHER:
        rows.append((name, ver, lic, where, scope, note))
    # vendored folders under components/ and tools/web/vendor that nobody listed
    covered = {d for v in VENDORED for d in v[5]}
    comp = os.path.join(ROOT, "components")
    for d in sorted(os.listdir(comp)):
        p = os.path.join(comp, d)
        if os.path.isdir(p) and any(os.path.exists(os.path.join(p, n)) for n in ("SOURCE.md", "SOURCE", "UPSTREAM.txt", "VENDOR.md")) and "components/" + d not in covered:
            problems.append("components/%s has an upstream note but is not in VENDORED" % d)
    vend = os.path.join(ROOT, "tools", "web", "vendor")
    for d in sorted(os.listdir(vend)):
        if os.path.isdir(os.path.join(vend, d)) and "tools/web/vendor/" + d not in covered:
            problems.append("tools/web/vendor/%s is not in VENDORED" % d)
    tp = os.path.join(ROOT, "components", "player_core", "third_party")
    for d in sorted(os.listdir(tp)):
        if os.path.isdir(os.path.join(tp, d)) and "components/player_core/third_party/" + d not in covered:
            problems.append("components/player_core/third_party/%s is not in VENDORED" % d)
    lines = ["# Third-party notices", "",
             "<!-- generated by tools/gen/third_party_notices.py; edit the tables there, then run it again -->", "",
             "JukeDial's own code is LGPL-2.1-or-later (`LICENSE`); how the parts combine is in `LICENSING.md`. Everything below keeps its own licence.",
             "\"Where the text is\" names the licence text or notice. Parts marked *fetched by the build* are ESP-IDF managed components: the build downloads them into `app/managed_components/`.",
             "Scope: **firmware** = linked into the default image; **web tool only** = shipped with the browser tool, not in the firmware; **data / card pack** = fonts and data files; **build** = not in the image.", "",
             "| Component | Version | Licence | Where the text is | Scope | Note |", "|---|---|---|---|---|---|"]
    for r in rows:
        lines.append("| " + " | ".join(c.replace("|", "\\|") for c in r) + " |")
    lines += ["", "## Required attribution lines", ""] + ["- " + a for a in ATTRIBUTION] + [""]
    return "\n".join(lines), problems

text, problems = build()
if "--check" in sys.argv:
    cur = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
    if cur != text: problems.append("THIRD_PARTY_NOTICES.md is stale: run python3 tools/gen/third_party_notices.py")
    for p in problems: print("notices:", p)
    print("notices: %d problem(s), %d rows" % (len(problems), text.count("\n| ") - 1))
    sys.exit(1 if problems else 0)
if problems:
    for p in problems: print("notices:", p)
    sys.exit(1)
open(OUT, "w", encoding="utf-8").write(text)
print("ok", OUT)
