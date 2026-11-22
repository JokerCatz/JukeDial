# Licensing

## Our own code
JukeDial's own source (everything outside the third-party folders listed in `THIRD_PARTY_NOTICES.md`) is licensed under the
**GNU Lesser General Public License, version 2.1 or (at your option) any later version** (`LICENSE`; each source file carries
`SPDX-License-Identifier: LGPL-2.1-or-later`).

## The firmware image as a whole
The firmware is one statically linked binary. It contains parts under **Apache-2.0** (the LDAC encoder `libldac`, `esp_tinyusb`, ESP-IDF and
several Espressif components). Apache-2.0 is not compatible with LGPL-2.1-only, but it is compatible with LGPL-3.0. Because our code says "or any
later version", the combined firmware image is conveyed under the terms of the **GNU LGPL version 3 or later**
(`COPYING.LGPL-3.0`, which builds on the GPL text in `COPYING.GPL-3.0`). The source of each part keeps its own licence; the full list with versions and
licence texts is `THIRD_PARTY_NOTICES.md`.

LGPL section 6 asks that you can relink the firmware with a modified library: the complete source and the build instructions are public, so you can
rebuild and flash an image yourself.

## LDAC and aptX
- The firmware can send Bluetooth audio with **LDAC** (Sony) and **aptX / aptX HD** (Qualcomm). The encoder libraries (`libldac`, Apache-2.0; `libfreeaptx`,
  LGPL-2.1-or-later) are open source, but the trademarks, and Sony's certification process for products that use LDAC, belong to the vendors.
  **This firmware is not certified by Sony or Qualcomm.** It is meant for personal, non-commercial use. aptX involves patents; whether they affect your use is
  your own responsibility. LDAC is only available in headphones mode.
- Sony's `NOTICE` (`components/libldac/NOTICE`) states that certification is required to use LDAC in products; it is kept as shipped.

## cRSID (C64 SID emulation)
cRSID 1.58 by Hermit (Mihaly Horvath) has an informal licence, quoted exactly from the release archive (see `docs/licenses/cRSID-WTF.txt`):

> License is still WTF: Do what the frick you want with this code, but it would be nice mentioning me as the original author.

We read it as permission to use, modify and redistribute, with the credit requested. It is not a standard licence text and the author has not been
asked for a different one.

## libtta
The libtta 2.3 sources say "LGPL" without a version number, and its `COPYING` is the LGPL-3 text. Without a version number you may choose any LGPL
version; JukeDial uses it under LGPL-2.1 (see `docs/licenses/libtta-NOTE.md`). The combined image is conveyed under LGPL-3 terms in any case.

## Game_Music_Emu and the GPL file
Game_Music_Emu is LGPL-2.1-or-later. Upstream also contains `gme/Ym2612_MAME.cpp` (GPL-2.0-or-later). It is **not compiled** into JukeDial
(the build uses the LGPL "GENS" YM2612 core) and it is excluded from the public source tree (`tools/release/exclude.txt`).

## Fonts, icons, data
Fonts are SIL OFL 1.1 and are not stored in the repository; icons are Tabler Icons (MIT) and Material Symbols (Apache-2.0); see `docs/licenses/`.
Nothing here grants rights to the Sony, Qualcomm, Espressif, Bluetooth or other trademarks.
