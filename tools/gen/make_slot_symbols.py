#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Slot machine symbols (spec an internal note): eight emoji from Noto Color Emoji
(SIL Open Font License 1.1, Google; see docs/licenses/noto-emoji-OFL.txt) rendered once into a flash asset, so the
board needs no emoji font. Order = pc_reel.h symbols: cherry lemon grape bell star diamond seven clover.

Writes app/main/assets/slot_syms.bin (embedded with EMBED_FILES, committed; run again only to change the look):
  "SYM1", u8 count, u8 size, u16 0, then per symbol size*size RGB565 (little endian) followed by size*size A8
  = LVGL's LV_COLOR_FORMAT_RGB565A8 (colour plane, then alpha plane).
Usage: tools/gen/make_slot_symbols.py [--size 52] [--font /usr/share/fonts/truetype/noto/NotoColorEmoji.ttf] [--png DIR]
Needs Pillow with raqm (the keycap seven is a ligature)."""
import argparse, os, struct, sys
from PIL import Image, ImageDraw, ImageFont

SYMS = ["\U0001F352", "\U0001F34B", "\U0001F347", "\U0001F514", "⭐", "\U0001F48E", "7️⃣", "\U0001F340"]
NAMES = ["cherry", "lemon", "grape", "bell", "star", "diamond", "seven", "clover"]
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def render(font, ch, size):
    im = Image.new("RGBA", (180, 180), (0, 0, 0, 0))
    ImageDraw.Draw(im).text((20, 20), ch, font=font, embedded_color=True)
    im = im.crop(im.getbbox())
    w, h = im.size
    s = max(w, h)
    sq = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sq.paste(im, ((s - w) // 2, (s - h) // 2))
    # premultiplied resize keeps the dark fringe off the edges
    return sq.resize((size, size), Image.LANCZOS, reducing_gap=3.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=52)
    ap.add_argument("--font", default="/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf")
    ap.add_argument("--out", default=os.path.join(ROOT, "app/main/assets/slot_syms.bin"))
    ap.add_argument("--png", help="also write each symbol as a PNG into this folder (to look at)")
    a = ap.parse_args()
    font = ImageFont.truetype(a.font, 109)          # the bitmap strike of the colour font
    out = bytearray(b"SYM1" + struct.pack("<BBH", len(SYMS), a.size, 0))
    for ch, name in zip(SYMS, NAMES):
        im = render(font, ch, a.size)
        if a.png:
            os.makedirs(a.png, exist_ok=True)
            im.save(os.path.join(a.png, name + ".png"))
        px = im.load()
        col, alpha = bytearray(), bytearray()
        for y in range(a.size):
            for x in range(a.size):
                r, g, b, al = px[x, y]
                col += struct.pack("<H", (r >> 3) << 11 | (g >> 2) << 5 | b >> 3)
                alpha.append(al)
        out += col + alpha
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(out)
    print(f"{a.out}: {len(SYMS)} symbols {a.size}x{a.size}, {len(out)} bytes")


if __name__ == "__main__":
    sys.exit(main())
