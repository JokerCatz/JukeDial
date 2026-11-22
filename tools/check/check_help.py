#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""check_help: the help texts (app/main/ui/ui_help_texts.c) are well formed (fx apps T6). Standard library only.

  check_help.py [--file app/main/ui/ui_help_texts.c] [--selftest]

Every `ui_help_t NAME = { ... }` initializer needs a non-empty id, title_zh and title_en, and either body_zh and body_en or a body_fn;
ids are unique; every initializer is listed in UI_HELPS[] and every name in UI_HELPS[] is an initializer; no ui_help_t is defined anywhere else
under app/main (one home); no help string mentions the device word (旋鈕 / knob): bodies say 轉動 / turn.
Prints `check_help: ok (<n> help texts)`; on a problem `<file>:<line>: <reason>` per problem and exit 1.
"""
import argparse
import os
import re
import sys

WORD_RE = re.compile(r"旋鈕|knob", re.I)
TABLE_RE = re.compile(r"\bui_help_t\s*\*\s*const\s+UI_HELPS\s*\[\s*\]\s*=\s*\{")
OTHER_RE = re.compile(r"\bui_help_t\s+\w+\s*=")
INIT_RE = re.compile(r"\bui_help_t\s+(\w+)\s*=\s*\{")
FIELD_RE = re.compile(r"\.(\w+)\s*=")
STR_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def blank_comments(src):
    """Comments blanked (same length, newlines kept); strings are kept."""
    out = list(src)
    n, i = len(src), 0
    while i < n:
        two = src[i:i + 2]
        if src[i] == '"':
            i += 1
            while i < n and src[i] != '"' and src[i] != "\n":
                i += 2 if src[i] == "\\" else 1
            i += 1
        elif two == "/*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if src[k] != "\n":
                    out[k] = " "
            i = j
        elif two == "//":
            j = src.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        else:
            i += 1
    return "".join(out)


def init_end(text, open_at):
    """Index just past the '}' closing the '{' at open_at (strings skipped); len(text) if none."""
    depth, i, n = 0, open_at, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


def fields_of(body, base_line_of):
    """{name: (line, value text)} of one initializer body: a value runs to the next top-level comma."""
    out = {}
    pos = 0
    while True:
        m = FIELD_RE.search(body, pos)
        if not m:
            return out
        i, depth = m.end(), 0
        while i < len(body):
            c = body[i]
            if c == '"':
                i += 1
                while i < len(body) and body[i] != '"':
                    i += 2 if body[i] == "\\" else 1
            elif c in "({[":
                depth += 1
            elif c in ")}]":
                if depth == 0:
                    break
                depth -= 1
            elif c == "," and depth == 0:
                break
            i += 1
        out[m.group(1)] = (base_line_of(m.start()), body[m.end():i].strip())
        pos = i


def text_of(value):
    """The concatenated string literal value, or None when the value is not (only) string literals (NULL, a name)."""
    if not value or not STR_RE.fullmatch(re.sub(r'"\s+"', "", value)):
        return None
    return "".join(STR_RE.findall(value))


def check_text(src, name="ui_help_texts.c"):
    """([problem strings], number of initializers)."""
    text = blank_comments(src)
    probs, ids, n = [], {}, 0
    names = {}
    for m in INIT_RE.finditer(text):
        n += 1
        names[m.group(1)] = text.count("\n", 0, m.start()) + 1
        open_at = m.end() - 1
        end = init_end(text, open_at)
        body = text[open_at + 1:end - 1]
        line0 = text.count("\n", 0, open_at + 1) + 1

        def line_of(off, line0=line0, body=body):
            return line0 + body.count("\n", 0, off)

        f = fields_of(body, line_of)
        where = lambda k, line0=line0: f[k][0] if k in f else line0
        val = {k: text_of(v[1]) for k, v in f.items()}
        for k in ("id", "title_zh", "title_en"):
            if not val.get(k):
                probs.append("%s:%d: %s: %s is missing or empty" % (name, where(k), m.group(1), k))
        has_fn = "body_fn" in f and f["body_fn"][1] not in ("", "NULL", "0")
        has_zh, has_en = bool(val.get("body_zh")), bool(val.get("body_en"))
        if not has_fn and not (has_zh and has_en):
            probs.append("%s:%d: %s: needs body_zh and body_en, or a body_fn" % (name, where("body_zh"), m.group(1)))
        for k in ("title_zh", "title_en", "body_zh", "body_en", "web_zh", "web_en"):
            if val.get(k) and WORD_RE.search(val[k]):
                probs.append("%s:%d: %s: %s mentions the device (旋鈕/knob); say 轉動 / turn" % (name, where(k), m.group(1), k))
        i = val.get("id")
        if i:
            if i in ids:
                probs.append("%s:%d: %s: duplicate id \"%s\" (first on line %d)" % (name, where("id"), m.group(1), i, ids[i]))
            else:
                ids[i] = where("id")
    t = TABLE_RE.search(text)
    if t:
        listed = re.findall(r"&\s*(\w+)", text[t.end():init_end(text, t.end() - 1)])
        for nm, ln in names.items():
            if nm not in listed:
                probs.append("%s:%d: %s is not listed in UI_HELPS[] (help open and ui_help_find cannot find it)" % (name, ln, nm))
        for nm in listed:
            if nm not in names:
                probs.append("%s:%d: UI_HELPS[] names %s, which is not a ui_help_t initializer in this file" % (name, text.count("\n", 0, t.start()) + 1, nm))
    elif n:
        probs.append("%s:1: no UI_HELPS[] table" % name)
    return probs, n


def check_second_homes(files):
    """files: {path: source}; a ui_help_t defined in any file but ui_help_texts.c is a problem."""
    probs = []
    for path, src in sorted(files.items()):
        if os.path.basename(path) == "ui_help_texts.c":
            continue
        text = blank_comments(src)
        for m in OTHER_RE.finditer(text):
            probs.append("%s:%d: a ui_help_t is defined outside ui_help_texts.c (every help text lives there)" % (path, text.count("\n", 0, m.start()) + 1))
    return probs


GOOD = '''const ui_help_t HELP_A = { .id = "a", .title_zh = "說明", .title_en = "Help", .body_zh = "好", .body_en = "ok" };
const ui_help_t HELP_B = { .id = "b", .title_zh = "t", .title_en = "t", .body_fn = fn_b, .arg = 1 };
const ui_help_t *const UI_HELPS[] = { &HELP_A, &HELP_B };
'''
# (name, source, expected number of problems, expected substring)
CASES = [
    ("good", GOOD, 0, None),
    ("missing title_en", GOOD.replace(', .title_en = "Help"', ""), 1, "title_en"),
    ("empty id", GOOD.replace('.id = "a"', '.id = ""'), 1, "id is missing"),
    ("no body and no body_fn", 'const ui_help_t H = { .id = "a", .title_zh = "x", .title_en = "x", .body_zh = NULL, .body_en = NULL }; const ui_help_t *const UI_HELPS[] = { &H };', 1, "body_fn"),
    ("only one body language", 'const ui_help_t H = { .id = "a", .title_zh = "x", .title_en = "x", .body_zh = "x" }; const ui_help_t *const UI_HELPS[] = { &H };', 1, "body_fn"),
    ("旋鈕 in a body", GOOD.replace('"好"', '"轉旋鈕"'), 1, "旋鈕"),
    ("knob in a body", GOOD.replace('"ok"', '"Turn the Knob"'), 1, "knob"),
    ("duplicate id", GOOD.replace('.id = "b"', '.id = "a"'), 1, "duplicate id"),
    ("knob only in a comment", GOOD + "/* knob 旋鈕 */ // knob\n", 0, None),
    ("concatenated literals", 'const ui_help_t H = { .id = "a", .title_zh = "x", .title_en = "x", .body_zh = "a\\n\\n"\n "b", .body_en = "c" }; const ui_help_t *const UI_HELPS[] = { &H };', 0, None),
    ("table is not an initializer", GOOD, 0, None),
    ("orphan: initializer not in the table", GOOD.replace("{ &HELP_A, &HELP_B }", "{ &HELP_A }"), 1, "HELP_B is not listed"),
    ("unknown name in the table", GOOD.replace("&HELP_B }", "&HELP_B, &HELP_C }"), 1, "names HELP_C"),
]


def selftest():
    bad = 0
    for name, src, want_n, want_sub in CASES:
        probs, _n = check_text(src)
        ok = len(probs) == want_n and (want_sub is None or want_sub in probs[0])
        if not ok:
            print("check_help selftest FAIL: %s: %s" % (name, probs))
            bad += 1
    for name, files, want in (("second home", {"ui/scr_x.c": "static const ui_help_t X = { .id = \"x\" };"}, 1),
                              ("own file is fine", {"ui/ui_help_texts.c": GOOD}, 0),
                              ("header typedef and pointers are fine", {"ui/ui_help.h": "} ui_help_t;\nconst ui_help_t *find(const char *id);\nui_help_t *p = 0;"}, 0)):
        probs = check_second_homes(files)
        if len(probs) != want:
            print("check_help selftest FAIL: %s: %s" % (name, probs))
            bad += 1
    if bad:
        print("check_help selftest: %d problems" % bad)
        return 1
    print("check_help selftest: ok (%d cases)" % (len(CASES) + 3))
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="help texts are well formed")
    ap.add_argument("--file", default=os.path.join(here, "..", "..", "app", "main", "ui", "ui_help_texts.c"))
    ap.add_argument("--root", default=os.path.join(here, "..", "..", "app", "main"))
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    with open(a.file, encoding="utf-8") as f:
        src = f.read()
    probs, n = check_text(src, os.path.relpath(a.file))
    files = {}
    for d, _dirs, names in os.walk(a.root):
        for nm in names:
            if nm.endswith((".c", ".h", ".cpp")):
                with open(os.path.join(d, nm), encoding="utf-8", errors="replace") as f:
                    files[os.path.relpath(os.path.join(d, nm))] = f.read()
    probs += check_second_homes(files)
    if not n:
        probs.append("%s:1: no ui_help_t initializer found" % os.path.relpath(a.file))
    if probs:
        print("\n".join(probs))
        return 1
    print("check_help: ok (%d help texts)" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
