#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Designed faces (TFACE, "特製畫面然後挖空填入數字"): every built-in face is a fixed drawing per board and feature with
blanks (slots) that the firmware fills with fixed-format values. This script IS the art: it draws each face's greyscale layers
(frame, accent, label, head, warn) and the shared sprite sheets (seven-segment digits drawn as polygons, weekday / ON-OFF / state
plates), writes the slot definitions (face.txt) and packs it all into ONE uncompressed (STORE method) zip, .local/faces.zip (generated, not in git),
which is a plug-in on the TF card: _player/faces.zip (make card-zip includes it). The firmware reads the entries in place with the chip-zip reader and
tints the layers per colour scheme at draw time.

Fonts used for the fixed labels (rendered here into the art, not shipped as fonts): Noto Serif CJK TC Bold, Noto Sans CJK TC Bold
and IBM Plex Sans Condensed Bold, all SIL Open Font License 1.1 (docs/licenses/). Labels are fixed bilingual art, not i18n.

The format is documented in the ui design notes, section 設計錶面. Zip entries: <face>/face.txt, <face>/<layer>.jdl, sheets/sheets.txt, sheets/<sheet>.jdl.
  image : "JDL1" u16 w u16 h, then RLE rows: b<0x40 = b+1 pixels of 0; 0x40..0x7f = (b-0x3f) pixels of 255; 0x80..0xfe = one pixel of (b&0x7f)*2;
          0xff n v = n+1 pixels of v
Usage: tools/gen/make_faces.py [--png DIR] [--guide docs/images/clock]   (previews of every face x scheme; the slot-grid guide images)"""
import argparse, math, os, struct, sys
from PIL import Image, ImageDraw, ImageFont, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, '.local', 'faces.zip')      # generated, never in git; make card-zip puts it at _player/faces.zip, the simulator fixture gets a copy
SS = 4                                                   # supersampling
SERIF = '/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc'
SANS = '/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc'
COND = '/usr/share/fonts/truetype/ibm-plex/IBMPlexSansCondensed-Bold.ttf'
_fc = {}
def font(kind, px):
    k = (kind, px)
    if k not in _fc:
        if kind == 'serif': _fc[k] = ImageFont.truetype(SERIF, px * SS, index=3)
        elif kind == 'sans': _fc[k] = ImageFont.truetype(SANS, px * SS, index=3)
        else: _fc[k] = ImageFont.truetype(COND, px * SS)
    return _fc[k]

# ---------------------------------------------------------------- canvas
ROLES = ['frame', 'accent', 'label', 'head', 'warn']
class Face:
    def __init__(self, name, w, h, round_):
        self.name, self.w, self.h, self.round = name, w, h, round_
        self.L = {r: Image.new('L', (w * SS, h * SS), 0) for r in ROLES}
        self.slots = []
        self.checks = []       # (what, role, box, axes): ink of that role inside box must be centred in it (tools/check/check_face_centering.py)
    def d(self, role): return ImageDraw.Draw(self.L[role])
    def poly(self, role, pts, fill=None, line=None, width=1.5):
        dr = self.d(role)
        p = [(x * SS, y * SS) for x, y in pts]
        if fill is not None: dr.polygon(p, fill=int(fill * 255))
        if line is not None: dr.line(p + [p[0]], fill=int(line * 255), width=max(1, int(width * SS)), joint='curve')
    def line(self, role, pts, a=1.0, width=1.5):
        self.d(role).line([(x * SS, y * SS) for x, y in pts], fill=int(a * 255), width=max(1, int(width * SS)), joint='curve')
    def rect(self, role, x0, y0, x1, y1, a=1.0):
        self.d(role).rectangle([x0 * SS, y0 * SS, x1 * SS - 1, y1 * SS - 1], fill=int(a * 255))
    def circle(self, role, cx, cy, r, a=1.0, width=1.5, fill=None):
        self.d(role).ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS, (cy + r) * SS], outline=int(a * 255) if width else None,
                             width=max(1, int(width * SS)) if width else 0, fill=None if fill is None else int(fill * 255))
    def text(self, role, x, y, s, kind, px, anchor='mm', a=1.0, spacing=0):
        f = font(kind, px)
        dr = self.d(role)
        if not spacing:
            dr.text((x * SS, y * SS), s, font=f, fill=int(a * 255), anchor=anchor)
            return
        wid = sum(f.getlength(c) for c in s) + spacing * SS * (len(s) - 1)
        x0 = x * SS - (wid / 2 if anchor[0] == 'm' else wid if anchor[0] == 'r' else 0)
        for c in s:
            dr.text((x0, y * SS), c, font=f, fill=int(a * 255), anchor='l' + anchor[1])
            x0 += f.getlength(c) + spacing * SS
    def stripes(self, role, x0, y0, x1, y1, period=12, duty=0.5, a=1.0, lean=1.0, clip=None):
        """hazard stripes inside a box (leaning bars); clip: an extra 'L' mask (supersampled)"""
        m = Image.new('L', self.L[role].size, 0)
        dr = ImageDraw.Draw(m)
        hgt = y1 - y0
        x = x0 - hgt * 2
        while x < x1 + hgt:
            dr.polygon([((x) * SS, y1 * SS), ((x + period * duty) * SS, y1 * SS), ((x + period * duty + hgt * lean) * SS, y0 * SS), ((x + hgt * lean) * SS, y0 * SS)], fill=int(a * 255))
            x += period
        box = Image.new('L', m.size, 0)
        ImageDraw.Draw(box).rectangle([x0 * SS, y0 * SS, x1 * SS - 1, y1 * SS - 1], fill=255)
        m = ImageChops.multiply(m, box)
        if clip is not None: m = ImageChops.multiply(m, clip)
        self.L[role] = ImageChops.lighter(self.L[role], m)
    def disc_mask(self, r):
        m = Image.new('L', self.L['frame'].size, 0)
        c = self.w / 2
        ImageDraw.Draw(m).ellipse([(c - r) * SS, (c - r) * SS, (c + r) * SS, (c + r) * SS], fill=255)
        return m
    def knock(self, role, fn):
        """cut a shape out of a layer (text knocked out of a solid plate)"""
        m = Image.new('L', self.L[role].size, 0)
        fn(ImageDraw.Draw(m))
        self.L[role] = ImageChops.subtract(self.L[role], m)
    # slots -----------------------------------------------------------
    def slot(self, field, x, y, sheet, ink='digit', ghost=True, chars=None, align='l'):
        self.slots.append(('slot', field, x, y, sheet, ink, 1 if ghost else 0, chars or '', align))
    def pick(self, field, x, y, sheet, ink='label'):
        self.slots.append(('pick', field, x, y, sheet, ink))
    def bar(self, field, x, y, w, h, n, gap, ink='digit'):
        self.slots.append(('bar', field, x, y, w, h, n, gap, ink))

# ---------------------------------------------------------------- shapes
def hexh(x0, y0, x1, y1, cut=None):
    """a stretched hexagon pointing left and right"""
    cut = (y1 - y0) / 2 if cut is None else cut
    ym = (y0 + y1) / 2
    return [(x0, ym), (x0 + cut, y0), (x1 - cut, y0), (x1, ym), (x1 - cut, y1), (x0 + cut, y1)]
def octo(x0, y0, x1, y1, c):
    return [(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c), (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)]
def hexv(cx, cy, r):
    """a hexagon with flat top (honeycomb cell)"""
    return [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))) for a in range(0, 360, 60)]
def honey(f, role, cx, cy, r, cells, a=1.0, fill=0.0):
    for (i, j) in cells:
        x = cx + i * r * 1.5
        y = cy + j * r * math.sqrt(3) + (r * math.sqrt(3) / 2 if i % 2 else 0)
        f.poly(role, hexv(x, y, r - 1.2), fill=fill if fill else None, line=a, width=1.2)
def inset(pts, d):
    """an inset copy of a convex polygon (each edge moved inwards by d)"""
    n = len(pts)
    cx = sum(p[0] for p in pts) / n; cy = sum(p[1] for p in pts) / n
    lines = []
    for i in range(n):
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        nx, ny = y1 - y0, -(x1 - x0)
        ln = math.hypot(nx, ny); nx /= ln; ny /= ln
        if (cx - x0) * nx + (cy - y0) * ny < 0: nx, ny = -nx, -ny
        lines.append(((x0 + nx * d, y0 + ny * d), (x1 + nx * d, y1 + ny * d)))
    out = []
    for i in range(n):
        (a, b), (c, e) = lines[i - 1], lines[i]
        x1, y1 = a; x2, y2 = b; x3, y3 = c; x4, y4 = e
        den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
        t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / den
        out.append((x1 + t * (x2 - x1), y1 + t * (y2 - y1)))
    return out
def panel(f, pts, fill=0.10, width=1.6, double=0, role='frame'):
    f.poly(role, pts, fill=fill, line=1.0, width=width)
    if double: f.poly(role, inset(pts, double), line=0.55, width=1.0)
def brackets(f, x0, y0, x1, y1, s=8, a=1.0, w=1.4):
    for (x, y, dx, dy) in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        f.line('frame', [(x + dx * s, y), (x, y), (x, y + dy * s)], a, w)
def slashes(f, role, x, y, n, h=8, w=5, gap=4, a=1.0, lean=4):
    for i in range(n):
        xx = x + i * (w + gap)
        f.poly(role, [(xx, y + h), (xx + w, y + h), (xx + w + lean, y), (xx + lean, y)], fill=a)
def ruler(f, x0, x1, y, n, a=0.7):
    f.line('frame', [(x0, y), (x1, y)], a, 1)
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i / n
        f.line('frame', [(x, y), (x, y - (5 if i % 5 == 0 else 2.5))], a, 1)
def bilabel(f, x, y, zh, en, zpx, epx, role='label', anchor='m', kind='sans', gap=None, a=1.0, box=None, axes='v'):
    """a fixed bilingual label: Chinese above, small English below, tight leading (baselines, no font line gap); anchor l / m / r on x,
    y is the top of the Chinese ink; the pair is about zpx * 0.88 + epx * 0.74 + gap tall"""
    if box: f.checks.append(('%s/%s' % (zh, en), role, box, axes))
    zb = y + zpx * 0.88
    if gap is None: gap = zpx * 0.13 + 1                  # the Chinese ink reaches ~0.12 em under its baseline
    f.text(role, x, zb, zh, kind, zpx, anchor + 's', a)
    f.text(role, x, zb + gap + epx * 0.74, en, 'cond', epx, anchor + 's', a * 0.9, spacing=0.4)
def biline(f, x, y, zh, en, zpx, epx, role='label', a=1.0, kind='sans', box=None, axes='v'):
    """Chinese and English on one line, separated by a short bar, left anchored at x, middle at y"""
    if box: f.checks.append(('%s/%s' % (zh, en), role, box, axes))
    fz = font(kind, zpx)
    w = fz.getlength(zh) / SS
    f.text(role, x, y, zh, kind, zpx, 'lm', a)
    f.line(role, [(x + w + 4, y - zpx * 0.45), (x + w + 4, y + zpx * 0.45)], a * 0.8, 1)
    f.text(role, x + w + 8, y + 1, en, 'cond', epx, 'lm', a * 0.9, spacing=0.3)

# ---------------------------------------------------------------- seven-segment sheets
SEG = {'0': 'abcdef', '1': 'bc', '2': 'abged', '3': 'abgcd', '4': 'fgbc', '5': 'afgcd', '6': 'afgedc', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg', '-': 'g', ' ': ''}
GLYPHS = '0123456789:.- '
def seg_metrics(h):
    w = round(h * 0.54); t = max(2.0, h * 0.13); adv = w + max(2, round(h * 0.09))
    nar = max(4, round(h * 0.22))
    return w, t, adv, nar
def seg_sheet(h):
    """one row of cells (cell width = digit width); advance per glyph in the face.txt line; drawn with the same skew everywhere"""
    w, t, adv, nar = seg_metrics(h)
    pad = round(h * 0.08) + 1                              # the skew leans the top right
    cw = w + pad * 2
    im = Image.new('L', (cw * SS * len(GLYPHS), h * SS), 0)
    dr = ImageDraw.Draw(im)
    sk = 0.085
    g = max(0.8, t * 0.18)                                 # the gap between segments
    def P(gx, x, y):
        return ((gx + pad + x + (h / 2 - y) * sk) * SS, y * SS)
    def hbar(gx, x0, x1, y):
        ht = t / 2
        dr.polygon([P(gx, x0 + g, y), P(gx, x0 + g + ht, y - ht), P(gx, x1 - g - ht, y - ht), P(gx, x1 - g, y), P(gx, x1 - g - ht, y + ht), P(gx, x0 + g + ht, y + ht)], fill=255)
    def vbar(gx, x, y0, y1):
        ht = t / 2
        dr.polygon([P(gx, x, y0 + g), P(gx, x + ht, y0 + g + ht), P(gx, x + ht, y1 - g - ht), P(gx, x, y1 - g), P(gx, x - ht, y1 - g - ht), P(gx, x - ht, y0 + g + ht)], fill=255)
    for i, c in enumerate(GLYPHS):
        gx = i * cw
        x0, x1 = t / 2, w - t / 2
        y0, ym, y1 = t / 2, h / 2, h - t / 2
        if c == ':':
            for yy in (h * 0.32, h * 0.70):
                r = t * 0.55
                cx = nar / 2
                dr.polygon([P(gx, cx - r, yy - r), P(gx, cx + r, yy - r), P(gx, cx + r, yy + r), P(gx, cx - r, yy + r)], fill=255)
            continue
        if c == '.':
            r = t * 0.55; cx = nar / 2; yy = h - r
            dr.polygon([P(gx, cx - r, yy - r), P(gx, cx + r, yy - r), P(gx, cx + r, yy + r), P(gx, cx - r, yy + r)], fill=255)
            continue
        for s in SEG[c]:
            if s == 'a': hbar(gx, x0, x1, y0)
            if s == 'g': hbar(gx, x0, x1, ym)
            if s == 'd': hbar(gx, x0, x1, y1)
            if s == 'f': vbar(gx, x0, y0, ym)
            if s == 'e': vbar(gx, x0, ym, y1)
            if s == 'b': vbar(gx, x1, y0, ym)
            if s == 'c': vbar(gx, x1, ym, y1)
    im = im.resize((cw * len(GLYPHS), h), Image.LANCZOS)
    advs = [nar if c in ':.' else adv for c in GLYPHS]
    return im, cw, advs, pad

def text_sheet(cells, w, h, draw):
    """pick sheets: cells stacked vertically, each w x h, drawn by draw(dr, i, cell_value, x0, y0) at SS"""
    im = Image.new('L', (w * SS, h * SS * len(cells)), 0)
    dr = ImageDraw.Draw(im)
    for i, c in enumerate(cells): draw(dr, i, c, 0, i * h * SS)
    return im.resize((w, h * len(cells)), Image.LANCZOS)

def t_draw(dr, x, y, s, kind, px, anchor='mm', a=255):
    dr.text((x, y), s, font=font(kind, px), fill=a, anchor=anchor)

def sheet_bi(cells, w, h, zpx, epx, plate=None, up=0):
    """bilingual state plates: (zh, en, style) per cell; style 'solid' = a filled plate with the words knocked out, 'line' = an outlined plate,
    '' = words only"""
    def draw(dr, i, c, x0, y0):
        zh, en, style = c
        W, H = w * SS, h * SS
        pts = [(x0 + H * 0.3, y0 + 1 * SS), (x0 + W - H * 0.3, y0 + 1 * SS), (x0 + W - 1 * SS, y0 + H / 2), (x0 + W - H * 0.3, y0 + H - 1 * SS), (x0 + H * 0.3, y0 + H - 1 * SS), (x0 + 1 * SS, y0 + H / 2)]
        ink = 255
        if style == 'solid': dr.polygon(pts, fill=255); ink = 0
        elif style == 'line': dr.line(pts + [pts[0]], fill=200, width=int(1.2 * SS))
        cy = y0 + H / 2 - up * SS       # up: the words sit this many px above the plate's middle (measured: the anchor's middle is the font's, the ink's is lower; tools/check/check_face_centering.py)
        if zh and en:
            fz = font('sans', zpx); fe = font('cond', epx)
            wz = fz.getlength(zh); we = fe.getlength(en)
            tot = wz + 4 * SS + we
            xs = x0 + (W - tot) / 2
            dr.text((xs, cy), zh, font=fz, fill=ink, anchor='lm')
            dr.text((xs + wz + 4 * SS, cy + 0.5 * SS), en, font=fe, fill=ink, anchor='lm')
        elif zh: dr.text((x0 + W / 2, cy), zh, font=font('sans', zpx), fill=ink, anchor='mm')
        else: dr.text((x0 + W / 2, cy), en, font=font('cond', epx), fill=ink, anchor='mm')
    im = text_sheet(cells, w, h, draw)
    im.plates = [('%s/%s' % (c[0], c[1]), i, (round(h * 0.3), 2, w - round(h * 0.3), h - 2), 'dark' if c[2] == 'solid' else 'light', (0, 0, w, h)) for i, c in enumerate(cells)]
    im.cell_h = h
    return im

def sheet_stack(cells, w, h, zpx, epx):
    """two-line cells: Chinese above, English below (weekday)"""
    def draw(dr, i, c, x0, y0):
        zh, en = c
        dr.text((x0 + w * SS / 2, y0 + h * SS * 0.33), zh, font=font('sans', zpx), fill=255, anchor='mm')
        dr.text((x0 + w * SS / 2, y0 + h * SS * 0.80), en, font=font('cond', epx), fill=255, anchor='mm')
    return text_sheet(cells, w, h, draw)

def sheet_lamp(cells, w, h, epx):
    """lamp + word: (en, lit)"""
    def draw(dr, i, c, x0, y0):
        en, lit = c
        s = h * SS * 0.5; cy = y0 + h * SS / 2; lx = x0 + 2 * SS
        box = [lx, cy - s / 2, lx + s, cy + s / 2]
        if lit: dr.rectangle(box, fill=255)
        else: dr.rectangle(box, outline=200, width=int(1.2 * SS))
        dr.text((lx + s + 4 * SS, cy + 0.5 * SS), en, font=font('cond', epx), fill=255, anchor='lm')
    return text_sheet(cells, w, h, draw)

WDAY = [('週一', 'MON'), ('週二', 'TUE'), ('週三', 'WED'), ('週四', 'THU'), ('週五', 'FRI'), ('週六', 'SAT'), ('週日', 'SUN')]

# ---------------------------------------------------------------- faces
def warn_plate(f, pts, cx, top, zpx=17, epx=9):
    """the warning header: a solid plate with the words knocked out (dark on red), shown only for a real state"""
    f.poly('warn', pts, fill=1.0)
    fz, fe = font('serif', zpx), font('cond', epx)
    def k(dr):
        zb = top + zpx * 0.88
        dr.text((cx * SS, zb * SS), '警戒', font=fz, fill=255, anchor='ms')
        dr.text((cx * SS, (zb + zpx * 0.13 + 1 + epx * 0.74) * SS), 'WARNING', font=fe, fill=255, anchor='ms')
    f.knock('warn', k)

def segw(h, chars):
    w, t, adv, nar = seg_metrics(h)
    return sum(nar if c in ':.' else adv for c in chars) - (adv - w)

def centred(f, field, cx, y, h, chars, ink='digit'):
    f.slot(field, round(cx - segw(h, chars) / 2), y, 'seg%d' % h, ink, chars=chars)

# sizes of the Chinese part of the fixed labels (the Chinese clearly bigger than the English, which stays small)
ZR_TAG, ZR_STRIP, ZR_CELL, ZR_MINI = 13, 15, 13, 13        # round: panel tag, strip label, honeycomb cell label, unit words (秒, 時)
ZS_TAG, ZS_STRIP, ZS_CELL, ZS_MINI = 17, 21, 16, 18        # square

def round_base(f, zh, en, band=(18, 30, 322, 334)):
    """the round board's shared frame: rim ticks, hazard bands, the header plate (normal / warning), honeycomb clusters"""
    c = 180
    f.circle('frame', c, c, 178, 1.0, 1.6)
    f.circle('frame', c, c, 172, 0.35, 1.0)
    for i in range(60):
        a = math.radians(i * 6)
        r0 = 164 if i % 5 == 0 else 167
        f.line('frame', [(c + r0 * math.sin(a), c - r0 * math.cos(a)), (c + 171 * math.sin(a), c - 171 * math.cos(a))], 0.9 if i % 5 == 0 else 0.45, 1.2)
    disc = f.disc_mask(160)
    t0, t1, b0, b1 = band
    f.stripes('accent', 0, t0, 360, t1, period=10, a=0.95, clip=disc)
    f.stripes('accent', 0, b0, 360, b1, period=10, a=0.95, clip=disc)
    for y in (t0 - 1, t1 + 1, b0 - 1, b1 + 1):
        hw = math.sqrt(max(0, 160 ** 2 - (y - c) ** 2))
        f.line('frame', [(c - hw, y), (c + hw, y)], 0.8, 1)
    hp = [(110, 34), (250, 34), (264, 54), (250, 74), (110, 74), (96, 54)]
    f.poly('frame', hp, fill=0.16, line=1.0, width=1.6)
    bilabel(f, 180, 38, zh, en, 22, 10, role='head', kind='serif', box=(110, 34, 250, 74), axes='hv')
    warn_plate(f, hp, 180, 38, 22, 10)
    slashes(f, 'accent', 68, 49, 3, h=10, w=6, gap=3)
    slashes(f, 'accent', 266, 49, 3, h=10, w=6, gap=3)
    honey(f, 'frame', 60, 74, 7, [(0, 0), (1, -1), (1, 0)], a=0.7)
    honey(f, 'frame', 288, 74, 7, [(0, 0), (1, -1), (1, 0)], a=0.7)

def round_panel(f, y0=80, y1=206, tag=('現在時刻', 'CURRENT TIME')):
    pts = hexh(16, y0, 344, y1, cut=26)
    panel(f, pts, fill=0.08, width=1.8, double=4)
    fz = font('sans', ZR_TAG); fe = font('cond', 7)
    tw = (fz.getlength(tag[0]) + fe.getlength(tag[1])) / SS + 8
    f.poly('frame', [(180 - tw / 2 - 14, y0 - 1), (180 + tw / 2 + 14, y0 - 1), (180 + tw / 2 + 5, y0 + 16), (180 - tw / 2 - 5, y0 + 16)], fill=0.5)
    biline(f, 180 - tw / 2, y0 + 7.5, tag[0], tag[1], ZR_TAG, 7, role='label', a=1.0, box=(180 - tw / 2 - 5, y0 - 1, 180 + tw / 2 + 5, y0 + 16), axes='hv')
    f.circle('frame', 27, (y0 + y1) / 2, 2.0, 0.0, 0, fill=0.9)
    f.circle('frame', 333, (y0 + y1) / 2, 2.0, 0.0, 0, fill=0.9)

def round_strip(f, y0, y1, zh, en):
    sp = hexh(36, y0, 324, y1, cut=12)
    panel(f, sp, fill=0.12, width=1.4)
    bilabel(f, 64, y0 + 2, zh, en, ZR_STRIP, 7, box=(48, y0, 82, y1), axes='v')
    f.line('frame', [(82, y0 + 4), (82, y1 - 4)], 0.6, 1)

HC_R = [(56, 148, 238, 282), (141, 233, 260, 304), (226, 318, 238, 282)]   # the round honeycomb: left, lower middle, right (x0 x1 y0 y1)
def round_honey(f, labels):
    """three hexagon cells locked into each other (honeycomb, the middle one half a cell lower), returns their boxes"""
    for (x0, x1, y0, y1), (zh, en) in zip(HC_R, labels):
        panel(f, hexh(x0, y0, x1, y1, cut=12), fill=0.10, width=1.4)
        biline(f, x0 + 14, y0 + 8.5, zh, en, ZR_CELL, 7, box=(x0 + 9, y0, x1 - 9, y0 + 17))
        f.line('frame', [(x0 + 9, y0 + 17), (x1 - 9, y0 + 17)], 0.45, 1)
    return HC_R

def round_lower3(f):
    """the honeycomb under the strip for features that are not the clock: the clock's time, sync, power"""
    c = round_honey(f, [('時刻', 'TIME'), ('同步', 'SYNC'), ('電源', 'POWER')])
    centred(f, 'clock_hm', (c[0][0] + c[0][1]) / 2, c[0][2] + 21, 20, '88:88', ink='value')
    f.pick('sync', c[1][0] + 14, c[1][2] + 24, 'sync_s', 'label')
    f.pick('power', c[2][0] + 14, c[2][2] + 24, 'power_s', 'label')

def face_round_clock():
    f = Face('rd_clock', 360, 360, True)
    round_base(f, '時刻表示', 'DIGITAL CLOCK')
    round_panel(f)
    f.slot('time_hm', 40, 98, 'seg84', 'digit', chars='88:88')
    f.text('label', 264, 103, '秒', 'sans', ZR_MINI, 'lt', 0.85); f.text('label', 280, 106, 'SEC', 'cond', 8, 'lt', 0.75)
    f.slot('time_s', 271, 142, 'seg40', 'digit', chars='88')
    ruler(f, 70, 290, 199, 44, 0.6)
    round_strip(f, 210, 236, '日期', 'DATE')
    f.slot('date', 90, 212, 'seg22', 'value', chars='8888-88-88')
    f.line('frame', [(258, 214), (258, 232)], 0.6, 1)
    f.pick('wday', 262, 209, 'wday_s', 'label')
    c = round_honey(f, [('同步', 'SYNC'), ('鬧鐘', 'ALARM'), ('電源', 'POWER')])
    f.pick('sync', c[0][0] + 14, c[0][2] + 24, 'sync_s', 'label')
    centred(f, 'alarm_hm', (c[1][0] + c[1][1]) / 2, c[1][2] + 21, 20, '88:88', ink='value')
    f.pick('power', c[2][0] + 14, c[2][2] + 24, 'power_s', 'label')
    return f

def face_round_sw():
    f = Face('rd_sw', 360, 360, True)
    round_base(f, '碼錶', 'STOPWATCH')
    round_panel(f, tag=('經過時間', 'ELAPSED'))
    f.slot('main', 40, 98, 'seg84', 'digit', chars='88:88')
    f.text('label', 262, 108, '1/100', 'cond', 8, 'lt', 0.8); f.text('label', 293, 103, '秒', 'sans', ZR_MINI, 'lt', 0.85)
    f.slot('cc', 271, 142, 'seg40', 'digit', chars='88')
    ruler(f, 70, 290, 199, 44, 0.6)
    round_strip(f, 210, 236, '圈數', 'LAPS')
    f.slot('laps', 90, 212, 'seg22', 'value', chars='88')
    biline(f, 124, 223, '時', 'HR', ZR_MINI + 2, 7)
    f.slot('hours', 156, 212, 'seg22', 'value', chars='88')
    f.line('frame', [(196, 214), (196, 232)], 0.6, 1)
    f.pick('state', 202, 215, 'swst_s', 'label')
    pts = octo(56, 240, 304, 314, 12)
    panel(f, pts, fill=0.08, width=1.4)
    for i in range(2):
        y = 244 + i * 26
        biline(f, 68, y + 11, '分段', 'SPLIT', 12, 6, a=0.8)
        f.slot('lap%d_no' % (i + 1), 128, y + 1, 'seg20', 'value', chars='88')
        f.slot('lap%d' % (i + 1), 164, y + 1, 'seg20', 'digit', chars='88:88.88')
        if i == 0: f.line('frame', [(66, y + 24), (294, y + 24)], 0.3, 1)
    f.pick('best', 264, 246, 'best_s', 'accent')
    return f

def face_round_cd():
    f = Face('rd_cd', 360, 360, True)
    round_base(f, '倒數計時', 'COUNTDOWN')
    round_panel(f, tag=('剩餘時間', 'REMAINING'))
    centred(f, 'main', 180, 94, 96, '88:88')
    f.bar('prog', 70, 194, 216, 6, 20, 2, 'accent')
    round_strip(f, 210, 236, '設定', 'SET')
    f.slot('set', 90, 212, 'seg22', 'value', chars='88:88')
    f.line('frame', [(176, 214), (176, 232)], 0.6, 1)
    f.pick('state', 184, 215, 'cdst_s', 'label')
    round_lower3(f)
    return f

def face_round_pomo():
    f = Face('rd_pomo', 360, 360, True)
    round_base(f, '番茄鐘', 'POMODORO')
    round_panel(f, tag=('專注計時', 'FOCUS TIMER'))
    centred(f, 'main', 180, 94, 96, '88:88')
    f.bar('prog', 70, 194, 216, 6, 20, 2, 'accent')
    round_strip(f, 210, 236, '階段', 'PHASE')
    f.pick('phase', 88, 215, 'phase_s', 'label')
    f.line('frame', [(200, 214), (200, 232)], 0.6, 1)
    biline(f, 204, 223, '完成', 'DONE', ZR_MINI + 1, 7)
    f.slot('round', 266, 212, 'seg22', 'value', chars='88')
    round_lower3(f)
    return f

# ---- the square board (FakePod 460 x 460)
def sq_base(f, tag, head):
    W = 460
    f.stripes('accent', 0, 4, W, 58, period=22, duty=0.5, a=0.9)
    f.stripes('accent', 0, 440, W, 456, period=22, duty=0.5, a=0.9)
    for y in (3, 59, 439, 457): f.line('frame', [(0, y), (W, y)], 0.7, 1)
    f.knock('accent', lambda dr: (dr.rectangle([8 * SS, 6 * SS, 206 * SS, 56 * SS], fill=255), dr.rectangle([256 * SS, 6 * SS, 452 * SS, 56 * SS], fill=255)))
    for (x0, x1) in ((10, 204), (258, 450)):
        f.d('frame').rectangle([x0 * SS, 8 * SS, x1 * SS - 1, 54 * SS - 1], fill=255)
        f.d('frame').rectangle([(x0 + 2) * SS, 10 * SS, (x1 - 2) * SS - 1, 52 * SS - 1], fill=int(0.18 * 255))
    bilabel(f, 107, 12, tag[0], tag[1], 26, 11, kind='serif', box=(12, 10, 202, 52), axes='hv')
    bilabel(f, 286, 16, '電力', 'BATTERY', 21, 9, kind='serif', box=(260, 10, 312, 52), axes='hv')
    f.line('frame', [(312, 14), (312, 48)], 0.7, 1.2)
    f.slot('batt', 318, 13, 'seg36', 'digit', chars='888')
    f.text('label', 383, 40, '%', 'cond', 20, 'ls', 1.0)
    f.poly('frame', [(402, 18), (440, 18), (440, 25), (444, 25), (444, 37), (440, 37), (440, 44), (402, 44)], line=1.0, width=1.4)
    f.bar('battbar', 405, 22, 31, 19, 5, 1, 'digit')
    hp = [(144, 64), (316, 64), (332, 87), (316, 110), (144, 110), (128, 87)]
    f.poly('frame', hp, fill=0.16, line=1.0, width=1.8)
    bilabel(f, 230, 67, head[0], head[1], 27, 12, role='head', kind='serif', box=(144, 64, 316, 110), axes='hv')
    warn_plate(f, hp, 230, 67, 27, 12)
    slashes(f, 'accent', 72, 80, 4, h=13, w=8, gap=5, lean=6)
    slashes(f, 'accent', 340, 80, 4, h=13, w=8, gap=5, lean=6)
    honey(f, 'frame', 24, 90, 10, [(0, 0), (1, 0), (1, -1)], a=0.75)
    honey(f, 'frame', 414, 90, 10, [(0, 0), (1, 0), (1, -1)], a=0.75)

def sq_frame(f, tag, y0=116, y1=302):
    pts = hexh(10, y0, 450, y1, cut=30)
    panel(f, pts, fill=0.08, width=2.2, double=5)
    fz = font('sans', ZS_TAG); fe = font('cond', 9)
    tw = (fz.getlength(tag[0]) + fe.getlength(tag[1])) / SS + 10
    f.poly('frame', [(230 - tw / 2 - 18, y0 - 1), (230 + tw / 2 + 18, y0 - 1), (230 + tw / 2 + 7, y0 + 21), (230 - tw / 2 - 7, y0 + 21)], fill=0.55)
    biline(f, 230 - tw / 2, y0 + 10, tag[0], tag[1], ZS_TAG, 9, box=(230 - tw / 2 - 7, y0 - 1, 230 + tw / 2 + 7, y0 + 21), axes='hv')
    for x in (24, 436):
        f.line('frame', [(x - 5, (y0 + y1) / 2), (x + 5, (y0 + y1) / 2)], 0.9, 1.2)
        f.line('frame', [(x, (y0 + y1) / 2 - 5), (x, (y0 + y1) / 2 + 5)], 0.9, 1.2)

def sq_strip(f, zh, en, y0=306, y1=342):
    sp = hexh(30, y0, 430, y1, cut=14)
    panel(f, sp, fill=0.12, width=1.6)
    bilabel(f, 72, y0 + 3, zh, en, ZS_STRIP, 9, box=(44, y0, 96, y1), axes='v')
    f.line('frame', [(96, y0 + 5), (96, y1 - 5)], 0.6, 1.2)

HC_S = [(4, 162, 346, 400), (147, 305, 375, 429), (290, 448, 346, 400)]   # the square honeycomb (x0 x1 y0 y1)
def sq_honey(f, labels):
    """three hexagon cells locked into each other under the strip (the middle one half a cell lower) and small honeycombs in the gaps"""
    for (x0, x1, y0, y1), (zh, en) in zip(HC_S, labels):
        panel(f, hexh(x0, y0, x1, y1, cut=16), fill=0.10, width=1.6)
        biline(f, x0 + 20, y0 + 10, zh, en, ZS_CELL, 9, box=(x0 + 14, y0, x1 - 14, y0 + 20))
        f.line('frame', [(x0 + 14, y0 + 20), (x1 - 14, y0 + 20)], 0.45, 1)
    honey(f, 'frame', 40, 416, 7, [(0, 0), (1, 0), (2, 0)], a=0.6)
    honey(f, 'frame', 390, 416, 7, [(0, 0), (1, 0), (2, 0)], a=0.6)
    return HC_S

def sq_lower3(f):
    c = sq_honey(f, [('時刻', 'TIME'), ('同步', 'SYNC'), ('電源', 'POWER')])
    centred(f, 'clock_hm', (c[0][0] + c[0][1]) / 2, c[0][2] + 23, 28, '88:88', ink='value')
    f.pick('sync', c[1][0] + 30, c[1][2] + 28, 'sync_l', 'label')
    f.pick('power', c[2][0] + 30, c[2][2] + 28, 'power_l', 'label')

def face_sq_clock():
    f = Face('sq_clock', 460, 460, False)
    sq_base(f, ('時刻模式', 'CLOCK MODE'), ('時刻表示', 'DIGITAL CLOCK'))
    sq_frame(f, ('現在時刻', 'CURRENT TIME'))
    f.slot('time_hm', 40, 140, 'seg112', 'digit', chars='88:88')
    f.text('label', 344, 147, '秒', 'sans', ZS_MINI, 'lt', 0.85); f.text('label', 366, 152, 'SEC', 'cond', 11, 'lt', 0.75)
    f.slot('time_s', 348, 200, 'seg52', 'digit', chars='88')
    sq_strip(f, '日期', 'DATE')
    f.slot('date', 108, 310, 'seg28', 'value', chars='8888-88-88')
    f.line('frame', [(344, 311), (344, 337)], 0.6, 1.2)
    f.pick('wday', 352, 307, 'wday_l', 'label')
    c = sq_honey(f, [('鬧鐘', 'ALARM'), ('同步', 'SYNC'), ('狀態', 'STATUS')])
    f.pick('alarm', c[0][0] + 22, c[0][2] + 28, 'onoff_l', 'label')
    f.slot('alarm_hm', c[0][0] + 68, c[0][2] + 23, 'seg28', 'value', chars='88:88')
    f.pick('sync', c[1][0] + 30, c[1][2] + 28, 'sync_l', 'label')
    f.pick('state', c[2][0] + 22, c[2][2] + 26, 'clkst_l', 'label')
    return f

def face_sq_sw():
    f = Face('sq_sw', 460, 460, False)
    sq_base(f, ('計時模式', 'TIMER MODE'), ('碼錶', 'STOPWATCH'))
    sq_frame(f, ('經過時間', 'ELAPSED'))
    f.slot('main', 40, 140, 'seg112', 'digit', chars='88:88')
    f.text('label', 340, 153, '1/100', 'cond', 11, 'lt', 0.8); f.text('label', 378, 147, '秒', 'sans', ZS_MINI, 'lt', 0.85)
    f.slot('cc', 348, 200, 'seg52', 'digit', chars='88')
    sq_strip(f, '圈數', 'LAPS')
    f.slot('laps', 108, 310, 'seg28', 'value', chars='88')
    biline(f, 150, 324, '時', 'HR', ZS_MINI, 9)
    f.slot('hours', 196, 310, 'seg28', 'value', chars='88')
    f.line('frame', [(246, 311), (246, 337)], 0.6, 1.2)
    f.pick('state', 262, 312, 'swst_l', 'label')
    pts = octo(30, 348, 430, 434, 14)
    panel(f, pts, fill=0.08, width=1.6)
    for i in range(3):
        y = 352 + i * 27
        biline(f, 46, y + 13, '分段', 'SPLIT', 16, 8, a=0.8)
        f.text('label', 126, y + 13, '#', 'cond', 14, 'mm', 0.7)
        f.slot('lap%d_no' % (i + 1), 134, y + 2, 'seg22', 'value', chars='88')
        f.slot('lap%d' % (i + 1), 184, y + 2, 'seg22', 'digit', chars='88:88.88')
        if i < 2: f.line('frame', [(44, y + 26), (416, y + 26)], 0.3, 1)
    f.pick('best', 340, 353, 'best_l', 'accent')
    return f

def face_sq_cd():
    f = Face('sq_cd', 460, 460, False)
    sq_base(f, ('計時模式', 'TIMER MODE'), ('倒數計時', 'COUNTDOWN'))
    sq_frame(f, ('剩餘時間', 'REMAINING'))
    centred(f, 'main', 230, 134, 132, '88:88')
    f.bar('prog', 76, 276, 300, 10, 20, 3, 'accent')
    sq_strip(f, '設定', 'SET')
    f.slot('set', 108, 310, 'seg28', 'value', chars='88:88')
    f.line('frame', [(220, 311), (220, 337)], 0.6, 1.2)
    f.pick('state', 236, 311, 'cdst_l', 'label')
    sq_lower3(f)
    return f

def face_sq_pomo():
    f = Face('sq_pomo', 460, 460, False)
    sq_base(f, ('專注模式', 'FOCUS MODE'), ('番茄鐘', 'POMODORO'))
    sq_frame(f, ('專注計時', 'FOCUS TIMER'))
    centred(f, 'main', 230, 134, 132, '88:88')
    f.bar('prog', 76, 276, 300, 10, 20, 3, 'accent')
    sq_strip(f, '階段', 'PHASE')
    f.pick('phase', 108, 311, 'phase_l', 'label')
    f.line('frame', [(272, 311), (272, 337)], 0.6, 1.2)
    biline(f, 280, 324, '完成', 'DONE', ZS_MINI, 9)
    f.slot('round', 352, 310, 'seg28', 'value', chars='88')
    sq_lower3(f)
    return f

# ---------------------------------------------------------------- pack
def rle(im):
    w, h = im.size
    px = im.tobytes()
    out = bytearray(b'JDL1' + struct.pack('<HH', w, h))
    i, n = 0, len(px)
    while i < n:
        v = px[i]
        if v < 3 or v > 252:
            full = v > 252
            j = i
            while j < n and j - i < 64 and ((px[j] > 252) if full else (px[j] < 3)): j += 1
            out.append((0x40 if full else 0) + (j - i - 1))
            i = j
            continue
        q = v // 2
        j = i
        while j < n and j - i < 256 and 2 < px[j] < 253 and px[j] // 2 == q: j += 1
        if j - i >= 3:
            out += bytes((0xff, j - i - 1, q * 2))
            i = j
        else:
            out.append(0x80 | min(q, 0x7e))
            i += 1
    return bytes(out)

def crop_layer(im):
    bb = im.getbbox()
    if not bb: return None, 0, 0
    return im.crop(bb), bb[0], bb[1]

SHEETS = {}
def best_sheet(w, rowh, rows, epx, cells=None):
    """the best-lap marker: cell 0 empty, cell i a BEST tag on row i (cells past the rows shown stay empty)"""
    def draw(dr, i, c, x0, y0):
        if not i or i > rows: return
        y = y0 + ((i - 1) * rowh) * SS
        H = (rowh - 2) * SS
        pts = [(x0 + 4 * SS, y), (x0 + w * SS, y), (x0 + w * SS - 4 * SS, y + H), (x0, y + H)]
        dr.polygon(pts, fill=255)
        dr.text((x0 + w * SS / 2, y + H / 2), 'BEST', font=font('cond', epx), fill=0, anchor='mm')
    return text_sheet(list(range(cells or rows + 1)), w, rowh * rows, draw)

def build_sheets():
    for h in (20, 22, 28, 36, 40, 52, 84, 96, 112, 132):
        im, cw, advs, pad = seg_sheet(h)
        SHEETS['seg%d' % h] = (im, 'glyph', cw, h, GLYPHS, advs, pad)
    def pk(name, im, w, h, n): SHEETS[name] = (im, 'pick', w, h, n, None, 0)
    wd = WDAY + [('－－', '---')]
    pk('wday_s', sheet_stack(wd, 52, 26, 14, 8), 52, 26, 8)
    pk('wday_l', sheet_stack(wd, 70, 34, 21, 10), 70, 34, 8)
    pk('onoff_s', sheet_bi([('', 'OFF', 'line'), ('', 'ON', 'solid')], 30, 14, 8, 8), 30, 14, 2)
    pk('onoff_l', sheet_bi([('', 'OFF', 'line'), ('', 'ON', 'solid')], 42, 20, 11, 11), 42, 20, 2)
    pk('sync_s', sheet_lamp([('NO SYNC', False), ('SYNCED', True)], 66, 16, 9), 66, 16, 2)
    pk('sync_l', sheet_lamp([('NO SYNC', False), ('SYNCED', True)], 104, 20, 13), 104, 20, 2)
    pk('power_s', sheet_lamp([('---', False), ('USB', True), ('BATTERY', True)], 66, 16, 9), 66, 16, 3)
    pk('power_l', sheet_lamp([('---', False), ('USB', True), ('BATTERY', True)], 104, 20, 13), 104, 20, 3)
    sw = [('停止', 'STOP', 'line'), ('計時', 'RUN', 'solid'), ('暫停', 'PAUSE', 'line')]
    cd = [('待命', 'READY', 'line'), ('倒數', 'RUN', 'solid'), ('暫停', 'PAUSE', 'line'), ('時間到', 'TIME UP', 'solid')]
    ph = [('待機', 'IDLE', 'line'), ('專注', 'FOCUS', 'solid'), ('休息', 'BREAK', 'solid'), ('長休', 'LONG BREAK', 'solid'), ('暫停', 'PAUSED', 'line')]
    ck = [('正常', 'NORMAL', 'line'), ('警戒', 'ALERT', 'solid'), ('鬧鐘', 'ALARM', 'solid')]
    pk('swst_s', sheet_bi(sw, 104, 18, 14, 8, up=0.5), 104, 18, 3)
    pk('swst_l', sheet_bi(sw, 160, 26, 21, 11, up=1), 160, 26, 3)
    pk('cdst_s', sheet_bi(cd, 132, 18, 14, 8, up=0.5), 132, 18, 4)
    pk('cdst_l', sheet_bi(cd, 180, 26, 21, 11, up=1), 180, 26, 4)
    pk('phase_s', sheet_bi(ph, 108, 18, 14, 8, up=0.5), 108, 18, 5)
    pk('phase_l', sheet_bi(ph, 156, 26, 21, 11, up=1), 156, 26, 5)
    pk('clkst_l', sheet_bi(ck, 116, 24, 19, 10, up=1), 116, 24, 3)
    pk('best_s', best_sheet(34, 26, 2, 8, cells=4), 34, 52, 4)
    pk('best_l', best_sheet(52, 27, 3, 11), 52, 81, 4)

def face_txt(f, files):
    L = ['# JukeDial designed face v1 (tools/gen/make_faces.py; format: the face.txt description in make_faces.py)', 'face %s %d %d %s' % (f.name, f.w, f.h, 'round' if f.round else 'square')]
    for role, fn, x, y in files: L.append('layer %s %s %d %d' % (role, fn, x, y))
    for s in f.slots:
        if s[0] == 'slot': L.append('slot %s %d %d %s %s ghost=%d chars=%s' % (s[1], s[2], s[3], s[4], s[5], s[6], s[7]))
        elif s[0] == 'pick': L.append('pick %s %d %d %s %s' % s[1:])
        else: L.append('bar %s %d %d %d %d %d %d %s' % s[1:])
    return '\n'.join(L) + '\n'

def sheets_txt():
    L = ['# shared sheets: sheet <name> <file> glyph <cell_w> <h> <pad> <glyphs> <adv,...> | sheet <name> <file> pick <w> <h> <n>']
    for k, (im, kind, w, h, n, advs, pad) in SHEETS.items():
        if kind == 'glyph': L.append('sheet %s %s.jdl glyph %d %d %d %s %s' % (k, k, w, h, pad, n.replace(' ', '_'), ','.join(map(str, advs))))
        else: L.append('sheet %s %s.jdl pick %d %d %d' % (k, k, w, h, n))
    return '\n'.join(L) + '\n'

FACES = [face_round_clock, face_round_sw, face_round_cd, face_round_pomo, face_sq_clock, face_sq_sw, face_sq_cd, face_sq_pomo]

def build():
    build_sheets()
    files = {}
    faces = []
    for fn in FACES:
        f = fn()
        lay = []
        for r in ROLES:
            im = f.L[r].resize((f.w, f.h), Image.LANCZOS)
            f.L[r] = im
            c, x, y = crop_layer(im)
            if c is None: continue
            files['%s/%s.jdl' % (f.name, r)] = rle(c)
            lay.append((r, '%s.jdl' % r, x, y))
        files['%s/face.txt' % f.name] = face_txt(f, lay).encode()
        faces.append(f)
    used = {sl[4] for f in faces for sl in f.slots if sl[0] in ('slot', 'pick')}
    for k in [k for k in SHEETS if k not in used]: del SHEETS[k]       # only the sheets a face uses go into the pack
    for k, v in SHEETS.items(): files['sheets/%s.jdl' % k] = rle(v[0])
    files['sheets/sheets.txt'] = sheets_txt().encode()
    return faces, files

def pack(files):
    """one STORE-method zip (no compression: the firmware reads the bytes in place); sorted, fixed time stamps and attributes = deterministic"""
    import io, zipfile
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_STORED) as z:
        for n in sorted(files):
            zi = zipfile.ZipInfo(n, date_time=(2026, 1, 1, 0, 0, 0))
            zi.compress_type = zipfile.ZIP_STORED
            zi.create_system = 3
            zi.external_attr = 0o644 << 16
            z.writestr(zi, files[n])
    return buf.getvalue()

# ---------------------------------------------------------------- previews (the same compositing as ui/face_art.c)
SCHEMES = {   # bg frame accent label digit value warn  (face_art.c s_schemes, keep in step)
    'green': ((0x00, 0x0a, 0x03), (0x22, 0xd6, 0x4c), (0x2c, 0xf0, 0x5a), (0xa8, 0xff, 0xb8), (0x52, 0xff, 0x6e), (0x52, 0xff, 0x6e), (0xff, 0x2a, 0x10)),
    'red':   ((0x0c, 0x02, 0x00), (0xff, 0x40, 0x14), (0xff, 0x4c, 0x1c), (0xff, 0xa0, 0x78), (0xff, 0x5c, 0x22), (0xff, 0x7c, 0x44), (0xff, 0x2a, 0x10)),
    'cyan':  ((0x00, 0x07, 0x0b), (0x40, 0xc8, 0xe6), (0x40, 0xc8, 0xe6), (0xc8, 0xf6, 0xff), (0x9c, 0xf2, 0xff), (0x9c, 0xf2, 0xff), (0xff, 0x2a, 0x10)),
    'tri':   ((0x06, 0x03, 0x0c), (0x8e, 0x62, 0xd4), (0xff, 0x6c, 0x1c), (0xe8, 0xde, 0xff), (0xff, 0x7c, 0x20), (0x5c, 0xff, 0x5c), (0xff, 0x2a, 0x10)),
}
INK = {'frame': 1, 'accent': 2, 'label': 3, 'head': 3, 'warn': 6, 'digit': 4, 'value': 5}

def blend(base, mask, col, ox=0, oy=0, k=1.0):
    solid = Image.new('RGB', mask.size, col)
    if k != 1.0: mask = mask.point(lambda v: int(v * k))
    base.paste(solid, (ox, oy), mask)

def preview(f, scheme, vals, warn=False):
    sc = SCHEMES['red' if warn else scheme]
    im = Image.new('RGB', (f.w, f.h), sc[0])
    for r in ROLES:
        if r == 'head' and warn: continue
        if r == 'warn' and not warn: continue
        blend(im, f.L[r], sc[INK[r]])
    for s in f.slots:
        if s[0] == 'slot':
            _, field, x, y, sh, ink, ghost, chars, al = s
            sim, kind, cw, h, glyphs, advs, pad = SHEETS[sh]
            v = vals.get(field, chars.replace('8', '0'))
            def put(text, k):
                xx = x
                for ch in text:
                    gi = glyphs.index(ch) if ch in glyphs else glyphs.index(' ')
                    blend(im, sim.crop((gi * cw, 0, gi * cw + cw, h)), sc[INK[ink]], xx - pad, y, k)
                    xx += advs[gi]
            if ghost: put(chars, 0.10)
            put(v, 1.0)
        elif s[0] == 'pick':
            _, field, x, y, sh, ink = s
            sim, kind, w, h, n, _, _ = SHEETS[sh]
            i = max(0, min(n - 1, int(vals.get(field, 0))))
            blend(im, sim.crop((0, i * h, w, i * h + h)), sc[INK[ink]], x, y)
        else:
            _, field, x, y, w, h, n, gap, ink = s
            lit = int(vals.get(field, 0))
            seg = (w - (n - 1) * gap) // n
            dr = ImageDraw.Draw(im)
            col = sc[INK[ink]]
            for i in range(n):
                x0 = x + i * (seg + gap)
                c = col if i < lit else tuple(int(sc[0][k] + (col[k] - sc[0][k]) * 40 / 255) for k in range(3))
                dr.polygon([(x0, y + h - 1), (x0 + seg - 1, y + h - 1), (x0 + seg - 1 + h // 2, y), (x0 + h // 2, y)], fill=c)
    if f.round:
        m = Image.new('L', (f.w * 2, f.h * 2), 0)
        ImageDraw.Draw(m).ellipse([0, 0, f.w * 2 - 1, f.h * 2 - 1], fill=255)
        bg = Image.new('RGB', (f.w, f.h), (0, 0, 0)); bg.paste(im, (0, 0), m.resize((f.w, f.h), Image.LANCZOS)); im = bg
    return im

SAMPLE = {'time_hm': '12:34', 'time_s': '56', 'date': '2026-11-18', 'wday': 2, 'alarm': 1, 'alarm_hm': '07:30', 'sync': 1, 'power': 1,
          'batt': ' 87', 'battbar': 5, 'clock_hm': '12:34', 'state': 1, 'main': '04:27', 'cc': '58', 'hours': '00', 'laps': '03', 'set': '05:00',
          'prog': 13, 'lap1_no': '03', 'lap1': '01:28.40', 'lap2_no': '02', 'lap2': '01:31.07', 'lap3_no': '01', 'lap3': '01:28.11', 'best': 3,
          'phase': 1, 'round': '02'}

def guide(f, out):
    """the slot-grid guide for artists: the face at 2x, a 10 px grid, every slot's rectangle and field name"""
    im = preview(f, 'green', SAMPLE).resize((f.w * 2, f.h * 2), Image.NEAREST)
    dr = ImageDraw.Draw(im, 'RGBA')
    for x in range(0, f.w, 10): dr.line([(x * 2, 0), (x * 2, f.h * 2)], fill=(255, 255, 255, 40 if x % 50 else 90))
    for y in range(0, f.h, 10): dr.line([(0, y * 2), (f.w * 2, y * 2)], fill=(255, 255, 255, 40 if y % 50 else 90))
    lf = ImageFont.truetype(COND, 13)
    for s in f.slots:
        if s[0] == 'slot':
            _, field, x, y, sh, ink, ghost, chars, al = s
            im_, kind, cw, h, glyphs, advs, pad = SHEETS[sh]
            w = sum(advs[glyphs.index(c)] for c in chars)
            box, lab = (x, y, x + w, y + h), '%s %s (%s)' % (field, chars, sh)
        elif s[0] == 'pick':
            _, field, x, y, sh, ink = s
            box, lab = (x, y, x + SHEETS[sh][2], y + SHEETS[sh][3]), '%s (%s, %d)' % (field, sh, SHEETS[sh][4])
        else:
            _, field, x, y, w, h, n, gap, ink = s
            box, lab = (x, y, x + w, y + h), '%s bar %d' % (field, n)
        dr.rectangle([box[0] * 2, box[1] * 2, box[2] * 2, box[3] * 2], outline=(255, 0, 255, 255), width=2)
        dr.text((box[0] * 2 + 2, box[1] * 2 - 14), lab, font=lf, fill=(255, 120, 255, 255))
    im.save(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=OUT, help='the zip to write (default .local/faces.zip)')
    ap.add_argument('--png')
    ap.add_argument('--guide', help='DIR: the slot-grid guide images (docs/images/clock/face_guide_*.png)')
    a = ap.parse_args()
    faces, files = build()
    data = pack(files)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, 'wb') as fp: fp.write(data)
    print('%s: %d bytes, %d files' % (a.out, len(data), len(files)))
    for n in sorted(files): print('  %-32s %7d' % (n, len(files[n])))
    if a.png:
        os.makedirs(a.png, exist_ok=True)
        for f in faces:
            for sc in SCHEMES:
                preview(f, sc, SAMPLE).save(os.path.join(a.png, '%s_%s.png' % (f.name, sc)))
            preview(f, 'green', SAMPLE, warn=True).save(os.path.join(a.png, '%s_warn.png' % f.name))
    if a.guide:
        os.makedirs(a.guide, exist_ok=True)
        for f in faces:
            if f.name in ('rd_clock', 'sq_clock'): guide(f, os.path.join(a.guide, 'face_guide_%s.png' % f.name))

if __name__ == '__main__':
    main()
