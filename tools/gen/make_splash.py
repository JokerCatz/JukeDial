#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Default power-on pictures for the card pack: one JPEG per boot mode, .local/splash/<mode>.jpg (generated, never in git);
make card-zip puts them at _player/splash/<mode>.jpg. The firmware shows splash/<mode>.jpg, else the user's splash.jpg (app/main/splash.c, pc/pc_splash.h).

Layout rule (the ONE image size of both boards): 360 x 360, black, designed for the round panel: it shows exactly the inscribed circle (radius 180), and the artwork reaches ~14 px
from that circle at its widest (nothing outside it). The firmware draws the picture 1:1 centred on black (app/main/splash.c), so the same file sits centred
on the 460 x 460 square panel with a black margin. Plain white / neutral grey on black
(looks are tuned later; themes are a separate topic). The picture is baked: ICONS, not mode names, because the UI language is a run-time setting (a text
variant per language would be the alternative). The icon is the one the menu already uses for the mode (app/main/ui/icons.def, drawn from the built icon font
fonts/ui-icon.ttf); the player's has the wordmark "JukeDial" under its music icon.

Needs: Pillow (the same as make faces-zip) and fonts/ui-icon.ttf (make icons-build). The wordmark uses IBM Plex Sans Condensed Bold when installed, else fonts/ui-tc.ttf.
usage: make_splash.py [--out DIR]     write the pictures
       make_splash.py --check [DIR]   host check of the files (default .local/splash): the hardware JPEG route's constraints (baseline, 8 bit, gray or YCbCr with
                                      chroma 1x1 and luma 1x1 / 2x1 / 2x2, one scan, 360 x 360, at most 8 MiB of RGB565, file <= PC_SPLASH_MAX_FILE) and the circle (SAFE_R)"""
import io, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIZE = 360                              # ONE size for both boards: the round panel is exactly this square (its circle shows), the square panel centres it on black
SAFE_R = 168                            # hard limit for --check: nothing lit farther than this from the centre (the circle is 180)
ART_R = 166                             # what render() aims for: the widest artwork reaches ~14 px from the circle edge
MODES = {   # mode (pc_splash_modes) -> icon id in icons.def: the icon the menu / the mode's entry row uses
    "player": "SONGS", "usb": "L_USB", "inst": "INSTRUMENT", "cast": "RADIO", "charge": "L_CHARGE",
}
QUALITY = 85
MAX_FILE = 1 << 20                      # pc_splash.h PC_SPLASH_MAX_FILE
PLEX = "/usr/share/fonts/truetype/ibm-plex/IBMPlexSansCondensed-Bold.ttf"


def need_pillow():
    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        sys.exit("STOP: Pillow is missing (the same Pillow that make faces-zip needs): pip install pillow")


def icon_points():
    pts = {}
    for ln in open(os.path.join(ROOT, "app/main/ui/icons.def"), encoding="utf-8"):
        m = re.match(r'ICON\((\w+),\s*(?:FILLED|LINE),\s*"[^"]+",\s*(0x[0-9A-Fa-f]+),', ln)
        if m: pts[m[1]] = int(m[2], 16)
    return pts


def _ink_radius(img):
    """farthest lit pixel (> 24) from the picture centre, in px"""
    g = img.convert("L").point(lambda v: 255 if v > 24 else 0)
    bb = g.getbbox()
    if not bb: return 0.0
    c, px, far = SIZE / 2, g.load(), 0.0
    for y in range(bb[1], bb[3]):
        for x in range(bb[0], bb[2]):
            if px[x, y]: far = max(far, ((x + 0.5 - c) ** 2 + (y + 0.5 - c) ** 2) ** 0.5)
    return far


def _ink_box(font, text):
    """the real ink box of text relative to the draw origin: measured on a scratch canvas (font.getbbox is the metrics box for some glyphs, off by up to ~17 px for the charge icon)"""
    from PIL import Image, ImageDraw
    n = int(font.size * 3 * max(1, len(text))) + 64
    im = Image.new("L", (n, n), 0)
    ImageDraw.Draw(im).text((n // 3, n // 3), text, font=font, fill=255)
    bb = im.point(lambda v: 255 if v > 24 else 0).getbbox()
    return (bb[0] - n // 3, bb[1] - n // 3, bb[2] - n // 3, bb[3] - n // 3)


def _draw(mode, icon_px):
    """the picture for an icon whose longer ink side is icon_px; everything centred by its INK bounding box (not the font's advance / ascent)"""
    from PIL import Image, ImageDraw, ImageFont
    pts = icon_points()
    icon_font = os.path.join(ROOT, "fonts", "ui-icon.ttf")
    if not os.path.isfile(icon_font):
        sys.exit("STOP: fonts/ui-icon.ttf is missing (make icons-build ...: docs/dev/fonts.md)")
    ch = chr(pts[MODES[mode]])
    f0 = ImageFont.truetype(icon_font, 200)
    b = _ink_box(f0, ch)
    size = max(8, int(200 * icon_px / max(b[2] - b[0], b[3] - b[1])))
    f = ImageFont.truetype(icon_font, size)
    b = _ink_box(f, ch)                          # ink box of the glyph, relative to the draw origin
    iw, ih = b[2] - b[0], b[3] - b[1]
    img = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    d = ImageDraw.Draw(img)
    word = mode == "player"
    total, tb, tf = ih, None, None
    gap = max(8, icon_px // 8)
    if word:
        tfont = PLEX if os.path.isfile(PLEX) else os.path.join(ROOT, "fonts", "ui-tc.ttf")
        if not os.path.isfile(tfont): sys.exit("STOP: no font for the wordmark (IBM Plex Sans Condensed Bold, or fonts/ui-tc.ttf)")
        tf = ImageFont.truetype(tfont, max(8, int(icon_px * 0.34)))
        tb = _ink_box(tf, "JukeDial")
        total = ih + gap + (tb[3] - tb[1])
    y0 = (SIZE - total) // 2
    d.text(((SIZE - iw) // 2 - b[0], y0 - b[1]), ch, font=f, fill=(255, 255, 255))
    if word:
        d.text(((SIZE - (tb[2] - tb[0])) // 2 - tb[0], y0 + ih + gap - tb[1]), "JukeDial", font=tf, fill=(200, 200, 200))
    return img


def render(mode):
    """the largest icon whose artwork stays within ART_R px of the centre (the round panel's circle has radius SIZE/2 = 180; ART_R leaves a rim of 12-16 px)"""
    for px in range(340, 39, -1):             # a scan, not a bisection: the ink radius is not monotone in the font size (hinting, the stack's centring)
        img = _draw(mode, px)
        if _ink_radius(img) <= ART_R: return img
    sys.exit("STOP: no icon size fits " + mode)


def encode(img):
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=QUALITY, subsampling=0, optimize=False, progressive=False)   # baseline 4:4:4 (luma 1x1 = MCU 8x8): 360 = 45 MCUs, so the engine pads nothing; 4:2:0 would pad to 368 and need a repack
    return buf.getvalue()


def check_jpeg(data, name="picture"):
    """the hardware route's constraints (app/main/jpeg_route.c, components/player_core/src/pc_jpeg.c pc_jpeg_probe: NATIVE) + the file cap; returns a list of problems"""
    bad = []
    if len(data) > MAX_FILE: bad.append(f"{name}: {len(data)} bytes > {MAX_FILE}")
    if data[:2] != b"\xff\xd8": return bad + [f"{name}: not a JPEG"]
    i, sof, scans = 2, None, 0
    while i + 4 <= len(data):
        if data[i] != 0xFF: bad.append(f"{name}: bad marker at {i}"); break
        m = data[i + 1]
        if m == 0xFF: i += 1; continue
        if m == 0x01 or 0xD0 <= m <= 0xD8: i += 2; continue
        if m == 0xD9: break
        ln = data[i + 2] << 8 | data[i + 3]
        s = data[i + 4:i + 2 + ln]
        if 0xC0 <= m <= 0xCF and m not in (0xC4, 0xC8, 0xCC):
            sof = (m, s[0], s[1] << 8 | s[2], s[3] << 8 | s[4], s[5], [(s[7 + 3 * c] >> 4, s[7 + 3 * c] & 15) for c in range(s[5])])
        if m == 0xDA:
            scans += 1
            break                       # entropy data follows; a second scan would be progressive / non-interleaved (SOF2 or scan_comps != ncomp)
        i += 2 + ln
    if not sof: return bad + [f"{name}: no frame header"]
    m, prec, h, w, nc, fac = sof
    if m != 0xC0: bad.append(f"{name}: SOF{m & 15} (baseline SOF0 expected: the engine takes no progressive)")
    if prec != 8: bad.append(f"{name}: {prec} bit")
    if (w, h) != (SIZE, SIZE): bad.append(f"{name}: {w}x{h}, {SIZE}x{SIZE} expected (one size for both boards)")
    if w * h * 2 > 8 << 20: bad.append(f"{name}: more than 8 MiB of RGB565")
    if nc == 3:
        if any(f != (1, 1) for f in fac[1:]) or fac[0] not in ((1, 1), (2, 1), (2, 2)): bad.append(f"{name}: sampling {fac} (luma 1x1 / 2x1 / 2x2, chroma 1x1)")
    elif nc != 1: bad.append(f"{name}: {nc} components")
    return bad


def check_circle(img, name="picture"):
    """every lit pixel inside the safe circle (SAFE_R of the round panel's 180 px radius)"""
    far = _ink_radius(img)
    if far == 0: return [f"{name}: empty picture"]
    return [f"{name}: content reaches {far:.0f} px from the centre, the safe circle is {SAFE_R}"] if far > SAFE_R else []


def check_dir(d):
    from PIL import Image
    bad = []
    for mode in MODES:
        p = os.path.join(d, mode + ".jpg")
        if not os.path.isfile(p): bad.append(f"{mode}.jpg missing in {d}"); continue
        data = open(p, "rb").read()
        bad += check_jpeg(data, mode + ".jpg")
        bad += check_circle(Image.open(io.BytesIO(data)).convert("RGB"), mode + ".jpg")
    return bad


def main():
    a = sys.argv[1:]
    need_pillow()
    if a and a[0] == "--check":
        d = a[1] if len(a) > 1 else os.path.join(ROOT, ".local", "splash")
        bad = check_dir(d)
        if bad: sys.exit("make_splash --check: " + str(len(bad)) + " problem(s)\n" + "\n".join(bad))
        print("make_splash --check ok:", ", ".join(f"{m}.jpg {os.path.getsize(os.path.join(d, m + '.jpg'))} B" for m in MODES))
        return
    out = os.path.join(ROOT, ".local", "splash")
    if a[:1] == ["--out"] and len(a) > 1: out = a[1]
    os.makedirs(out, exist_ok=True)
    for mode in MODES:
        img = render(mode)
        bad = check_circle(img, mode)          # before the JPEG: its ringing is not content
        if bad: sys.exit("make_splash: " + "; ".join(bad))
        open(os.path.join(out, mode + ".jpg"), "wb").write(encode(img))
    bad = check_dir(out)
    if bad: sys.exit("make_splash: " + "\n".join(bad))
    print("make_splash: wrote", out, "(", ", ".join(f"{m} {os.path.getsize(os.path.join(out, m + '.jpg'))} B" for m in MODES), ")")


if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    main()
