#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Generate tiny SYNTHETIC tracker-module seeds for the xmp_fuzz harness (one file per format).

usage: make_seeds.py OUTDIR
The seeds are hand-built from the libxmp 4.7.2 loader sources; they are generated, never committed.
Each seed is meant to load (>=1 pattern with a few notes, >=1 tiny sample, >=1 order).

Not seeded (no seed written):
  not seeded: mfp (needs a companion sample file)
  not seeded: np3 (NoisePacker v3: packed track codes not worked out)
  not seeded: p40/p50a/p60a/p61a/theplayer (The Player family: large bit-packed formats)
  not seeded: pha, pm10c, pm18a, tp1, tp2, tp3, unic, unic2, xann, di, starpack, fuchs, ksm, crb
              (ProWizard packers whose depackers need long hand-made layouts; time-boxed out)
Partial (header-correct but the payload does not decode, so the loader refuses the file):
  partial: ice1/ice2 (Pack-Ice bit-reversed LZ stream is not encoded; garbage payload)
  partial: s404 (StoneCracker bit stream is not encoded; garbage payload)
  partial: pw_noiserun (passes the NoiseRunner test but the plain MOD loader claims it first)
"""
import os
import struct
import sys

P = struct.pack


def z(s, n):
    """Fixed-width NUL padded ASCII field."""
    b = s.encode() if isinstance(s, str) else s
    return b[:n].ljust(n, b"\0")


def sp(s, n):
    b = s.encode() if isinstance(s, str) else s
    return b[:n].ljust(n, b" ")


def smp8(n=64, signed=True):
    """A small triangle-ish 8-bit sample."""
    out = bytearray()
    for i in range(n):
        v = ((i * 8) % 128) - 64
        out.append(v & 0xFF if signed else (v + 128) & 0xFF)
    return bytes(out)


def smp16(n=32):
    return b"".join(P("<h", ((i * 1024) % 16384) - 8192) for i in range(n))


# Amiga period table helper (C-2.. for MOD cells)
PERIODS = [428, 381, 339, 320, 285, 254, 226, 214]


# ---------------------------------------------------------------- MOD family
def mod_cells(chn, rows=64, notes=4):
    out = bytearray()
    for r in range(rows):
        for c in range(chn):
            if r < notes * 4 and r % 4 == 0 and c < 2:
                per = PERIODS[(r // 4 + c) % 8]
                out += P(">I", (0 << 28) | (per << 16) | (1 << 12) | (0xC << 8) | 0x30)
            else:
                out += b"\0\0\0\0"
    return bytes(out)


def mod_samples(nsmp, wlen=32, loop=False):
    out = bytearray()
    for i in range(nsmp):
        if i == 0:
            out += z("seed", 22) + P(">HBBHH", wlen, 0, 64, 0 if not loop else 4, 1 if not loop else wlen - 4)
        else:
            out += z("", 22) + P(">HBBHH", 0, 0, 0, 0, 1)
    return bytes(out)


def mod_generic(magic, chn, nsmp=31, npat=1):
    h = z("seed " + magic.decode("latin1"), 20) + mod_samples(nsmp)
    h += P("BB", 1, 127) + bytes([0] * 128)
    if nsmp == 31:
        h += magic
    body = mod_cells(chn) * npat + smp8(64)
    return h + body


def f_mod_mk():
    return mod_generic(b"M.K.", 4)


def f_mod_6chn():
    return mod_generic(b"6CHN", 6)


def f_mod_flt4():
    return mod_generic(b"FLT4", 4)


def f_mod_mk_excl():
    return mod_generic(b"M!K!", 4)


def f_mod_st15():
    # 15-sample soundtracker: no magic; sample names must be printable-ish
    h = z("seed st15", 20) + mod_samples(15)
    h += P("BB", 1, 120) + bytes([0] * 128)
    return h + mod_cells(4) + smp8(64)


# ---------------------------------------------------------------- XM
def f_xm():
    chn, rows = 4, 16
    h = b"Extended Module: " + z("seed xm", 20) + b"\x1a" + z("FastTracker v2.00", 20) + P("<H", 0x0104)
    hdr = P("<HHHHHHHH", 2, 0, chn, 1, 1, 1, 6, 125) + bytes([0, 0] + [0] * 254)
    h += P("<I", 4 + len(hdr)) + hdr
    pat = bytearray()
    for r in range(rows):
        for c in range(chn):
            if r % 4 == 0 and c < 2:
                pat += bytes([0x80 | 0x1F, 49 + c * 4, 1, 0x40, 0x0C, 0x30])
            else:
                pat += b"\x80"
    h += P("<IBHH", 9, 0, rows, len(pat)) + bytes(pat)
    data = smp8(64)
    ins = P("<I", 263) + z("inst", 22) + P("<BH", 0, 1) + P("<I", 40) + bytes(96) + bytes(48) + bytes(48)
    ins += bytes(2 + 6 + 2 + 4 + 2) + bytes(22)
    assert len(ins) == 263, len(ins)
    # sample header: len, loopstart, looplen, vol, finetune, type, pan, relnote, reserved, name
    shdr = P("<III", len(data), 0, 0) + P("<BbBBbB", 64, 0, 0, 128, 0, 0) + z("smp", 22)
    # delta encode
    d = bytearray()
    prev = 0
    for b in data:
        d.append((b - prev) & 0xFF)
        prev = b
    return h + ins + shdr + bytes(d)


# ---------------------------------------------------------------- S3M
def s3m_pattern(chn=4, rows=64):
    out = bytearray()
    for r in range(rows):
        if r % 8 == 0:
            for c in range(2):
                out += bytes([c | 0x20 | 0x40 | 0x80, 0x40 + r // 8 + c * 4, 1, 40, 3, 0x20])
        out += b"\0"
    return P("<H", len(out) + 2) + bytes(out)


def f_s3m():
    data = smp8(64, signed=False)
    ordn, insn, patn = 2, 1, 1
    h = z("seed s3m", 28) + b"\x1a\x10\0\0" + P("<HHHHHH", ordn, insn, patn, 0, 0x1320, 2) + b"SCRM"
    h += bytes([64, 6, 125, 0xB0, 0, 252]) + bytes(8) + P("<H", 0)
    ch = bytes([0, 8, 1, 9, 2, 10] + [255] * 26)
    h += ch + bytes([0, 255])
    pp_off = len(h) + 2 + 2
    # parapointers: instrument at para P0, pattern at P1, sample data at P2
    ins_para = (pp_off + 15) // 16
    pat_para = ins_para + 5
    pat = s3m_pattern()
    smp_para = pat_para + (len(pat) + 15) // 16
    h += P("<H", ins_para) + P("<H", pat_para)
    h = h.ljust(ins_para * 16, b"\0")
    ins = P("<B", 1) + z("smp.s3m", 12) + bytes([0]) + P("<H", smp_para) + P("<III", len(data), 0, 0)
    ins += P("<BBBB", 64, 0, 0, 0) + P("<I", 8363) + bytes(12) + z("seed", 28) + b"SCRS"
    h += ins
    h = h.ljust(pat_para * 16, b"\0") + pat
    h = h.ljust(smp_para * 16, b"\0") + data
    return h


# ---------------------------------------------------------------- STM
def f_stm():
    data = smp8(64, signed=True)
    h = z("seed stm", 20) + z("!Scream!", 8) + b"\x1a\x02" + bytes([2, 21]) + bytes([0x60, 1, 64]) + bytes(13)
    for i in range(31):
        n = len(data) if i == 0 else 0
        h += z("smp", 13) + b"\0" + P("<H", 0) + P("<HHHBB", n, 0, 0xFFFF, 64, 0) + P("<H", 8448) + P("<I", 0) + P("<H", (n + 15) // 16)
    h += bytes([0] + [99] * 127)
    pat = bytearray()
    for r in range(64):
        for c in range(4):
            if r % 8 == 0 and c < 2:
                pat += bytes([0x30 + r // 8, (1 << 3) | 0, 0x20, 0x00])
            else:
                pat += bytes([255, 0, 0, 0])
    h += bytes(pat)
    return h.ljust((len(h) + 15) // 16 * 16, b"\0") + data


# ---------------------------------------------------------------- IT
def it_env(flags=0):
    return P("<BBBBBB", flags, 2, 0, 0, 0, 0) + b"".join(P("<bH", 32 if k < 2 else 0, k * 10) for k in range(25)) + b"\0"


def f_it():
    data = smp8(64, signed=True)
    rows = 32
    pat = bytearray()
    for r in range(rows):
        if r % 4 == 0:
            pat += bytes([0x81, 0x0F, 60 + r // 4, 1, 40, 0x01, 0x03])
            pat += bytes([0x82, 0x03, 64 + r // 4, 1])
        pat += b"\0"
    ordn, insn, smpn, patn = 2, 1, 1, 1
    h = b"IMPM" + z("seed it", 26) + bytes([4, 16]) + P("<HHHH", ordn, insn, smpn, patn)
    h += P("<HHHH", 0x0214, 0x0214, 0x0009 | 0x04, 0)  # stereo + instruments + linear
    h += bytes([128, 48, 6, 125, 128, 0]) + P("<HII", 0, 0, 0)
    h += bytes([32] * 64) + bytes([64] * 64)
    h += bytes([0, 255])
    base = len(h) + 4 + 4 + 4
    ins_off = base
    smp_off = ins_off + 554
    pat_off = smp_off + 80
    dat_off = pat_off + 8 + len(pat)
    h += P("<I", ins_off) + P("<I", smp_off) + P("<I", pat_off)
    keys = b"".join(bytes([n, 1]) for n in range(120))
    ins = b"IMPI" + z("inst.iti", 12) + bytes([0, 0, 0, 0]) + P("<H", 256) + bytes([0, 60, 128, 32, 0, 0]) + P("<H", 0x0214)
    ins += bytes([1, 0]) + z("seed inst", 26) + bytes([0, 0, 0xFF, 0xFF]) + P("<H", 0xFFFF) + keys + it_env(1) + it_env(0) + it_env(0) + bytes(4)
    assert len(ins) == 554, len(ins)
    smp = b"IMPS" + z("smp.its", 12) + bytes([0, 64, 0x01 | 0x10, 64]) + z("seed smp", 26) + bytes([1, 0x80 | 32])
    smp += P("<IIIIIII", len(data), 8, 56, 8363, 0, 0, dat_off) + bytes([0, 0, 0, 0])
    assert len(smp) == 80, len(smp)
    return h + ins + smp + P("<HHI", len(pat), rows, 0) + bytes(pat) + data


# ---------------------------------------------------------------- 669
def f_669():
    data = smp8(64, signed=False)
    h = b"if" + sp("seed 669 composer module", 36) + sp("", 36) + sp("", 36) + bytes([1, 1, 0])
    h += bytes([0] + [0xFF] * 127) + bytes([4] * 128) + bytes([63] * 128)
    h += z("smp669", 13) + P("<III", len(data), 0, 0xFFFFF)
    pat = bytearray()
    for r in range(64):
        for c in range(8):
            if r % 8 == 0 and c < 2:
                pat += bytes([(12 + r // 8) << 2 | 0, 0x00 | 0x30, 0x00])
            else:
                pat += bytes([0xFF, 0xFF, 0xFF])
    return h + bytes(pat) + data


# ---------------------------------------------------------------- MTM
def f_mtm():
    data = smp8(64, signed=False)
    chn = 4
    h = b"MTM\x10" + z("seed mtm", 20) + P("<HBBHBBBB", 1, 0, 0, 0, 1, 0, 64, chn) + bytes([8, 8, 7, 7] + [8] * 28)
    h += z("smp", 22) + P("<IIIBBB", len(data), 0, 0, 0, 64, 0)
    h += bytes([0] * 128)
    trk = bytearray()
    for r in range(64):
        if r % 8 == 0:
            trk += bytes([((25 + r // 8) << 2) | 0, (1 << 4) | 0xC, 0x30])
        else:
            trk += b"\0\0\0"
    h += bytes(trk)
    h += b"".join(P("<H", 1 if c == 0 else 0) for c in range(32))
    return h + data


# ---------------------------------------------------------------- PTM
def f_ptm():
    data = smp8(64, signed=True)
    d = bytearray(); prev = 0
    for b in data:
        d.append((b - prev) & 0xFF); prev = b
    chn = 4
    h = z("seed ptm", 28) + bytes([0x1A, 0x03, 0x02, 0]) + P("<HHHHHH", 2, 1, 1, chn, 0, 0) + b"PTMF" + bytes(16)
    h += bytes([0, 15, 0, 15] + [0] * 28) + bytes([0, 0xFF] + [0] * 254) + P("<H", 0) * 0
    patseg = [0] * 128
    ins_off = 608 + 0
    pat_off = ins_off + 80
    pat = bytearray()
    for r in range(64):
        if r % 8 == 0:
            pat += bytes([0x20 | 0x40 | 0x80 | 0, 40 + r // 8, 1, 0x0C, 0x30, 64])
        pat += b"\0"
    dat_off = ((pat_off + len(pat)) + 15) // 16 * 16
    pat_off = (pat_off + 15) // 16 * 16
    patseg[0] = pat_off // 16
    h += b"".join(P("<H", x) for x in patseg)
    assert len(h) == 608, len(h)
    ins = bytes([1]) + z("smp.ptm", 12) + bytes([64]) + P("<HHI", 8363, 0, dat_off) + P("<III", len(data), 0, 0) + P("<III", 0, 0, 0) + bytes([0, 0]) + z("seed", 28) + b"PTMS"
    assert len(ins) == 80, len(ins)
    h += ins
    h = h.ljust(pat_off, b"\0") + bytes(pat)
    h = h.ljust(dat_off, b"\0") + bytes(d)
    return h


# ---------------------------------------------------------------- FAR
def f_far():
    data = smp8(64, signed=True)
    rows = 16
    text = b""
    h = b"FAR\xfe" + z("seed far", 40) + b"\x0d\x0a\x1a" + P("<H", 869 + len(text)) + bytes([0x10]) + bytes([1] * 16)
    h += bytes(9) + bytes([4]) + bytes([8] * 16) + bytes(4) + P("<H", len(text)) + text
    h += bytes([0] * 256)
    h += bytes([1, 1, 0])
    h += P("<H", rows * 64 + 2) + b"".join(P("<H", 0) for _ in range(255))
    pat = bytearray()
    for r in range(rows):
        for c in range(16):
            if r % 4 == 0 and c < 2:
                pat += bytes([12 + r, 0, 0x0A + c, 0x00])
            else:
                pat += bytes(4)
    h += bytes([rows - 1, 0]) + bytes(pat)
    h += bytes([1, 0, 0, 0, 0, 0, 0, 0])
    h += z("far smp", 32) + P("<I", len(data)) + bytes([0, 15]) + P("<II", 0, 0) + bytes([0, 0])
    return h + data


# ---------------------------------------------------------------- OKT
def f_okt():
    def chunk(tag, body):
        b = P(">I", len(body)) + body
        return tag + b  # no padding: the OKT loader registers no IFF alignment flag
    data = smp8(64, signed=True)
    h = b"OKTASONG"
    h += chunk(b"CMOD", P(">HHHH", 0, 0, 0, 0))
    smp = z("okt smp", 20) + P(">IHHHH", len(data), 0, 1, 64, 0)
    smp = smp.ljust(32, b"\0")
    smps = smp + bytes(32 * 35)
    h += chunk(b"SAMP", smps)
    h += chunk(b"SPEE", P(">H", 6))
    h += chunk(b"SLEN", P(">H", 1))
    h += chunk(b"PLEN", P(">H", 1))
    h += chunk(b"PATT", bytes([0]))
    rows = 16
    pat = P(">H", rows)
    for r in range(rows):
        for c in range(4):
            if r % 4 == 0 and c < 2:
                pat += bytes([12 + r, 0, 0, 0])
            else:
                pat += bytes(4)
    h += chunk(b"PBOD", pat)
    h += chunk(b"SBOD", data)
    return h


# ---------------------------------------------------------------- ULT
def f_ult():
    data = smp8(64, signed=True)
    h = b"MAS_UTrack_V004" + z("seed ult", 32) + bytes([0]) + bytes([1])
    h += z("ult smp", 32) + z("SMP.RAW", 12) + P("<IIII", 0, 0, 0, len(data)) + bytes([64, 0]) + P("<HH", 8363, 0)
    h += bytes([0, 0xFF] + [0xFF] * 254)
    h += bytes([3, 0])  # 4 channels, 1 pattern
    h += bytes([8, 7, 7, 8])
    for c in range(4):
        out = bytearray()
        for r in range(64):
            if r % 8 == 0:
                out += bytes([0x10 + r // 8 + c, 1, 0x00, 0x00, 0x00])
            else:
                out += bytes([0, 0, 0, 0, 0])
        h += bytes(out)
    return h + data


# ---------------------------------------------------------------- MDL
def mdl_chunk(tag, body):
    return tag + P("<I", len(body)) + body


def f_mdl():
    data = smp8(64, signed=True)
    inn = z("seed mdl", 32) + z("author", 20) + P("<HH", 1, 0) + bytes([128, 6, 125])
    inn += bytes([8, 0x78, 0x78, 8] + [0x80] * 28) + bytes([0])
    h = b"DMDL\x11" + mdl_chunk(b"IN", inn)
    pa = bytes([1]) + bytes([4, 63]) + z("pat", 16) + b"".join(P("<H", t) for t in (1, 2, 0, 0))
    h += mdl_chunk(b"PA", pa)
    t1 = bytes([0x1F, 48, 1, 200, 0x1C, 0x1F, 52, 1, 180])
    t2 = bytes([0x1F, 55, 1, 150, 0x38, 0x1F, 60, 1, 150])
    h += mdl_chunk(b"TR", P("<H", 2) + P("<H", len(t1)) + t1 + P("<H", len(t2)) + t2)
    ii = bytes([1]) + bytes([1, 1]) + z("inst", 32) + bytes([1, 0x6F, 0xFF, 0x00, 0x40, 0x00]) + P("<H", 0) + bytes([0, 0, 0, 0, 0, 0])
    h += mdl_chunk(b"II", ii)
    is_ = bytes([1]) + bytes([1]) + z("smp", 32) + z("smp.raw", 8) + P("<IIII", 8363, len(data), 0, 0) + bytes([64, 0])
    h += mdl_chunk(b"IS", is_)
    h += mdl_chunk(b"SA", data)
    return h


# ---------------------------------------------------------------- STX
def s3m_rows(rows=64):
    out = bytearray()
    for r in range(rows):
        if r % 8 == 0:
            for c in range(2):
                out += bytes([c | 0x20 | 0x40 | 0x80, 0x40 + r // 8 + c * 4, 1, 40, 3, 0x20])
        out += b"\0"
    return bytes(out)


def f_stx():
    data = smp8(64, signed=False)
    h = z("seed stx", 20) + b"!Scream!" + P("<HHHHHHH", 0x1234, 0, 4, 5, 6, 0, 0) + bytes([64, 0x60])
    h += P("<HHHHHHH", 0, 0, 1, 1, 1, 0, 0) + P("<H", 0) + b"SCRM"
    assert len(h) == 64
    h = h.ljust(16 * 4, b"\0")
    pat_para = 9 + 5
    h += P("<H", pat_para).ljust(16, b"\0")  # para 4: pattern pointers
    h += P("<H", 9).ljust(16, b"\0")  # para 5: instrument pointers
    h += (bytes(32) + bytes([0, 0, 0, 0, 0])).ljust(48, b"\0")  # para 6..8: channel table + order
    ins = bytes([1]) + z("smp.stx", 13) + P("<H", 0) + P("<III", len(data), 0, 0xFFFF) + bytes([64, 0, 0, 0]) + P("<HH", 8363, 0) + bytes(4)
    ins += P("<HHI", 0, 0, 0) + z("seed", 28) + b"SCRS"
    assert len(ins) == 80, len(ins)
    h += ins
    assert len(h) == pat_para * 16
    return h + s3m_rows() + data


# ---------------------------------------------------------------- IMF
def f_imf():
    data = smp8(64, signed=True)
    h = z("seed imf", 32) + P("<HHHH", 1, 1, 1, 1) + bytes(8) + bytes([6, 125, 64, 128]) + bytes(8) + b"IM10"
    for c in range(32):
        h += z("ch%d" % c, 12) + bytes([0, 0, 128, 0 if c < 4 else 2])
    h += bytes([0] + [255] * 255)
    pat = bytearray()
    for r in range(16):
        if r % 4 == 0:
            pat += bytes([0x20 | 0, 0x50 + r // 4, 1])
            pat += bytes([0x20 | 1, 0x52 + r // 4, 1])
        pat += b"\0"
    h += P("<HH", len(pat) + 4, 16) + bytes(pat)
    ins = z("imf inst", 32) + bytes([0] * 120) + bytes(8) + bytes(192) + bytes(24) + P("<HH", 256, 1) + b"II10"
    assert len(ins) == 384, len(ins)
    smp = z("imf smp", 13) + bytes(3) + P("<IIII", len(data), 0, 0, 8363) + bytes([64, 128]) + bytes(14) + bytes([0]) + bytes(5) + P("<HI", 0, 0) + b"IS10"
    assert len(smp) == 64, len(smp)
    return h + ins + smp + data


# ---------------------------------------------------------------- AMF (DSMI)
def f_amf():
    data = smp8(64, signed=False)
    chn = 4
    h = b"AMF\x0e" + z("seed amf", 32) + bytes([1, 1]) + P("<H", 1) + bytes([chn]) + bytes([0x00] * 32) + bytes([125, 6])
    h += P("<H", 64) + b"".join(P("<H", 1 if c == 0 else 0) for c in range(chn))
    h += bytes([1]) + z("amf inst", 32) + z("smp.raw", 13) + P("<I", 1) + P("<I", len(data)) + P("<H", 8363) + bytes([64]) + P("<II", 0, 0)
    h += P("<H", 1)
    ev = [(0, 0x80, 0), (0, 24, 0x40), (8, 28, 0x40), (0xFF, 0xFF, 0xFF)]
    h += P("<H", len(ev)) + b"\0" + b"".join(bytes(e) for e in ev)
    return h + data


# ---------------------------------------------------------------- Asylum
def f_asylum():
    data = smp8(64, signed=True)
    h = z("ASYLUM Music Format V1.0", 32) + bytes([6, 125, 1, 1, 1, 0]) + bytes([0])
    h = h.ljust(294, b"\0")
    h += z("asy smp", 22) + bytes([0, 64, 0]) + P("<III", len(data), 0, 0) + bytes(37 * 63)
    pat = bytearray()
    for r in range(64):
        for c in range(8):
            if r % 8 == 0 and c < 2:
                pat += bytes([24 + r // 8, 1, 0, 0])
            else:
                pat += bytes(4)
    return h + bytes(pat) + data


# ---------------------------------------------------------------- Liquid Tracker LIQ
def f_liq():
    data = smp8(64, signed=True)
    chn, rows = 2, 8
    hdrsz = 109 + 2 * chn + 1
    h = b"Liquid Module:" + z("seed liq", 30) + z("author", 20) + b"\x1a" + z("Liquid Tracker", 20)
    h += P("<HHHHHH", 0x0100, 6, 125, 1, 0x6000, chn) + P("<I", 0) + P("<HHHH", 1, 1, 1, hdrsz)
    h += bytes([16, 48]) + bytes([32, 32]) + bytes([0])
    assert len(h) == hdrsz
    ev = bytes([0xC3, 40, 0]) + bytes([0x80] * 3) + bytes([0xA0]) + bytes([0xC3, 47, 0]) + bytes([0xC0])
    h += b"LP\0\0" + z("pat", 30) + P("<HII", rows, len(ev), 0) + ev
    li = b"LDSS" + P("<H", 0x0101) + z("liq inst", 30) + z("ed", 20) + z("au", 20) + bytes([0])
    li += P("<IIII", len(data), 0, 0, 8363) + bytes([64, 0, 32, 0, 32, 0]) + P("<HHI", 0x90, 0, 0) + bytes([0, 0]) + bytes(10) + z("smp", 25)
    assert len(li) == 0x90, len(li)
    return h + li + data


# ---------------------------------------------------------------- GDM
def f_gdm():
    data = smp8(64, signed=False)
    h = b"GDM\xfe" + z("seed gdm", 32) + z("author", 32) + b"\r\n\x1a" + b"GMFS" + bytes([1, 0]) + P("<H", 0) + bytes([1, 0])
    h += bytes([8, 7, 8, 7] + [16] * 28)
    pat = bytearray()
    for r in range(64):
        if r % 8 == 0:
            pat += bytes([0x20 | 0x40 | 0, 0x40 + r // 8, 1, 0x20 | 0x06, 0x31, 0x00 | 0x0F, 0x02])
            pat += bytes([0x20 | 1, 0x44 + r // 8, 1])
        pat += b"\0"
    pat = P("<H", len(pat) + 2) + bytes(pat)
    base = 137 + 4 * 0
    # header end = 137 + 1 (ins count) ; fixed fields ord/pat/ins/smp offsets are inside the header
    hdr_len = 137
    ord_ofs = hdr_len
    ins_ofs = ord_ofs + 1
    pat_ofs = ins_ofs + 62
    smp_ofs = pat_ofs + len(pat)
    h += bytes([64, 6, 125]) + P("<H", 0) + P("<I", ord_ofs) + bytes([0]) + P("<I", pat_ofs) + bytes([0]) + P("<I", ins_ofs) + P("<I", smp_ofs) + bytes([0])
    assert len(h) == hdr_len, len(h)
    ins = z("gdm smp", 32) + z("smp.raw", 12) + bytes([0]) + P("<III", len(data), 0, 0) + bytes([0]) + P("<H", 8363) + bytes([64, 8])
    assert len(ins) == 62
    return h + bytes([0]) + ins + pat + data


def delta8(data):
    out = bytearray()
    prev = 0
    for b in data:
        out.append((b - prev) & 0xFF)
        prev = b
    return bytes(out)


# ---------------------------------------------------------------- MASI (PSM "PSM ")
def f_masi():
    data = smp8(64, signed=True)

    def ck(tag, body):
        return tag + P("<I", len(body)) + body

    song = z("seed", 9) + b"\0" + bytes([4]) + b""
    oplh = P("<H", 4) + bytes([0x07, 6, 0x08, 125, 0x01]) + b"PAT0"
    oplh = P("<H", 5) + bytes([0x07, 6]) + bytes([0x08, 125]) + bytes([0x01]) + b"PAT0" + bytes([0])
    song = z("seed", 9) + bytes([0, 4]) + ck(b"OPLH", oplh)
    rows = 16
    pb = bytearray()
    for r in range(rows):
        row = bytearray()
        if r % 4 == 0:
            row += bytes([0xC0, 0, (4 << 4) | (r // 4), 0])
            row += bytes([0xC0, 1, (4 << 4) | (r // 4 + 4), 0])
        pb += P("<H", len(row) + 2) + bytes(row)
    pbod = P("<I", 0) + b"PAT0" + P("<H", rows) + bytes(pb)
    dsmp = bytes([0]) + z("SONG", 8) + b"SMP0" + z("masi smp", 31) + bytes(8) + bytes([0, 0]) + P("<III", len(data), 0, 0) + P("<H", 0)
    dsmp += bytes([128]) + P("<I", 0) + P("<H", 8363) + bytes(16) + delta8(data)
    body = b"FILE" + ck(b"SDFT", b"MAINSONG") + ck(b"TITL", b"seed psm") + ck(b"SONG", song) + ck(b"DSMP", dsmp) + ck(b"PBOD", pbod)
    return b"PSM " + P("<I", len(body)) + body


# ---------------------------------------------------------------- MASI16 (PSM16 "PSM\xfe")
def f_masi16():
    data = smp8(64, signed=True)
    chn, rows = 4, 16
    p_ord = 98
    p_chn = p_ord + 1
    p_ins = p_chn + 16
    pat = bytearray()
    for r in range(rows):
        if r % 4 == 0:
            pat += bytes([0x80 | 0x40 | 0, 12 + r // 4, 1, 63])
            pat += bytes([0x80 | 1, 16 + r // 4, 1])
        pat += b"\0"
    pat = P("<H", len(pat) + 4) + bytes([rows, chn]) + bytes(pat)
    p_pat = p_ins + 64
    p_smp = p_pat + len(pat)
    h = b"PSM\xfe" + z("seed psm16", 60) + bytes([0, 0x10, 0, 6, 125, 64]) + P("<HHHHHH", 1, 1, 1, 1, chn, chn)
    h += P("<IIII", p_ord, p_chn, p_pat, p_ins)
    assert len(h) == 98, len(h)
    ins = z("smp.raw", 13) + z("psm16 smp", 24) + P("<I", p_smp) + bytes(4) + P("<H", 1) + bytes([0]) + P("<III", len(data), 0, 0) + bytes([0x70, 64]) + P("<H", 8363)
    assert len(ins) == 64, len(ins)
    return h + bytes([0]) + bytes([8, 8, 7, 7] + [8] * 12) + ins + pat + delta8(data)


# ---------------------------------------------------------------- DBM0
def ck_be(tag, body):
    return tag + P(">I", len(body)) + body


def f_dbm():
    data = smp8(64, signed=True)
    chn, rows = 4, 16
    h = b"DBM0" + P(">H", 0x0300) + bytes(2) + ck_be(b"NAME", z("seed dbm", 44))[:8]
    h += z("seed dbm", 44)
    info = P(">HHHHH", 1, 1, 1, 1, chn)
    h += ck_be(b"INFO", info)
    h += ck_be(b"SONG", z("song", 44) + P(">HH", 1, 0))
    inst = z("dbm inst", 30) + P(">HH", 1, 64) + P(">IIIh", 8363, 0, 0, 0) + P(">H", 0)
    assert len(inst) == 50
    h += ck_be(b"INST", inst)
    pb = bytearray()
    for r in range(rows):
        if r % 4 == 0:
            pb += bytes([1, 0x03, (4 << 4) | (r // 4 + 1), 1])
            pb += bytes([2, 0x03, (4 << 4) | (r // 4 + 5), 1])
        pb += b"\0"
    h += ck_be(b"PATT", P(">HI", rows, len(pb)) + bytes(pb))
    h += ck_be(b"SMPL", P(">II", 1, len(data)) + data)
    return h


# ---------------------------------------------------------------- DIGI Booster
def f_digi():
    data = smp8(64, signed=True)
    h = z("DIGI Booster module", 20) + b"V1.6" + bytes([0x16, 4, 1]) + bytes(19) + bytes([0, 0]) + bytes([0] * 128)
    h += P(">I", len(data)) + bytes(4 * 30) + bytes(4 * 31) + bytes(4 * 31) + bytes([64]) + bytes(30) + bytes(31)
    h += z("seed digi", 32) + z("digi smp", 30) + bytes(30 * 30)
    # packed pattern: per row bitmask byte (0x80 = channel 0) + events
    tbl = bytearray(64)
    evs = bytearray()
    for r in range(64):
        if r % 8 == 0:
            tbl[r] = 0xC0
            for c in range(2):
                evs += P(">I", (PERIODS[(r // 8 + c) % 8] << 16) | (1 << 12) | (0xC << 8) | 0x30)
    pat = P(">H", 64 + len(evs)) + bytes(tbl) + bytes(evs)
    return h + pat + data


# ---------------------------------------------------------------- EMOD (Quadra Composer)
def f_emod():
    data = smp8(64, signed=True)
    rows, chn = 16, 4
    emic = P(">H", 1) + z("seed emod", 20) + bytes(20) + bytes([125, 1])
    emic += bytes([1, 64]) + P(">H", len(data) // 2) + z("emod smp", 20) + bytes([0, 0]) + P(">HHI", 0, 1, 0)
    emic += bytes([0, 1]) + bytes([0, rows - 1]) + z("pat", 20) + P(">I", 0)
    emic += bytes([1, 0])
    pat = bytearray()
    for r in range(rows):
        for c in range(chn):
            if r % 4 == 0 and c < 2:
                pat += bytes([1, 24 + r // 4 + c, 0x0C, 0x30])
            else:
                pat += bytes([0, 0xFF, 0, 0])
    body = b"EMOD" + ck_be(b"EMIC", emic) + ck_be(b"PATT", bytes(pat)) + ck_be(b"8SMP", data)
    return b"FORM" + P(">I", len(body)) + body


# ---------------------------------------------------------------- SoundFX 1.3
def f_sfx():
    data = smp8(64, signed=True)
    nins = 15
    h = P(">I", len(data)) + bytes(4 * 14) + b"SONG" + P(">H", 0x38E5) + bytes(14)
    for i in range(nins):
        if i == 0:
            h += z("sfx smp", 22) + P(">HBBHH", len(data) // 2, 0, 64, 0, 1)
        else:
            h += z("", 22) + P(">HBBHH", 0, 0, 0, 0, 1)
    h += bytes([1, 0]) + bytes([0] * 128)
    for r in range(64):
        for c in range(4):
            if r % 8 == 0 and c < 2:
                h += P(">I", (0x1 << 28) | (PERIODS[(r // 8 + c) % 8] << 16) | (0x1 << 12))
            else:
                h += bytes(4)
    return h + data


# ---------------------------------------------------------------- Funktracker
def f_fnk():
    data = smp8(64, signed=True)
    chn = 4
    body = b"Funk" + bytes([0x01, 0x40, 0x10, 0x00])
    pats = bytearray()
    for r in range(64):
        for c in range(chn):
            if r % 8 == 0 and c < 2:
                pats += bytes([(24 + r // 8) << 2, 0x00 | 0x0C, 0x30])
            else:
                pats += bytes([0x3F << 2, 0x0F, 0])
    ins = z("fnk smp", 19) + P("<II", 0xFFFFFFFF, len(data)) + bytes([255, 128, 0, 0, 0])
    inss = ins + (z("", 19) + P("<II", 0xFFFFFFFF, 0) + bytes([255, 128, 0, 0, 0])) * 63
    hdr_rest = b"Fk04" + bytes([0]) + bytes([0, 0xFF] + [0xFF] * 254) + bytes(128)
    total = 8 + 4 + len(hdr_rest) + len(inss) + len(pats) + len(data)
    return body + P("<I", total) + hdr_rest + inss + bytes(pats) + data


# ---------------------------------------------------------------- MED 1.12 (MED2); samples are external files, so none here
def f_med2():
    h = b"MED\x02" + bytes(40) + bytes(40 * 31)
    h += bytes([0]) + bytes([64] * 31) + P(">H", 0) + bytes(62) + P(">H", 0) + bytes(62)
    h += P(">H", 1) + bytes([0] * 100) + P(">H", 1) + P(">H", 33) + P(">HHI", 0, 6, 0) + bytes(16)
    pat = P(">I", 0)
    for r in range(64):
        for c in range(4):
            if r % 8 == 0 and c < 2:
                pat += P(">H", PERIODS[(r // 8 + c) % 8]) + bytes([0x1C, 0x30])
            else:
                pat += bytes(4)
    return h + pat


# ---------------------------------------------------------------- MED 2.00 (MED3)
def f_med3():
    data = smp8(64, signed=True)
    h = b"MED\x03" + b"".join((b"smp\0" if i == 0 else b"\0") for i in range(32))
    h += P(">I", 0x80000000) + bytes([64])
    h += P(">I", 0) + P(">I", 0)
    h += P(">H", 1) + P(">H", 1) + bytes([0]) + P(">H", 33) + bytes([0, 0x08]) + P(">H", 0) + P(">I", 0) + bytes(16)
    h += P(">I", 0) + P(">I", 0)
    # pattern block: line mask 0 = row 0 only, no fx
    conv = bytes([0xC1, 0x81, 0x1A, 0x10])
    h += bytes([4, 0x20 | 0x40 | 0x80]) + P(">H", len(conv)) + P(">I", 0x80000000) + conv
    h += P(">I", 0x80000000) + P(">I", len(data)) + P(">H", 0) + data
    return h


# ---------------------------------------------------------------- MMD0 / MMD1 / MMDC (OctaMED)
def mmd_song(nblk, tempo=125, numsmp=1, flags2=0x20):
    h = b""
    for k in range(63):
        h += P(">HHBBBb", 0, 0, 0, 0, 64 if k == 0 else 0, 0)
    h += P(">HH", nblk, 1) + bytes([0] + [0] * 255) + P(">H", tempo) + bytes([0, 0x08, flags2, 6]) + bytes([64] * 16) + bytes([64, numsmp])
    assert len(h) == 788
    return h


def mmd_med01(magic):
    data = smp8(64, signed=True)
    chn, lines = 4, 16
    ver = 1 if magic == b"MMD1" else 0
    song_off = 52
    blkarr_off = song_off + 788
    smparr_off = blkarr_off + 4
    blk_off = smparr_off + 4
    cell = 4 if ver else 3
    cells = bytearray()
    for r in range(lines):
        for c in range(chn):
            if r % 4 == 0 and c < 2:
                n = 13 + r // 2 + c
                if ver:
                    cells += bytes([n, 1, 0x0C, 0x40])
                else:
                    cells += bytes([n, 0x10 | 0x0C, 0x40])
            else:
                cells += bytes(cell)
    if ver:
        blk = P(">HHI", chn, lines - 1, 0) + bytes(cells)
    elif magic == b"MMDC":
        blk = bytes([chn, lines - 1])
        raw = bytes(cells)
        packed = bytearray()
        j = 0
        while j < len(raw):
            n = min(64, len(raw) - j)
            if raw[j:j + n] == bytes(n) and n >= 2:
                packed.append((256 - n) & 0xFF)
            else:
                packed.append(n - 1)
                packed += raw[j:j + n]
            j += n
        blk += bytes(packed)
    else:
        blk = bytes([chn, lines - 1]) + bytes(cells)
    smp_off = blk_off + len(blk)
    sample = P(">IH", len(data), 0) + data
    total = smp_off + len(sample)
    h = magic + P(">I", total) + P(">I", song_off) + P(">HH", 0, 0) + P(">I", blkarr_off) + P(">I", 0) + P(">I", smparr_off) + P(">I", 0)
    h += P(">I", 0) + P(">I", 0) + P(">HHHHH", 0, 0, 0, 0, 0) + bytes([0, 0])
    assert len(h) == 52, len(h)
    return h + mmd_song(1) + P(">I", blk_off) + P(">I", smp_off) + blk + sample


def f_mmd1():
    return mmd_med01(b"MMD1")


def f_mmd0():
    return mmd_med01(b"MMD0")


def f_mmdc():
    return mmd_med01(b"MMDC")


# ---------------------------------------------------------------- MMD2 / MMD3 (OctaMED SS)
def mmd_med23(magic):
    data = smp8(64, signed=True)
    chn, lines = 4, 16
    song_off = 52
    seq_off = song_off + 788
    playseq_off = seq_off + 4
    playseq = z("seq", 32) + P(">II", 0, 0) + P(">H", 1) + P(">H", 0)
    tv_off = playseq_off + len(playseq)
    tp_off = tv_off + 16
    blkarr_off = tp_off + 16
    smparr_off = blkarr_off + 4
    blk_off = smparr_off + 4
    cells = bytearray()
    for r in range(lines):
        for c in range(chn):
            if r % 4 == 0 and c < 2:
                cells += bytes([13 + r // 2 + c, 1, 0x0C, 0x40])
            else:
                cells += bytes(4)
    blk = P(">HHI", chn, lines - 1, 0) + bytes(cells)
    smp_off = blk_off + len(blk)
    sample = P(">IH", len(data), 0) + data
    song = b""
    for k in range(63):
        song += P(">HHBBBb", 0, 0, 0, 0, 64 if k == 0 else 0, 0)
    song += P(">HH", 1, 1) + P(">I", seq_off) + P(">I", 0) + P(">I", tv_off) + P(">HH", chn, 1) + P(">I", tp_off) + P(">I", 0)
    song += P(">HH", 0, chn) + bytes([0, 0]) + P(">H", 0) + bytes([0]) + bytes(223)
    song += P(">H", 125) + bytes([0, 0x08, 0x20, 6]) + bytes(16) + bytes([64, 1])
    assert len(song) == 788, len(song)
    total = smp_off + len(sample)
    h = magic + P(">I", total) + P(">I", song_off) + P(">HH", 0, 0) + P(">I", blkarr_off) + P(">I", 0) + P(">I", smparr_off) + P(">I", 0)
    h += P(">I", 0) + P(">I", 0) + P(">HHHHH", 0, 0, 0, 0, 0) + bytes([0, 0])
    return h + song + P(">I", playseq_off) + playseq + bytes([64] * 16) + bytes([0] * 16) + P(">I", blk_off) + P(">I", smp_off) + blk + sample


def f_mmd3():
    return mmd_med23(b"MMD3")


def f_mmd2():
    return mmd_med23(b"MMD2")


# ---------------------------------------------------------------- RTM (Real Tracker)
def rtm_obj(tag, name, version, body_hdr):
    return tag + b"\x20" + z(name, 32) + b"\x1a" + P("<HH", version, len(body_hdr)) + body_hdr


def f_rtm():
    data = smp8(64, signed=True)
    chn, rows = 4, 16
    ver = 0x0110
    mm = z("RealTracker", 20) + z("composer", 32) + P("<H", 1) + bytes([chn, 1]) + P("<HH", 1, 1) + bytes([6, 125]) + bytes(32) + P("<I", 2)
    h = rtm_obj(b"RTMM", "seed rtm", ver, mm) + P("<H", 0)
    nd = bytearray()
    for r in range(rows):
        if r % 4 == 0:
            nd += bytes([0x06, 24 + r // 4, 1, 0x06, 30 + r // 4, 1])
        nd += b"\0"
    h += rtm_obj(b"RTND", "pat", ver, P("<HBHI", 1, chn, rows, len(nd))) + bytes(nd)
    env = bytes([0]) + bytes(96) + bytes([0, 0, 0]) + P("<H", 0)
    rin = bytes([1]) + P("<H", 0) + bytes(120) + env + env + bytes([0, 0, 0, 0]) + P("<H", 0) + bytes(4)
    h += rtm_obj(b"RTIN", "rtm inst", ver, rin)
    h += rtm_obj(b"RTSM", "rtm smp", ver, P("<HBBI", 4, 64, 64, len(data)) + bytes([0, 0, 0, 0]) + P("<III", 0, 0, 8363) + bytes([48, 0])) + delta8(data)
    return h


# ---------------------------------------------------------------- Digital Tracker DTM ("D.T.")
def f_dt():
    data = smp8(64, signed=True)
    chn, rows = 4, 16
    dt = P(">H", 0) + bytes([0xFF, 0]) + P(">H", 0) + P(">HH", 6, 125) + P(">I", 8400) + b"seed dt\0"
    h = ck_be(b"D.T.", dt)
    h += ck_be(b"S.Q.", P(">HH", 1, 0) + P(">I", 0) + bytes([0] * 128))
    h += ck_be(b"PATT", P(">HH", chn, 1) + b"2.04")
    inst = P(">H", 1) + P(">I", 0) + P(">I", len(data)) + bytes([0, 64]) + P(">II", 0, 0) + z("dt inst", 22) + bytes([0, 8]) + P(">HH", 60, 0) + P(">I", 8363)
    assert len(inst) == 52, len(inst)
    h += ck_be(b"INST", inst)
    cells = bytearray()
    for r in range(rows):
        for c in range(chn):
            if r % 4 == 0 and c < 2:
                cells += bytes([(3 << 4) | (r // 4 + c), (32 << 2) | 0, (1 << 4) | 0x0C, 0x30])
            else:
                cells += bytes(4)
    h += ck_be(b"DAPT", P(">IHH", 0xFFFFFFFF, 0, rows) + bytes(cells))
    h += ck_be(b"DAIT", data)
    return h


# ---------------------------------------------------------------- Megatracker MGT
def f_mgt():
    data = smp8(64, signed=True)
    chn, rows = 4, 16
    sng_ptr = 58
    song_len = 32 + 4 + 2 + 2 + 1 + 1 + 2 + 1 + 1 + 2 * chn
    seq_ptr = sng_ptr + song_len
    ins_ptr = seq_ptr + 2
    ins_size = 80
    pat_ptr = ins_ptr + ins_size
    trk_ptr = pat_ptr + 2 + 2 * chn
    trk_data = trk_ptr + 8
    trk = P(">H", rows)
    for r in range(0, rows, 4):
        trk += bytes([(0 if r == 0 else 3) | 0x04 | 0x08, 30 + r // 4, 1])
    trk += bytes([0, 0, 0])
    smp_off = trk_data + len(trk)
    h = b"MGT\x10\xbdMCS" + P(">HHHHHH", chn, 1, 1, 1, 2, 1) + P(">H", 0) + P(">I", 0)
    h += P(">IIIII", sng_ptr, seq_ptr, ins_ptr, pat_ptr, trk_ptr) + P(">III", smp_off, len(data), 0)
    assert len(h) == sng_ptr, len(h)
    h += z("seed mgt", 32) + P(">I", seq_ptr) + P(">HH", 1, 0) + bytes([125, 6]) + P(">H", 64) + bytes([0, 0]) + P(">H", 128) * chn
    h += P(">H", 0)
    ins = z("mgt smp", 32) + P(">IIII", smp_off, len(data), 0, 0) + P(">II", 0, 0) + P(">I", 8363) + P(">H", 0x400) + bytes([0, 0, 0, 0]) + bytes(14)
    assert len(ins) == ins_size, len(ins)
    h += ins[:0] + ins
    h += P(">H", rows) + P(">H", 2) * chn
    h += bytes(8)[:0] + P(">I", 0) + P(">I", trk_data)
    h += trk + data
    return h


# ---------------------------------------------------------------- AMOS Music Bank (AmBk)
def f_abk():
    data = smp8(64, signed=True)
    base = 0x14
    ins_rel = 12
    songs_rel = ins_rel + 2 + 32
    song_sec = 6
    song_struct = 8 + 2 + 2 + 16
    pls = [4 * k for k in range(4)]
    playlists = b"".join(P(">HH", 0, 0xFFFF) for _ in range(4))
    pat_rel = songs_rel + song_sec + song_struct + len(playlists)
    notes = b""
    for r in range(4):
        notes += P(">HH", 0x4000 | 4, PERIODS[r])
    notes += P(">H", 0x8000)
    pat_sec = P(">H", 1) + P(">HHHH", 10, 10, 10, 10)
    pat_sec = pat_sec.ljust(2 + 8, b"\0")
    data_rel = pat_rel + len(pat_sec) + len(notes)
    insec_abs = base + ins_rel
    smp_off = (base + data_rel) - insec_abs
    ins = P(">IIHHHH", smp_off, smp_off, len(data) // 2, 0, 64, len(data) // 2) + z("abk smp", 16)
    assert len(ins) == 32
    h = b"AmBk" + P(">H", 3) + P(">I", 0) + P(">H", 0) + b"Music   "
    h += P(">III", ins_rel, songs_rel, pat_rel)
    h += P(">H", 1) + ins
    h += P(">H", 1) + P(">I", song_sec)
    song = b"".join(P(">H", song_struct + p) for p in pls) + P(">HH", 6, 0) + z("seed abk", 16)
    h += song + playlists + pat_sec + notes + data
    return h


# ---------------------------------------------------------------- UMX (Unreal package) wrapping S3M or IT
def fci(a):
    """Unreal FCompactIndex, values 0..8191 only."""
    if a < 64:
        return bytes([a])
    return bytes([0x40 | (a & 0x3F), (a >> 6) & 0x7F])


def umx_wrap(typename, payload):
    ver = 69
    names = bytes([len(typename) + 1]) + typename.encode() + b"\0" + P("<I", 0)
    name_off = 64
    exp_off = name_off + len(names)
    imp_off = exp_off + 32
    # export entry: class, super, package(int32), object name, flags(int32), serial size, serial offset
    pre = len(fci(0)) * 2 + 4 + 1 + 4
    ser_size = fci(len(payload) + 8)
    # the serial offset field length depends on its own value; keep it at 2 bytes
    obj_off = imp_off + 4
    ser_off = fci(obj_off)
    if len(ser_off) < 2:
        ser_off = bytes([0x40 | obj_off, 0])
    export = fci(0) + fci(0) + P("<i", 0) + fci(0) + P("<I", 0) + ser_size + ser_off
    export = export.ljust(32, b"\0")
    # object header at serial offset: junk fci, type name fci(0), export size int32, object size fci
    objhdr = fci(0) + fci(0) + P("<I", 0) + fci(len(payload))
    hdr = P("<IIIiiiiii", 0x9E2A83C1, ver, 0, 1, name_off, 1, exp_off, 0, imp_off)
    hdr = hdr.ljust(64, b"\0")
    body = hdr + names + export + bytes(4)
    assert len(body) == imp_off + 4, (len(body), imp_off)
    return body + objhdr + payload


def f_umx_s3m():
    return umx_wrap("S3M", f_s3m())


def f_umx_it():
    return umx_wrap("IT", f_it())


# ---------------------------------------------------------------- XMF (Astroidea)
def f_xmf():
    data = smp8(64, signed=True)
    chn, npat = 4, 1
    h = bytes([0x04])
    tab = b"".join(
        (bytes([0, 0, 0, 0, 0, 0]) + P("<I", 0)[:3] + P("<I", len(data))[:3] + bytes([64, 0]) + P("<H", 8363)) if k == 0 else bytes(16)
        for k in range(256))
    h += tab
    h += bytes([0] + [0xFF] * 255)
    h += bytes([chn - 1, npat - 1])
    h += bytes([7, 8, 8, 7])
    for r in range(64):
        for c in range(chn):
            if r % 8 == 0 and c < 2:
                h += bytes([12 + r // 8 + c, 0, 0x0C, 0, 0, 0x30])
            else:
                h += bytes(6)
    return h + data


# ---------------------------------------------------------------- HMN (His Master's Noise): a MOD with FEST magic
def f_hmn():
    return mod_generic(b"FEST", 4)


# ---------------------------------------------------------------- Coconizer
def f_coco():
    data = smp8(64, signed=True)
    seq_ptr, pat_ptr = 64, 72
    smp_ptr = pat_ptr + 64 * 4 * 4
    h = bytes([0x84]) + b"seed coco\r".ljust(20, b"\r") + bytes([1, 1, 1]) + P("<II", seq_ptr, pat_ptr)
    h += P("<IIIII", smp_ptr, len(data), 0, 0, 0) + b"cocosmp\r".ljust(11, b"\r") + bytes([0])
    h = h.ljust(64, b"\0")
    h += bytes([0, 0xFF]).ljust(8, b"\xff")
    pat = bytearray()
    for r in range(64):
        for c in range(4):
            if r % 8 == 0 and c < 2:
                pat += bytes([0x00, 0x0C, 1, 20 + r // 8])
            else:
                pat += bytes(4)
    return h + bytes(pat) + data


# ---------------------------------------------------------------- Archimedes Tracker (MUSX)
def f_arch():
    data = smp8(64, signed=False)

    def ck_le(tag, body):
        return tag + P("<I", len(body)) + body

    rows = 16
    chn = 4
    pat = bytearray()
    for r in range(rows):
        for c in range(chn):
            if r % 4 == 0 and c < 2:
                pat += bytes([0, 0, 1, 10 + r // 4])
            else:
                pat += bytes(4)
    samp = b"SNAM" + P("<I", 8) + z("archsmp", 8)
    samp += b"SVOL" + P("<II", 4, 255) + b"SLEN" + P("<II", 4, len(data)) + b"ROFS" + P("<II", 4, 0) + b"RLEN" + P("<II", 4, 0)
    samp += b"SDAT" + P("<II", len(data) + 4, 0) + data
    body = ck_le(b"MVOX", P("<I", chn)) + ck_le(b"MNAM", z("seed arch", 32)) + ck_le(b"MLEN", P("<I", 1)) + ck_le(b"PNUM", P("<I", 1))
    body += ck_le(b"PLEN", bytes([rows]) + bytes(63)) + ck_le(b"SEQU", bytes(128)) + ck_le(b"PATT", bytes(pat)) + ck_le(b"SAMP", samp)
    return b"MUSX" + P("<I", len(body)) + body


# ---------------------------------------------------------------- Ice Tracker / Soundtracker 2.6 (MTN\0 or IT10)
def f_ice(magic=b"IT10"):
    data = smp8(64, signed=True)
    h = z("seed ice", 20) + mod_samples(31)
    h += bytes([1, 1]) + bytes(128 * 4) + magic
    assert len(h) == 1468
    trk = b"".join((P(">I", (PERIODS[r // 8 % 8] << 16) | (1 << 12) | (0xC << 8) | 0x30) if r % 8 == 0 else bytes(4)) for r in range(64))
    return h + trk + data


def f_mtn():
    return f_ice(b"MTN\0")


# ---------------------------------------------------------------- Images Music System
def f_ims():
    data = smp8(64, signed=True)
    h = z("seed ims", 20)
    for i in range(31):
        if i == 0:
            h += z("ims smp", 22) + P(">HBBHH", len(data) // 2, 0, 64, 0, 1)
        else:
            h += z("", 22) + P(">HBBHH", 0, 0, 0, 0, 1)
    h += bytes([1, 0]) + bytes([0] * 128) + P(">I", 0x43C + 0x300)
    assert len(h) == 1084
    pat = bytearray()
    for j in range(256):
        r, c = j >> 2, j & 3
        if r % 8 == 0 and c < 2:
            pat += bytes([(r // 8 + c) & 0x3F, 0x10 | 0x0C, 0x30])
        else:
            pat += bytes([0x3F, 0, 0])
    return h + bytes(pat) + data


# ---------------------------------------------------------------- ProTracker 3 IFF (FORM MODL)
def f_pt3():
    inner = f_mod_mk()
    # PTDT carries a plain M.K. module; its sample data follows the patterns
    info = z("seed pt3", 32) + P(">HHHHH", 31, 1, 1, 64, 125) + P(">H", 0) + P(">HHHHHH", 1, 1, 2020, 0, 0, 0) + P(">HHH", 0, 0, 0)
    ptdt_inner = inner
    vers = b"VERS" + P(">I", 10) + b"PT3.6 ..\0\0"[:10]
    body = b"MODL" + vers + b"INFO" + P(">I", len(info) + 8) + info + b"PTDT" + P(">I", len(ptdt_inner) + 8) + ptdt_inner
    return b"FORM" + P(">I", len(body)) + body


# ---------------------------------------------------------------- Liquid Tracker "NO"
def f_no():
    data = smp8(64, signed=False)
    chn, npat = 4, 1
    h = b"NO\0\0" + bytes([7]) + z("seed no", 29) + bytes([npat, 0, chn]) + bytes([0]) + P("<HH", 0, 0) + bytes([0])
    h += bytes([0, 0xFF]) + bytes([0xFF] * 254)
    for i in range(63):
        if i == 0:
            h += bytes([5]) + z("nosmp", 30) + bytes([64]) + P("<H", 8363) + P("<III", len(data), 0, 0)
        else:
            h += bytes([0]) + z("", 30) + bytes([0]) + P("<H", 8363) + P("<III", 0, 0, 0)
    for r in range(64):
        for c in range(chn):
            if r % 8 == 0 and c < 2:
                h += P("<I", (20 + r // 8) | (0 << 6) | (63 << 13) | (0xF << 20))
            else:
                h += P("<I", 0x3F | (0x7F << 6) | (0x7F << 13) | (0xF << 20))
    return h + data


# ---------------------------------------------------------------- STIM (Slamtilt)
def f_stim():
    data = smp8(64, signed=True)
    pataddr = 0x200 - 0x14 - 4
    smpaddr = 0x300
    h = b"STIM" + P(">I", smpaddr) + bytes(8) + P(">HHH", 1, 1, 1) + bytes([0] * 128) + P(">I", pataddr) + bytes(63 * 4)
    h = h.ljust(pataddr + 0x0C + 8, b"\0")
    for c in range(4):
        if c < 2:
            h += bytes([1, 0x20 | (24 + c), 0x30]) + bytes([0x80 | 62])
        else:
            h += bytes([0x80 | 63])
    h = h.ljust(smpaddr + 4, b"\0")
    h += P(">HBBHH", len(data) // 2, 0, 64, 0, 1) + data
    return h


# ---------------------------------------------------------------- Galaxy Music System 5.0 (RIFF AM) and 4.0 (RIFF AMFF)
def gal_pattern(rows=16):
    pat = bytearray()
    for r in range(rows):
        if r % 4 == 0:
            pat += bytes([0x40 | 0, 0, 40 + r // 4])
            pat += bytes([0x40 | 1, 0, 44 + r // 4])
        pat += b"\0"
    return bytes(pat)


def riff_ck(tag, body, align=True):
    b = tag + P("<I", len(body)) + body
    return b + (b"\0" if align and len(body) & 1 else b"")


def f_gal5():
    data = smp8(64, signed=True)
    rows = 16
    init = z("seed gal5", 64) + bytes([1, 4, 6, 125]) + P("<HH", 0x01C5, 0xFF00) + bytes([0x80]) + bytes([32] * 64)
    ordr = bytes([0, 0])
    pat = gal_pattern(rows)
    patt = bytes([0]) + P("<I", len(pat)) + bytes([rows - 1]) + pat
    inst = P(">I", 0x42010000) + bytes([0, 0]) + z("gal5 inst", 28) + bytes(290) + P("<H", 1)
    inst += b"RIFF" + P("<I", 0) + b"AS  SAMP" + P("<I", 0) + P(">I", 0x40000000) + z("gal5 smp", 28) + P(">I", 0) + bytes([0])
    inst += bytes([64]) + P("<HHH", 0x7FFF, 0x80, 0x80) + P("<III", len(data), 0, 0) + P("<I", 8363) + P("<II", 0, 0) + data
    body = b"AM  " + riff_ck(b"INIT", init) + riff_ck(b"ORDR", ordr) + riff_ck(b"PATT", patt) + riff_ck(b"INST", inst)
    return b"RIFF" + P("<I", len(body)) + body


def f_gal4():
    data = smp8(64, signed=True)
    rows = 16
    main = z("seed gal4", 64) + bytes([1, 4, 6, 125]) + P("<HH", 0x01C5, 0xFF00) + bytes([0x80])
    ordr = bytes([0, 0])
    pat = gal_pattern(rows)
    patt = bytes([0]) + P("<I", len(pat) + 2) + bytes([rows - 1]) + pat
    inst = bytes([0, 0]) + z("gal4 inst", 28) + bytes([1]) + bytes(108) + bytes(11) + bytes([0, 0, 0, 0, 0]) + P("<H", 0) + bytes([0, 0x00, 0, 0, 0, 0])
    inst += bytes(30) + bytes(30) + bytes([0, 0])
    inst += b"SAMP" + P("<I", 0) + z("gal4 smp", 28) + bytes([32, 64, 0x00, 0x80]) + P("<III", len(data), 0, 0) + P("<I", 8363) + P("<II", 0, 0) + data
    body = b"AMFF" + riff_ck(b"MAIN", main, False) + riff_ck(b"ORDR", ordr, False) + riff_ck(b"PATT", patt, False) + riff_ck(b"INST", inst, False)
    return b"RIFF" + P("<I", len(body)) + body


# ---------------------------------------------------------------- MUSE container (zlib stream holding GAL5 or GAL4)
def f_muse():
    import zlib
    return b"MUSE" + P(">I", 0xDEADBEAF) + bytes(16) + zlib.compress(f_gal5())


def f_muse4():
    import zlib
    return b"MUSE" + P(">I", 0xDEADBABE) + bytes(16) + zlib.compress(f_gal4())


# ---------------------------------------------------------------- Chiptracker (KRIS)
def f_chip():
    data = smp8(64, signed=True)
    h = z("seed chip", 20) + P(">H", 0)
    for i in range(31):
        if i == 0:
            h += z("chip smp", 22) + P(">HBBHH", len(data) // 2, 0, 64, 0, 1)
        else:
            h += z("", 22) + P(">HBBHH", 0, 0, 0, 0, 1)
    h += b"KRIS" + bytes([1, 0]) + bytes(1024) + P(">H", 0)
    trk = b"".join((bytes([(12 + r // 8) * 2, 1, 0x0C, 0x30]) if r % 8 == 0 else bytes(4)) for r in range(64))
    return h + trk + data


# ---------------------------------------------------------------- Digital Symphony (BASSTRAK), stored (uncompressed) variant
def f_sym():
    data = smp8(64, signed=True)
    chn, npat, ntrk = 4, 1, 2
    h = P(">II", 0x02011313, 0x1412010B) + bytes([0, chn]) + P("<HH", npat, ntrk) + P("<I", 0)[:3]
    for k in range(63):
        h += (bytes([6]) + P("<I", len(data) // 2)[:3]) if k == 0 else bytes([0x80])
    h += bytes([8]) + b"seed sym" + bytes([0xFF] * 8)
    # sequence (stored): one pattern, each channel plays track 0 or 1
    h += bytes([0]) + b"".join(P("<H", c & 1) for c in range(chn))
    # tracks (stored): 64 rows * 4 bytes per track
    tracks = bytearray()
    for t in range(ntrk):
        for r in range(64):
            if r % 8 == 0:
                tracks += P("<I", (12 + r // 8 + t) | (1 << 6) | (0xC << 14) | (0x30 << 20))
            else:
                tracks += bytes(4)
    h += bytes([0]) + bytes(tracks)
    # instruments: name, sus start/len (words), volume, finetune, then packing + data
    h += b"symsmp" + P("<I", 0)[:3] + P("<I", 0)[:3] + bytes([64, 0]) + bytes([2]) + data
    return h


# ---------------------------------------------------------------- MED 2.10 (MED4), bit-packed patterns, internal samples
def f_med4():
    data = smp8(64, signed=True)
    chn, rows = 4, 16
    h = b"MED\x04" + bytes([0x80, 0x80])  # instrument mask: byte count flags, then 0x80 = instrument 1
    h += bytes([0xFF, 4]) + b"inst"  # flags 0xFF: no optional fields; name
    h += P(">HH", 1, 1) + bytes([0])  # patterns, song length, order
    h += P(">H", 33) + bytes([0, 0x08]) + P(">H", 6) + bytes(20) + bytes([64] * 16) + bytes([64])
    nib = []
    for line in (0, 4):
        nib += [0xC]
        for c in range(2):
            n = 0x18 + line + c
            nib += [n >> 4, n & 0xF, 1]
    if len(nib) & 1:
        nib.append(0)
    stream = bytes((nib[k] << 4) | nib[k + 1] for k in range(0, len(nib), 2))
    ctl = bytes([0x10]) + P(">I", 0x88000000)
    size = 1 + 1 + 2 + len(ctl)
    h += bytes([size, chn, rows - 1]) + P(">H", len(stream)) + ctl + stream
    h += P(">II", 0x40000000, 0) + P(">IH", len(data), 0) + data
    return h


# ================================================================ ProWizard packers
def pw_mod_like(tag, cell, restart=0x7F, nsmp=31):
    """MOD-shaped container: 950-byte header, song length, restart, orders, 4-byte tag, 256-cell pattern, one sample."""
    data = smp8(64, signed=True)
    h = z("seed pw", 20) + mod_samples(nsmp)
    h += bytes([1, restart]) + bytes(128) + tag
    pat = b"".join(cell(r) for r in range(256))
    return h + pat + data


def f_pw_wn():
    return pw_mod_like(b"WN\0\0", lambda r: bytes([2 * (3 + r // 64 % 3), 0x10, 0x0C, 0x30]) if r % 16 == 0 else bytes(4))


def f_pw_prun1():
    return pw_mod_like(b"SNT.", lambda r: bytes([0x02 * 0, 6 * 2, 0x0C, 0x30]) if r % 16 == 0 else bytes(4))


def f_pw_hrt():
    return pw_mod_like(b"HRT!", lambda r: bytes([0x00, 6 * 2, 0x0C, 0x30]) if r % 16 == 0 else bytes(4))


def pw_ins_table(data, nsmp=31, fine_vol=True):
    """31 x 8-byte packed sample descriptors: size(words) finetune volume loopstart loopsize."""
    out = b""
    for k in range(nsmp):
        if k == 0:
            out += P(">HBBHH", len(data) // 2, 0, 64, 0, 1)
        else:
            out += P(">HBBHH", 0, 0, 0, 0, 1)
    return out


def f_pw_ac1d():
    data = smp8(64, signed=True)
    pat_addr = 0x380
    track = bytes([0x20 | 5, 0x1C, 0x30]) + bytes([0x80 | 63])
    pat = bytes(12) + track * 4
    saddr = pat_addr + len(pat)
    h = bytes([1, 0x78]) + b"\xac\x1d" + P(">I", saddr) + pw_ins_table(data)
    h += P(">II", pat_addr, saddr) + bytes(4 * 126)
    h += bytes([0] * 128)
    h = h.ljust(pat_addr, b"\0")
    return h + pat + data


def f_pw_fcm():
    data = smp8(64, signed=True)
    h = b"FC-M" + bytes([1, 0]) + b"NAME" + z("seed fcm", 20) + b"INST" + pw_ins_table(data) + b"LONG" + bytes([1, 0x7F]) + b"PATT" + bytes([0]) + b"SONG"
    pat = b"".join((bytes([0x00, 0x7C, 0x0C, 0x30]) if r % 8 == 0 else bytes(4)) for r in range(256))
    return h + pat + b"SAMP" + data


def f_pw_skyt():
    data = smp8(64, signed=True)
    h = pw_ins_table(data) + bytes(8) + b"SKYT" + bytes([0]) + P(">HHHH", 1, 2, 0, 0) + bytes([0])
    trk = b""
    for t in range(2):
        for r in range(64):
            trk += bytes([1 + (r // 8 + t) % 12, 0x10, 0x0C, 0x30]) if r % 8 == 0 else bytes(4)
    return h + trk + data


def f_pw_ntp():
    data = smp8(64, signed=True)
    nins, ln, npat = 1, 1, 1
    hdr = b"MODU" + z("seed ntp", 16)
    body_off = 30 + nins * 8 + ln * 2 + npat * 2
    pat = bytearray()
    for r in range(64):
        if r % 8 == 0:
            pat += P(">H", 0x0003) + bytes([0x10 | 0x00, 0x7C, 0x0C, 0x30]) + bytes([0x10, 0x7C, 0x0C, 0x30])
        else:
            pat += P(">H", 0)
    ins = bytes([0, 64]) + P(">H", len(data) // 2) + bytes([0, 0, 0, 1])
    pat_addr = 0
    # BODY tag position = body_off - 4 + ... : the tag lives at u16(data+20) + 4
    body_hdr_pos = 30 + len(ins) + 2 * ln + 2 * npat
    h = hdr
    h = h[:20] + P(">H", body_hdr_pos - 4) + P(">HHH", nins, ln, npat) + P(">H", len(pat))
    h += ins + P(">H", 0) + P(">H", pat_addr)
    assert len(h) == body_hdr_pos, (len(h), body_hdr_pos)
    h += b"BODY" + bytes(pat)
    return h + b"SAMP" + data


def f_pw_prun2():
    data = smp8(64, signed=True)
    h = b"SNT!" + bytes(4) + pw_ins_table(data) + bytes([1, 0x7F]) + bytes(128)
    h = h.ljust(770, b"\0")
    pat = b""
    for r in range(256):
        if r % 16 == 0:
            pat += bytes([(5 + r // 64) << 1, 0x1C, 0x30])
        elif r % 16 == 8:
            pat += bytes([0xC0])
        else:
            pat += bytes([0x80])
    return h + pat + data


def f_pw_fuzzac():
    data = smp8(64, signed=True)
    h = b"M1.0" + bytes(2)
    for k in range(31):
        size = len(data) // 2 if k == 0 else 0
        h += z("fuzz smp" if k == 0 else "", 22) + bytes(38) + P(">HHH", size, 0, 1) + bytes([0, 64 if k == 0 else 0])
    assert len(h) == 2114
    nlen = 1
    h += bytes([nlen, 2]) + bytes(2)
    # track numbers: 4 voices x len entries of 4 bytes; use tracks 0 and 1
    h = h[:2118] + b"".join(bytes([c & 1, 0, 0, 0]) for c in range(4) for _ in range(nlen))
    trk = b""
    for t in range(2):
        for r in range(64):
            trk += bytes([0x01, 0xAC, 0x1C, 0x30]) if r % 8 == 0 else bytes(4)
    return h + trk + b"SEnd" + data


def f_pw_pp10():
    data = smp8(64, signed=True)
    h = pw_ins_table(data) + bytes([1, 0x78])
    lists = bytes([0] * 128) + bytes([1] * 128) + bytes([0] * 128) + bytes([1] * 128)
    h += lists
    h = h.ljust(762, b"\0")
    trk = b""
    for t in range(2):
        for r in range(64):
            trk += P(">I", (PERIODS[(r // 8 + t) % 8] << 16) | (0x1 << 12) | (0xC << 8) | 0x30) if r % 8 == 0 else bytes(4)
    return h + trk + data


def mp_cells(npat=1, mark74=True):
    cells = bytearray()
    for r in range(256 * npat):
        if r % 16 == 0:
            cells += P(">I", (0x1AC << 16) | (0x1 << 12) | (0xC << 8) | 0x30)
        else:
            cells += bytes(4)
    return cells


def f_pw_mp():
    data = smp8(64, signed=True)
    h = pw_ins_table(data) + bytes([1, 0x7F]) + bytes([0] * 128) + bytes(0)
    assert len(h) == 378
    cells = mp_cells()
    cells[(762 - 378):(762 - 378) + 1] = bytes([74])  # makes the ProPacker 1.0 test fail so Module Protector wins
    return h + bytes(cells) + data


def f_pw_mp_id():
    data = smp8(64, signed=True)
    h = b"TRK1" + pw_ins_table(data) + bytes([1, 0x7F]) + bytes([0] * 128) + bytes(0)
    return h + bytes(mp_cells()) + data


def f_pw_gmc():
    data = smp8(96, signed=True)
    h = b""
    for k in range(15):
        if k == 0:
            h += bytes(4) + P(">H", len(data) // 2) + bytes([0, 64]) + bytes(4) + P(">H", 1) + P(">H", 0)
        else:
            h += bytes(16)
    h = h.ljust(243, b"\0") + bytes([2])
    h += P(">HH", 0, 1024) + bytes(2 * 98)
    h = h.ljust(444, b"\0")
    pat = bytearray(2048)
    for r in range(0, 256, 16):
        pat[r * 4:r * 4 + 4] = bytes([0x01, 0xAC, 0x10, 0x00])
    return h + bytes(pat) + data


def np_common(v1):
    """NoisePacker v1/v2: 4 tracks of 64 rows x 3 bytes; v1 differs from v2 only in how the loop fields are scaled."""
    data = smp8(64, signed=True)
    nins = 1
    h = bytes([0x00, 0x1C]) + P(">HHH", 2, 2, 768)
    if v1:
        # loop start 16 bytes, loop size 40 (unscaled in v1 -> accepted there, rejected by the v2 test)
        ins = bytes(4) + P(">H", len(data) // 2) + bytes([0, 64]) + bytes(4) + P(">HH", 8, 40)
    else:
        ins = bytes(4) + P(">H", len(data) // 2) + bytes([0, 64]) + bytes(4) + P(">HH", 0, 0)
    assert len(ins) == 16
    h += ins + bytes(4) + P(">H", 0) + P(">HHHH", 0, 192, 384, 576)
    trk = b""
    for t in range(4):
        for r in range(64):
            if r % 8 == 0 and t < 2:
                trk += bytes([(3 + r // 8) * 2, 0x10, 0x00])
            else:
                trk += bytes([0, 0x10 if False else 0, 0])
    return h + trk + data


def f_pw_np2():
    return np_common(False)


def f_pw_np1():
    return np_common(True)


# ================================================================ Amiga-era container packers (each wraps a tiny M.K. MOD)
class BitSink:
    """Collects bits in consumption order (first appended = first read by the decoder)."""

    def __init__(self):
        self.bits = []

    def put(self, v, n):
        for k in range(n - 1, -1, -1):
            self.bits.append((v >> k) & 1)


def pp20_pack(data, eff=(9, 10, 12, 13)):
    """PowerPacker 2.0: literals plus greedy LZ matches, decoded back-to-front by libxmp."""
    rev = data[::-1]
    n = len(rev)
    bs = BitSink()

    def put_run(c):
        k, r = divmod(c - 1, 3)
        for _ in range(k):
            bs.put(3, 2)
        bs.put(r, 2)

    def put_match(dist, length):
        off = dist - 1
        if length <= 4:
            x = length - 2
            bs.put(x, 2)
            bs.put(off, eff[x])
        else:
            bs.put(3, 2)
            if off < 128:
                bs.put(0, 1)
                bs.put(off, 7)
            else:
                bs.put(1, 1)
                bs.put(off, eff[3])
            k, r = divmod(length - 5, 7)
            for _ in range(k):
                bs.put(7, 3)
            bs.put(r, 3)

    p = 0
    lit = []
    while p < n:
        best = (0, 0)
        if p >= 2:
            for dist in (1, 2, 3, 4, 8, 16, 64, 256, 1024):
                if dist > p or dist - 1 >= (1 << eff[3]):
                    continue
                L = 0
                while p + L < n and L < 60 and rev[p + L] == rev[p - dist + L]:
                    L += 1
                if L >= 2 and L > best[1]:
                    best = (dist, L)
        if best[1] >= 2 and best[0] - 1 < (1 << eff[3]):
            if lit:
                bs.put(0, 1)
                put_run(len(lit))
                for b in lit:
                    bs.put(b, 8)
                lit = []
            else:
                bs.put(1, 1)
            put_match(*best)
            p += best[1]
        else:
            lit.append(rev[p])
            p += 1
    if lit:
        bs.put(0, 1)
        put_run(len(lit))
        for b in lit:
            bs.put(b, 8)
    bits = bs.bits
    nbytes = (len(bits) + 7) // 8
    nbytes += (-nbytes) % 4
    stream = bytearray(nbytes)
    for k, b in enumerate(bits):
        if b:
            stream[nbytes - 1 - k // 8] |= 1 << (k % 8)
    return b"PP20" + bytes(eff) + bytes(stream) + P(">I", len(data) << 8)


def f_pp20():
    return pp20_pack(f_mod_mk())


def crc16_arc(data, crc=0):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def rle90(data):
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]
        r = 1
        while i + r < len(data) and data[i + r] == b and r < 255:
            r += 1
        if r >= 4:
            out += bytes([b]) if b != 0x90 else b"\x90\0"
            out += bytes([0x90, r])
            i += r
        elif b == 0x90:
            out += b"\x90\0"
            i += 1
        else:
            out.append(b)
            i += 1
    return bytes(out)


def arc_entry(method, name, packed, orig):
    h = b"\x1a" + bytes([method]) + z(name, 13) + P("<I", len(packed)) + P("<HH", 0, 0) + P("<H", crc16_arc(orig))
    if method != 1:
        h += P("<I", len(orig))
    return h + packed


def f_arc():
    m = f_mod_mk()
    return arc_entry(2, "SONG.MOD", m, m) + b"\x1a\x00"


def f_arc_old():
    m = f_mod_mk()
    return arc_entry(1, "SONG.MOD", m, m) + b"\x1a\x00"


def f_arc_rle():
    m = f_mod_mk()
    return arc_entry(3, "SONG.MOD", rle90(m), m) + b"\x1a\x00"


def f_arcfs():
    m = f_mod_mk()
    entries_len = 36 * 2
    data_off = 96 + entries_len
    ent = bytearray(36)
    ent[0] = 0x82
    ent[1:12] = z("SONG.MOD", 11)
    ent[12:16] = P("<I", len(m))
    ent[25] = 0
    ent[26:28] = P("<H", crc16_arc(m))
    ent[28:32] = P("<I", len(m))
    ent[32:36] = P("<I", 0)
    end = bytes(36)  # method 0 = end of directory
    h = b"Archive\0" + P("<IIIII", entries_len, data_off, 260, 260, 0x0A).ljust(88, b"\0")
    return h + bytes(ent) + end + m


def f_lzx():
    m = f_mod_mk()
    name = b"SONG.MOD"
    e = bytearray(31)
    e[2:6] = P("<I", len(m))
    e[6:10] = P("<I", len(m))
    e[15] = 0x0A
    e[22:26] = P("<I", zlib_crc32(m))
    e[30] = len(name)
    crc = zlib_crc32(bytes(e) + name)
    e[26:30] = P("<I", crc)
    return b"LZX\0\x0c\0\x0a\x04\0\0" + bytes(e) + name + m


def zlib_crc32(b):
    import zlib
    return zlib.crc32(b) & 0xFFFFFFFF


def xpk_checksum(buf):
    buf = buf + bytes((-len(buf)) % 4)
    s = 0
    for k in range(0, len(buf), 4):
        s ^= struct.unpack(">I", buf[k:k + 4])[0]
    return (s ^ (s >> 16)) & 0xFFFF


def f_sqsh():
    """XPK-SQSH made only of verbatim (type 0) chunks; each chunk checksum covers its data rounded up to a word."""
    m = f_mod_mk()
    out = b""
    for k in range(0, len(m), 1024):
        part = m[k:k + 1024]
        padded = part + bytes((-len(part)) % 4)
        out += bytes([0, 0]) + P(">HHH", xpk_checksum(padded), len(part), len(part)) + part
    body = bytes(20) + out
    return b"XPKF" + P(">I", len(body) + 8) + b"SQSH" + P(">I", len(m)) + body


def f_mmcmp():
    m = f_mod_mk()
    hdr_end = 1084
    pat_end = 1084 + 1024
    parts = [(0, m[:hdr_end]), (pat_end, m[pat_end:])]
    pats = m[hdr_end:pat_end]
    out = b"ziRCONia" + P("<H", 14) + P("<HH", 0x0111, 2) + P("<II", len(m), 0) + bytes([0, 0])
    offs = []
    # block 0: stored, two sub-blocks
    offs.append(len(out))
    body = b"".join(d for _, d in parts)
    out += P("<IIIHHHH", len(body), len(body) + 1, 0, 2, 0, 0, 0) + b"".join(P("<II", p, len(d)) for p, d in parts) + body
    # block 1: 8-bit "packed" with an identity translation table; numbits=7 stores each byte as 8 raw bits
    offs.append(len(out))
    ptab = bytes(range(256))
    out += P("<IIIHHHH", len(pats), len(ptab) + len(pats), 0, 1, 1, 256, 7) + P("<II", hdr_end, len(pats)) + ptab + pats
    tbl = len(out)
    out += b"".join(P("<I", o) for o in offs)
    return out[:18] + P("<I", tbl) + out[22:]


def noise(n, seed=1):
    """Deterministic pseudo-random payload for header-correct-only seeds."""
    out = bytearray()
    x = seed
    for _ in range(n):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def f_ice2():
    """Pack-Ice 'Ice!' header with a garbage payload (the bit-reversed LZ stream is not encoded)."""
    payload = noise(300, 2)
    total = 12 + len(payload)
    return b"Ice!" + P(">II", total, 2172) + payload


def f_ice1():
    """Old Pack-Ice 'ICE!' trailer (unpacked size + 'Ice!') with a garbage payload."""
    return noise(300, 3) + P(">I", 2172) + b"Ice!"


def f_s404():
    """StoneCracker S404 header with a garbage payload."""
    payload = noise(300, 4)
    return b"S404" + P(">III", 0, 2172, len(payload)) + payload


def pp2x(v30):
    """ProPacker 2.1 (cell-index references) / 3.0 (byte-offset references)."""
    data = smp8(64, signed=True)
    h = pw_ins_table(data) + bytes([1, 0x78])
    h += bytes([0] * 128) + bytes([1] * 128) + bytes([0] * 128) + bytes([1] * 128)
    scale = 4 if v30 else 1
    refs = b"".join(P(">H", (1 if r % 8 == 0 else 0) * scale) for _ in range(2) for r in range(64))
    cells = bytes(4) + P(">I", (0x1AC << 16) | (0x1 << 12) | (0xC << 8) | 0x30)
    return h + refs + P(">I", len(cells)) + cells + data


def f_pw_pp21():
    return pp2x(False)


def f_pw_pp30():
    return pp2x(True)


def f_pw_heatseek():
    data = smp8(64, signed=True)
    h = pw_ins_table(data) + bytes([1, 0x7F]) + bytes(128)
    pat = (bytes([0x01, 0xAC, 0x10, 0x00]) + bytes([0x80, 0, 0, 62])) * 4
    return h + pat + data


def f_pw_titanics():
    data = smp8(64, signed=True)
    pat = bytes([0x00]) + bytes([(5) | (0 << 6), 0x0C, 0x30]) + bytes([0x04]) + bytes([(8) | (1 << 6), 0x00, 0x00]) + bytes([0x84]) + bytes([(12) | (2 << 6), 0x00, 0x00])
    pat_addr = 180 + 4
    smp_addr = pat_addr + len(pat)
    h = b""
    for k in range(15):
        if k == 0:
            h += P(">I", smp_addr) + P(">H", len(data) // 2) + bytes([0, 64]) + P(">HH", 0, 1)
        else:
            h += bytes(4) + P(">H", 0) + bytes([0, 0]) + P(">HH", 0, 1)
    assert len(h) == 180
    h += P(">HH", pat_addr, 0xFFFF)
    return h + pat + data


def f_pw_tdd():
    data = smp8(64, signed=True)
    h = bytes([1, 0]) + bytes(128)
    base = 564
    for k in range(31):
        if k == 0:
            h += P(">IH", base, len(data) // 2) + bytes([0, 64]) + P(">IH", base, 1)
        else:
            h += P(">IH", base, 0) + bytes([0, 0]) + P(">IH", base, 0)
    assert len(h) == 564, len(h)
    pat = bytearray()
    for r in range(256):
        if r % 16 == 0:
            pat += bytes([0x00, 2 * (3 + r // 64), 0x0C, 0x30])
        else:
            pat += bytes(4)
    return h + data + bytes(pat)


def f_pw_eureka():
    data = smp8(64, signed=True)
    h = z("seed eu", 20)
    for k in range(31):
        h += z("", 22) + P(">HBBHH", len(data) // 2 if k == 0 else 0, 0, 64 if k == 0 else 0, 0, 1)
    h += bytes([1, 0]) + bytes(128)
    assert len(h) == 1080
    trk = bytes([0x01, 0xAC, 0x10, 0x00, 0xC0 | 62])
    trk_addr = 1080 + 4 + 8
    smp_off = trk_addr + len(trk)
    return h + P(">I", smp_off) + P(">HHHH", trk_addr, trk_addr, trk_addr, trk_addr) + trk + data


def f_pw_zen():
    data = smp8(64, signed=True)
    pat_ofs = 502
    pat_addr = pat_ofs + 8
    smp_addr = pat_addr + 8
    h = P(">I", pat_ofs) + bytes([0, 1])
    for k in range(31):
        size = len(data) // 2 if k == 0 else 0
        h += P(">H", 0) + bytes([0, 64 if k == 0 else 0]) + P(">HH", size, 1) + P(">II", smp_addr, smp_addr)
    assert len(h) == 502, len(h)
    h += P(">I", pat_addr) + P(">I", 0xFFFFFFFF)
    cells = bytes([0, 2 * 8, 0x1C, 0x30]) + bytes([255, 0, 0, 0])
    return h + cells + data


def f_pw_noiserun():
    data = smp8(64, signed=True)
    h = b""
    base = 1084 + 1024
    for k in range(31):
        if k == 0:
            h += bytes([0, 64]) + P(">I", base) + P(">H", len(data) // 2) + P(">I", base) + P(">HH", 1, 0)
        else:
            h += bytes([0, 0]) + P(">I", base) + P(">H", 0) + P(">I", base) + P(">HH", 0, 0)
    h = h.ljust(950, b"\0") + bytes([1, 0x7F]) + bytes(128)
    h = h.ljust(1080, b"\0") + b"M.K."
    # first nibble of every cell is >1 (an effect byte in NRU), which makes the plain MOD pattern validation reject the file
    pat = b"".join((bytes([0x28, 0x00, 2 * (6 + r // 16 % 6), 1 << 3]) if r % 16 == 0 else bytes([0x28, 0, 0, 0])) for r in range(256))
    return h + pat + data


SEEDS = [
    ("mod_mk.mod", f_mod_mk),
    ("mod_6chn.mod", f_mod_6chn),
    ("mod_flt4.mod", f_mod_flt4),
    ("mod_mk_excl.mod", f_mod_mk_excl),
    ("mod_st15.mod", f_mod_st15),
    ("xm.xm", f_xm),
    ("s3m.s3m", f_s3m),
    ("stm.stm", f_stm),
    ("it.it", f_it),
    ("669.669", f_669),
    ("mtm.mtm", f_mtm),
    ("ptm.ptm", f_ptm),
    ("far.far", f_far),
    ("okt.okt", f_okt),
    ("ult.ult", f_ult),
    ("mdl.mdl", f_mdl),
    ("stx.stx", f_stx),
    ("imf.imf", f_imf),
    ("amf.amf", f_amf),
    ("asylum.amf", f_asylum),
    ("liq.liq", f_liq),
    ("gdm.gdm", f_gdm),
    ("masi.psm", f_masi),
    ("masi16.psm", f_masi16),
    ("dbm.dbm", f_dbm),
    ("digi.digi", f_digi),
    ("emod.emod", f_emod),
    ("sfx.sfx", f_sfx),
    ("fnk.fnk", f_fnk),
    ("med2.med", f_med2),
    ("med3.med", f_med3),
    ("mmd1.med", f_mmd1),
    ("mmd0.med", f_mmd0),
    ("mmdc.med", f_mmdc),
    ("mmd3.med", f_mmd3),
    ("mmd2.med", f_mmd2),
    ("rtm.rtm", f_rtm),
    ("dt.dtm", f_dt),
    ("mgt.mgt", f_mgt),
    ("abk.abk", f_abk),
    ("umx_s3m.umx", f_umx_s3m),
    ("umx_it.umx", f_umx_it),
    ("xmf.xmf", f_xmf),
    ("hmn.hmn", f_hmn),
    ("coco.coco", f_coco),
    ("arch.musx", f_arch),
    ("ice.ice", f_ice),
    ("mtn.ice", f_mtn),
    ("ims.ims", f_ims),
    ("pt3.pt3", f_pt3),
    ("no.no", f_no),
    ("stim.stim", f_stim),
    ("gal5.j2b", f_gal5),
    ("gal4.j2b", f_gal4),
    ("muse5.muse", f_muse),
    ("muse4.muse", f_muse4),
    ("chip.chip", f_chip),
    ("sym.dsym", f_sym),
    ("med4.med", f_med4),
    ("pw_wn.pw", f_pw_wn),
    ("pw_prun1.pw", f_pw_prun1),
    ("pw_hrt.pw", f_pw_hrt),
    ("pw_ac1d.pw", f_pw_ac1d),
    ("pw_fcm.pw", f_pw_fcm),
    ("pw_skyt.pw", f_pw_skyt),
    ("pw_ntp.pw", f_pw_ntp),
    ("pw_prun2.pw", f_pw_prun2),
    ("pw_fuzzac.pw", f_pw_fuzzac),
    ("pw_pp10.pw", f_pw_pp10),
    ("pw_mp.pw", f_pw_mp),
    ("pw_mp_id.pw", f_pw_mp_id),
    ("pw_gmc.pw", f_pw_gmc),
    ("pw_np2.pw", f_pw_np2),
    ("pw_np1.pw", f_pw_np1),
    ("pw_pp21.pw", f_pw_pp21),
    ("pw_pp30.pw", f_pw_pp30),
    ("pw_heatseek.pw", f_pw_heatseek),
    ("pw_titanics.pw", f_pw_titanics),
    ("pw_tdd.pw", f_pw_tdd),
    ("pw_eureka.pw", f_pw_eureka),
    ("pw_zen.pw", f_pw_zen),
    ("pw_noiserun.pw", f_pw_noiserun),
    ("pp20.pp", f_pp20),
    ("mmcmp.mmcmp", f_mmcmp),
    ("sqsh.xpk", f_sqsh),
    ("arc.arc", f_arc),
    ("arc_old.arc", f_arc_old),
    ("arc_rle.arc", f_arc_rle),
    ("arcfs.arc", f_arcfs),
    ("lzx.lzx", f_lzx),
    ("ice2.ice", f_ice2),
    ("ice1.ice", f_ice1),
    ("s404.s404", f_s404),
]


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, fn in SEEDS:
        with open(os.path.join(out, name), "wb") as f:
            f.write(fn())
    print("wrote %d seeds to %s" % (len(SEEDS), out))


if __name__ == "__main__":
    main()
