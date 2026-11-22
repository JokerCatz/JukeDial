# SPDX-License-Identifier: LGPL-2.1-or-later
"""Glyph pack writer (format: an internal note). Same bytes as tools/web/glyphpack.js.

A pack is a zip: pack.bin (index), g0.bin..g3.bin (glyph bitmaps per size), img.bin (images), source.json (editor).
Pure Python (zipfile, struct); the renderers that make the bitmaps live in make_lists.py / the web page.
"""
import io, json, struct, zipfile

MAGIC = b"JGPK"
VERSION = 1
HEADER = 64
NL = 0xFFFE
NONE = 0xFFFF
KINDS = {"plain": 0, "lunch": 1, "people": 2, "columns": 3, "vocab": 4}
FMT_EMPTY, FMT_A8, FMT_RGB565A8 = 0, 1, 2
GF_SPACE, GF_WORD = 1, 2


def bpp(fmt):
    return {FMT_EMPTY: 0, FMT_A8: 1, FMT_RGB565A8: 3}[fmt]


class Node:
    def __init__(self, text=(), image=NONE, weight=1, flags=0, children=None):
        self.text, self.image, self.weight, self.flags = list(text), image, weight, flags
        self.children = children or []


def flatten(root):
    """Breadth first: node 0 is the root, every node's children are contiguous and after it."""
    order, q = [root], [root]
    first = {}
    while q:
        n = q.pop(0)
        first[id(n)] = len(order)
        order.extend(n.children)
        q.extend(n.children)
    return order, first


def pack_bin(kind, sizes, glyphs, images, root, ellipsis=NONE):
    """sizes: [(px, line_h)]; glyphs: [{'flags': f, 'sizes': [{'w','h','x','y','adv','fmt','data'}]}];
    images: [{'w','h','fmt','data'}]. Returns (pack.bin, [g0.bin, ...], img.bin)."""
    ns = len(sizes)
    assert 1 <= ns <= 4 and len(glyphs) <= 0xFFF0
    order, first = flatten(root)
    seq, node_recs = [], []
    for n in order:
        off = len(seq)
        seq.extend(n.text)
        nc = len(n.children)
        node_recs.append(struct.pack("<IHHIHBB", off, len(n.text), n.image, first[id(n)] if nc else 0, nc, n.weight, n.flags))
    blobs, grecs = [], []
    for s in range(ns):
        blob = bytearray()
        for g in glyphs:
            m = g["sizes"][s]
            assert len(m["data"]) == m["w"] * m["h"] * bpp(m["fmt"])
            grecs.append(struct.pack("<IBBbbBBBB", len(blob), m["w"], m["h"], m["x"], m["y"], m["adv"], m["fmt"], g["flags"], 0))
            blob += m["data"]
        blobs.append(bytes(blob))
    img, irecs = bytearray(), []
    for im in images:
        assert len(im["data"]) == im["w"] * im["h"] * bpp(im["fmt"])
        irecs.append(struct.pack("<IHHBBBB", len(img), im["w"], im["h"], im["fmt"], 0, 0, 0))
        img += im["data"]
    hdr = MAGIC + struct.pack("<HHBBHIIIII", VERSION, HEADER, kind, ns, ellipsis, len(glyphs), len(images), len(order), len(seq), 0)
    for s in range(4):
        px, lh = sizes[s] if s < ns else (0, 0)
        hdr += struct.pack("<HHI", px, lh, 0)
    assert len(hdr) == HEADER
    body = hdr + b"".join(grecs) + b"".join(irecs) + b"".join(node_recs) + struct.pack("<%dH" % len(seq), *seq)
    return body, blobs, bytes(img)


def write_zip(path_or_file, pack, blobs, img, source=None, deflate=False):
    """Stored by default (the device reads bitmaps lazily from stored entries)."""
    method = zipfile.ZIP_DEFLATED if deflate else zipfile.ZIP_STORED
    with zipfile.ZipFile(path_or_file, "w", method) as z:
        z.writestr("pack.bin", pack)
        for i, b in enumerate(blobs):
            z.writestr("g%d.bin" % i, b)
        if img:
            z.writestr("img.bin", img)
        if source is not None:
            z.writestr("source.json", json.dumps(source, ensure_ascii=False, indent=1).encode())


def build(kind, sizes, glyphs, images, root, ellipsis=NONE, source=None, deflate=False):
    f = io.BytesIO()
    write_zip(f, *pack_bin(kind, sizes, glyphs, images, root, ellipsis), source=source, deflate=deflate)
    return f.getvalue()
