#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Builds 單字 vocabulary decks (glyph packs of kind 4, spec an internal note).

Words come from ECDICT (https://github.com/skywind3000/ECDICT, MIT licence, docs/licenses/ECDICT-MIT.txt): the
Chinese meanings are simplified Chinese there and are converted to traditional characters with OpenCC (s2tw). Roots and
affixes come from tools/gen/vocab/roots.tsv (our own list). Rendering is the same as tools/gen/make_lists.py (Noto Sans
CJK TC, sizes 24 and 40 px, A8 masks).

Deck layout: root = title; its children = cards in teaching order (common first); card text = the word, weight =
level band (1 + frequency rank / 1000, max 254), flags = class (1 n, 2 v, 3 adj, 4 adv, 5 other, 8 prefix,
9 root, 10 suffix); child 0 = meaning ("n. 能力；才幹"), child 1 = example words (roots only).

Needs Pillow, OpenCC (pip install opencc) and the fonts of make_lists.py. Easiest:
    python3 -m venv --system-site-packages .local/venv && .local/venv/bin/pip install opencc
ECDICT (66 MB, never committed):
    mkdir -p .local/ecdict && curl -L -o .local/ecdict/ecdict.csv \\
        https://raw.githubusercontent.com/skywind3000/ECDICT/39cfb9672c0906b63569e0453c2e6abfb9ab182a/ecdict.csv

usage: .local/venv/bin/python tools/gen/make_vocab.py [--out DIR] [deck ...]
  decks: zk gk cet4 cet6 ky toefl ielts gre (ECDICT exam tags), frqA-B (frequency ranks A..B, e.g. frq1-3000),
         roots (tools/gen/vocab/roots.tsv), sample (the built-in 100 words -> app/main/assets/vocab/sample.zip, deflate)
  default: zk gk cet4 toefl roots into tools/gen/build-vocab/; copy to /sdcard/vocab/ with tools/board/tf_put.py or USB drive mode
"""
import argparse, csv, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import glyphpack as gp
import make_lists as ml

ECDICT = os.path.join(ROOT, ".local", "ecdict", "ecdict.csv")
ROOTS = os.path.join(HERE, "vocab", "roots.tsv")
SAMPLE_OUT = os.path.join(ROOT, "app", "main", "assets", "vocab", "sample.zip")

TAGS = {  # ECDICT tag -> deck title. ECDICT's exam lists are mainland China ones: 中考 is about 國中 level, 高考 高中
    "zk": "中考詞彙", "gk": "高考詞彙", "cet4": "CET-4", "cet6": "CET-6", "ky": "考研詞彙", "toefl": "TOEFL",
    "ielts": "IELTS", "gre": "GRE",
}
POS = {  # translation prefix -> (label, class)
    "n.": ("n.", 1), "v.": ("v.", 2), "vt.": ("v.", 2), "vi.": ("v.", 2), "a.": ("adj.", 3), "adj.": ("adj.", 3),
    "adv.": ("adv.", 4), "prep.": ("prep.", 5), "conj.": ("conj.", 5), "pron.": ("pron.", 5), "num.": ("num.", 5),
    "interj.": ("int.", 5), "int.": ("int.", 5), "art.": ("art.", 5), "aux.": ("aux.", 5), "abbr.": ("abbr.", 5),
}
CLASS = {"prefix": 8, "root": 9, "suffix": 10}
MAX_CJK = 9          # meaning length (CJK characters after the part of speech)
ANSWER_W = 118       # px of an answer box's text at 24 px (app/main/ui/scr_vocab.c: QW - 18)
# after OpenCC s2tw (one character at a time where it has no phrase): Taiwan forms it misses. Whole senses, then
# substrings. Not s2twp: its phrase table misfires on short senses (局部 -> 區域性).
TW_SENSE = {"周": "週"}
TW_SUB = {"蠟制": "蠟製", "羊毛制": "羊毛製", "制乾草": "製乾草"}

_cc = None


def tw(s):
    global _cc
    if _cc is None:
        try:
            import opencc
        except ImportError:
            sys.exit("needs OpenCC: python3 -m venv --system-site-packages .local/venv && .local/venv/bin/pip install opencc")
        _cc = opencc.OpenCC("s2tw")       # not s2twp: its phrase table misfires on short senses (局部 -> 區域性)
    return _cc.convert(s)


def tw_fix(meaning):
    """'星期；周' -> '星期；週' (TW_SENSE per sense, then TW_SUB)."""
    out = "；".join(TW_SENSE.get(x, x) for x in meaning.split("；"))
    for a, b in TW_SUB.items():
        out = out.replace(a, b)
    return out


def short_meaning(translation, pos=""):
    """The main part of speech and its first sense (and the second when the first is very short), trimmed:
    ('n. 能力', class) or None. Main = a modal (aux.) line if there is one, else ECDICT's pos field (n:46/v:54) when
    it has one, else the line with the most senses (the first of equals): ECDICT lists rare uses first at times
    (can: vt. 裝罐 before aux. 能)."""
    cand = []
    lines = [l.strip() for l in translation.replace("\\r", "").replace("\\n", "\n").split("\n") if l.strip()]
    for k, line in enumerate(lines):
        m = re.match(r"^([a-z]+\.)\s*(.*)$", line)
        if not m or m.group(1) not in POS:
            continue
        label, cls = POS[m.group(1)]
        body = re.sub(r"\[[^\]]*\]", "", m.group(2))                 # [计] domain tags
        body = re.sub(r"[（(][^）)]*[）)]", "", body)                   # (notes)
        senses = []
        for s in re.split(r"[,，;；]", body):
            s = s.strip(" .…")
            if s and re.search(r"[一-鿿]", s) and s not in senses:
                senses.append(s)
        if senses:
            cand.append((k, m.group(1), label, cls, senses))
    if not cand:
        return None
    share = {}
    for p in pos.split("/"):
        if ":" in p:
            t, v = p.split(":", 1)
            share[{"n": 1, "v": 2, "j": 3, "a": 3, "r": 4}.get(t, 5)] = int(v) if v.isdigit() else 0
    k, _, label, cls, senses = max(cand, key=lambda c: (c[1] == "aux.", share.get(c[3], -1), len(c[4]), -c[0]))
    out = senses[0][:MAX_CJK]
    if len(out) <= 2 and len(senses) > 1 and len(out) + 1 + len(senses[1]) <= MAX_CJK:
        out += "；" + senses[1]
    return "%s %s" % (label, tw_fix(tw(out))), cls


def rank(r):
    f = [int(x) for x in (r["frq"], r["bnc"]) if x and x.isdigit() and int(x) > 0]
    return min(f) if f else 99999


def ecdict_rows():
    if not os.path.exists(ECDICT):
        sys.exit("ECDICT missing: %s (see the header of this script for the download)" % ECDICT)
    with open(ECDICT, encoding="utf-8") as f:
        yield from csv.DictReader(f)


def word_cards(pred, limit=None, title=""):
    cards, seen = [], set()
    for r in ecdict_rows():
        w = r["word"].strip()
        if not w or w.lower() in seen or not pred(r) or not re.fullmatch(r"[A-Za-z][A-Za-z' .-]*", w):
            continue
        sm = short_meaning(r["translation"], r["pos"])
        if not sm:
            continue
        seen.add(w.lower())
        cards.append(dict(word=w, meaning=sm[0], cls=sm[1], rank=rank(r)))
    cards.sort(key=lambda c: (c["cls"] == 5, c["rank"], c["word"].lower()))   # function words (the, of) last
    if limit:
        cards = cards[:limit]
    return cards


def root_cards():
    cards = []
    for line in open(ROOTS, encoding="utf-8"):
        if not line.strip() or line.startswith("#"):
            continue
        cls, part, meaning, examples = line.rstrip("\n").split("\t")
        cards.append(dict(word=part, meaning=meaning, cls=CLASS[cls], rank=0, extra=" · ".join(e.strip() for e in examples.split(","))))
    return cards


def build(title, cards, deflate=False):
    m = dict(kind="vocab", title=title, groups=[])
    ids, glyph_list = {}, []

    def gid(c):
        if c not in ids:
            ids[c] = len(glyph_list)
            glyph_list.append(c)
        return ids[c]

    def enc(s):
        return [gid(c) for c in ml.clusters(s)]

    f24 = ml.cjk_font(24)

    def width(s):
        return sum(max(1, round(f24.getlength(c))) for c in ml.clusters(s))

    def enc_meaning(s):
        """Two senses that need two lines in an answer box: break at the '；' between them (a forced line break,
        PC_GP_NL), not wherever the first line runs out - when each sense fits on its line."""
        if "；" in s and width(s) > ANSWER_W:
            a, b = s.split("；", 1)
            if width(a + "；") <= ANSWER_W and width(b) <= ANSWER_W:
                return enc(a + "；") + [gp.NL] + enc(b)
        return enc(s)

    gid(" ")
    ell = gid("…")
    nodes = []
    for c in cards:
        band = max(1, min(254, 1 + c["rank"] // 1000)) if c["rank"] else 1
        ch = [gp.Node(enc_meaning(c["meaning"]))]
        if c.get("extra"):
            ch.append(gp.Node(enc(c["extra"])))
        nodes.append(gp.Node(enc(c["word"]), weight=band, flags=c["cls"], children=ch))
        m["groups"].append(dict(name=c["word"], items=[dict(text=c["meaning"])] + ([dict(text=c["extra"])] if c.get("extra") else [])))
    root = gp.Node(enc(title), children=nodes)
    fonts = {px: ml.cjk_font(px) for px, _ in ml.SIZES}
    fonts["emoji"] = ml.ImageFont.truetype(ml.EMOJI, 109)
    glyphs = [dict(flags=(gp.GF_SPACE if c.isspace() else 0) | (gp.GF_WORD if ml.is_word(c) else 0),
                   sizes=[ml.render(c, px, lh, fonts) for px, lh in ml.SIZES]) for c in glyph_list]
    # source.json: the cards as a model the web page could show (not edited there yet)
    return gp.build(gp.KINDS["vocab"], ml.SIZES, glyphs, [], root, ell, source=m, deflate=deflate), len(glyph_list)


def deck(name):
    if name in TAGS:
        cs = word_cards(lambda r: name in r["tag"].split())
        return TAGS[name] + " %d 字" % len(cs), cs, False
    m = re.fullmatch(r"frq(\d+)-(\d+)", name)
    if m:
        a, b = int(m.group(1)), int(m.group(2))
        cs = word_cards(lambda r: a <= rank(r) <= b and (r["collins"] or r["oxford"] == "1" or r["tag"]))
        return "常用字 第 %d–%d 名 %d 字" % (a, b, len(cs)), cs, False
    if name == "roots":
        cs = root_cards()
        return "字根字首字尾 %d 個" % len(cs), cs, False
    if name == "sample":
        cs = [c for c in word_cards(lambda r: "zk" in r["tag"].split()) if c["cls"] <= 4 and len(c["word"]) >= 3 and c["word"].islower() and c["rank"] >= 150][:100]
        return "中考範例 %d 字" % len(cs), cs, True
    sys.exit("unknown deck %s" % name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(HERE, "build-vocab"))
    ap.add_argument("decks", nargs="*", default=["zk", "gk", "cet4", "toefl", "roots"])
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name in a.decks:
        title, cards, deflate = deck(name)
        z, ng = build(title, cards, deflate)
        p = SAMPLE_OUT if name == "sample" else os.path.join(a.out, name + ".zip")
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(z)
        print("%s: %s, %d cards, %d glyphs, %d bytes" % (p, title, len(cards), ng, len(z)))


if __name__ == "__main__":
    main()
