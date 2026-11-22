#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""ui_lint: pages may not hand-build UI (rules R1-R6, spec 2026-11-13 section 5). Standard library only.

  ui_lint.py [--root app/main] [--allow app/main/ui/ui_lint.allow] [--selftest] [--seed]

Files: every <root>/**/*.c except third_party/, and every header under <root>/ui/ (a static inline builder in a header is
the same code; final review I-2). A file is a piece when the allowance table says `piece`
(or lists it as `page`: then it is a page). Pieces are not checked. Everything else is checked when it is a
page: every file under ui/ that the table does not call a piece (ui/scr_*.[ch] can never be one); any other file only
when, comments and strings removed, it still has an `lv_` token (a service file with no LVGL call is not a page).
R1 widget creator   R2 input handling   R3 geometry/position call   R4 screen-size literal   R5 device word in T()
R6 explanation text on a page (fx apps T6, spec 10.3): a T() literal (adjacent pieces joined) of 12+ CJK characters or ending in 。 in a text call or an snprintf-family call
The table is a ratchet: a count above the allowance fails ("new hand-built UI"), below it fails too
("lower the allowance ..."), so a page's line only ever shrinks.
Limit of any ratchet: it counts hits, not what they are. A page can swap one harmless hit for a harmful one (say
a remove_flag(CLICKABLE) for an add_flag) and keep its count; the whys in the table and the review are the other half.
"""
import argparse
import os
import re
import sys
import tempfile

RULES = (1, 2, 3, 4, 5, 6)

# R1: a widget constructor's name, called or not (a function pointer to it is the same widget), and the class-based constructor
R1_RE = re.compile(r"\blv_\w+_create\b|\blv_obj_class_create_obj\b")
R2_CB_RE = re.compile(r"\blv_obj_add_event_cb\s*\(")
R2_CODE_RE = re.compile(r"\bLV_EVENT_(ALL|CLICKED|SHORT_CLICKED|SINGLE_CLICKED|DOUBLE_CLICKED|TRIPLE_CLICKED|PRESSED|PRESSING|"
                        r"RELEASED|LONG_PRESSED|LONG_PRESSED_REPEAT|GESTURE|KEY|SCROLL|VALUE_CHANGED)\b")
R2_NAME_RE = re.compile(r"\s*LV_EVENT_\w+\s*$")   # the event argument must be one LV_EVENT_ name (a code in a variable is not checkable)
R2_OTHER_RE = re.compile(r"\bLV_OBJ_FLAG_CLICKABLE\b|\blv_indev_(?!t\b)\w*|\bbsp_knob_set_hook\b|\blv_obj_scroll_by\b")
# R3: geometry and position: the object calls, the same properties as a style on the object or in a local lv_style_t (not colours, text, ...)
R3_STYLE = r"(?:pad|radius|width|height|translate|x|y|align|min_|max_|size|margin|transform_width|transform_height)\w*"
R3_RE = re.compile(r"\blv_obj_(?:set_size|set_width|set_height|set_pos|set_x|set_y|set_align|align|align_to|center|"
                   r"set_ext_click_area|set_flex_\w+|set_grid_\w+|set_content_width|set_content_height|set_layout)\s*\(|"
                   r"\blv_obj_set_style_" + R3_STYLE + r"\s*\(|"
                   r"\blv_style_set_(?:" + R3_STYLE + r"|flex_\w+|grid_\w+|layout)\s*\(|"
                   r"\bLV_ALIGN_\w+|\b(?:ui_button|ui_label|ui_text_button|ui_icon_button)\s*\(")
R4_RE = re.compile(r"(?<![\w.])(?:360|180|460|230)(?![\w.])")
R4_INC_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"ui_geom\.h"', re.M)
R5_T_RE = re.compile(r"(?<![\w.])T(?:_RAW)?\s*\(")
R5_WORD_RE = re.compile(r"旋鈕|knob", re.I)
# R6: the text sinks of a page; a T("...") literal in their arguments that reads like an explanation (explanations live behind 說明)
R6_SINK_RE = re.compile(r"\b(?:ui_list_note|lv_label_set_text|lv_label_set_text_fmt|lv_label_set_text_static|snprintf|sprintf|vsnprintf)\s*\(")
R6_LIT_RE = re.compile(r'(?<![\w.])T(?:_RAW)?\s*\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')   # adjacent literals are one text
R6_PIECE_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
R6_CJK_RE = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
R6_MIN_CJK = 12
LV_TOKEN_RE = re.compile(r"\blv_\w+")
R6_ID_RE = re.compile(r"(?<![\w.])T\s*\(\s*(S_[A-Z0-9_]+)\s*\)")   # i18n A: T(S_xxx); its texts are in tools/gen/i18n/strings.tsv
ID_ONLY_RE = re.compile(r"\s*(S_[A-Z0-9_]+)\s*")
_IDS = {}


def id_texts(name):
    """(zh_tw, en) of a string id of tools/gen/i18n/strings.tsv ('', '' for an unknown one: the compiler finds that)"""
    if not _IDS:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "gen", "i18n", "strings.tsv")
        if os.path.isfile(path):
            for ln in open(path, encoding="utf-8"):
                f = ln.rstrip("\n").split("\t")
                if len(f) >= 4 and not ln.startswith("#"): _IDS[f[0]] = (f[1].replace("\\n", "\n"), f[3])
        _IDS[""] = ("", "")
    return _IDS.get(name, ("", ""))


def strip(src):
    """Return (blank, keep): comments blanked in both; in `blank` string and char literal contents are blanked
    too. Same length as src, every newline kept, so line numbers do not move."""
    n = len(src)
    blank = list(src)
    keep = list(src)
    i = 0
    while i < n:
        c = src[i]
        two = src[i:i + 2]
        if two == "/*":
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if src[k] != "\n":
                    blank[k] = keep[k] = " "
            i = j
        elif two == "//":
            j = src.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                blank[k] = keep[k] = " "
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c and src[j] != "\n":
                j += 2 if src[j] == "\\" else 1
            j = min(j, n - 1)
            for k in range(i + 1, j):
                if src[k] != "\n":
                    blank[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(blank), "".join(keep)


def match_paren(text, open_at):
    """Index just past the ')' that closes the '(' at open_at (text has no strings/comments); len(text) if none."""
    depth = 0
    for k in range(open_at, len(text)):
        if text[k] == "(":
            depth += 1
        elif text[k] == ")":
            depth -= 1
            if depth == 0:
                return k + 1
    return len(text)


def split_args(text):
    """The top-level comma-separated arguments of a call's text between its parens (no strings/comments in it)."""
    out, depth, cur = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur)); cur = []
        else:
            cur.append(ch)
    out.append("".join(cur))
    return out


def lint_text(src, kind):
    """[(line, rule)] for one file's text, in order of line. kind is 'page' or 'piece' (a piece reports nothing)."""
    if kind != "page":
        return []
    blank, keep = strip(src)
    out = []

    def line_of(pos):
        return src.count("\n", 0, pos) + 1

    for m in R1_RE.finditer(blank):
        if not m.group(0).startswith("lv_timer_create"):
            out.append((line_of(m.start()), 1))
    for m in R2_CB_RE.finditer(blank):
        end = match_paren(blank, m.end() - 1)
        args = split_args(blank[m.end():end - 1])
        if R2_CODE_RE.search(blank, m.end(), end) or len(args) < 3 or not R2_NAME_RE.match(args[2]):
            out.append((line_of(m.start()), 2))
    for m in R2_OTHER_RE.finditer(blank):
        out.append((line_of(m.start()), 2))
    for m in R3_RE.finditer(blank):
        out.append((line_of(m.start()), 3))
    for m in R4_RE.finditer(blank):
        out.append((line_of(m.start()), 4))
    for m in R4_INC_RE.finditer(keep):
        out.append((line_of(m.start()), 4))
    # R5: the strings inside T( ... ) only; blank has the same offsets as keep, so its parens are safe to match
    # (one finding per source line, however many T() calls the line holds)
    r5_lines = set()
    for m in R5_T_RE.finditer(blank):
        end = match_paren(blank, m.end() - 1)
        body = keep[m.end():end]
        idm = ID_ONLY_RE.fullmatch(body[:-1] if body.endswith(")") else body)
        if idm: body = " ".join(id_texts(idm.group(1)))
        if R5_WORD_RE.search(body):
            r5_lines.add(line_of(m.start()))
    out.extend((ln, 5) for ln in r5_lines)
    # R6: one finding per sink call whose arguments hold a long or full-sentence T() literal (strings kept: `keep`)
    for m in R6_SINK_RE.finditer(blank):
        end = match_paren(blank, m.end() - 1)
        for lit in R6_LIT_RE.finditer(keep, m.end(), end):
            text = "".join(R6_PIECE_RE.findall(lit.group(1)))
            if len(R6_CJK_RE.findall(text)) >= R6_MIN_CJK or text.rstrip().endswith("。"):
                out.append((line_of(m.start()), 6))
                break
        else:
            for idm in R6_ID_RE.finditer(keep, m.end(), end):
                text = id_texts(idm.group(1))[0]
                if len(R6_CJK_RE.findall(text)) >= R6_MIN_CJK or text.rstrip().endswith("。"):
                    out.append((line_of(m.start()), 6))
                    break
    out.sort()
    return out


def is_page(rel, kind_on_table, src):
    """Whether a file is checked. kind_on_table is 'piece', 'page' or None (not on the table)."""
    if kind_on_table == "piece":
        return False
    if kind_on_table == "page":
        return True
    if rel.startswith("ui/"):
        return True  # ui/scr_*.[ch], and any ui/ file not declared a piece
    return bool(LV_TOKEN_RE.search(strip(src)[0]))


def read_allow(path):
    """{path: (kind, {rule: n}, why)}; raises ValueError('<line>: ...') on a malformed line."""
    table = {}
    with open(path, encoding="utf-8") as f:
        for no, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            tok = line.split(None, 1)
            if len(tok) < 2:
                raise ValueError("%d: need `path kind ...`" % no)
            rest = tok[1].split(None, len(RULES) + 1)
            kind = rest[0]
            if tok[0] in table:
                raise ValueError("%d: %s listed twice" % (no, tok[0]))
            if kind == "piece":
                why = " ".join(rest[1:])
                if re.match(r"ui/scr_[^/]*\.[ch]$", tok[0]):
                    raise ValueError("%d: %s is a page: `piece` is not allowed for ui/scr_*.[ch]" % (no, tok[0]))
                if not why.strip():
                    raise ValueError("%d: piece line is `path piece why` (why not empty)" % no)
                table[tok[0]] = ("piece", {}, why)
            elif kind == "page":
                n = len(RULES)
                if len(rest) < n + 2 or not all(x.isdigit() for x in rest[1:n + 1]) or not rest[n + 1].strip():
                    raise ValueError("%d: page line is `path page R1 R2 R3 R4 R5 R6 why` (why not empty)" % no)
                table[tok[0]] = ("page", {r: int(rest[r]) for r in RULES}, rest[n + 1])
            else:
                raise ValueError("%d: kind must be page or piece" % no)
    return table


def check_counts(path, counts, table):
    """Allowance-mismatch messages for one page. A page absent from the table has allowance 0."""
    allow = table[path][1] if path in table else {}
    msgs = []
    for r in RULES:
        a, b = counts.get(r, 0), allow.get(r, 0)
        if a > b:
            msgs.append("%s: R%d: count %d, allowance %d (new hand-built UI)" % (path, r, a, b))
        elif a < b:
            msgs.append("%s: R%d: count %d, allowance %d (lower the allowance to %d in ui_lint.allow)"
                        % (path, r, a, b, a))
    return msgs


def collect(root):
    files = []
    for d, dirs, names in os.walk(root):
        dirs[:] = sorted(x for x in dirs if x != "third_party")
        for nm in sorted(names):
            rel = os.path.relpath(os.path.join(d, nm), root).replace(os.sep, "/")
            if nm.endswith(".c") or (nm.endswith(".h") and rel.startswith("ui/")):
                files.append(rel)
    return sorted(files)


def scan(root, table):
    """[(rel, kind, src, findings)] for every scanned file (findings only for pages)."""
    res = []
    for rel in collect(root):
        with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
            src = f.read()
        on = table[rel][0] if rel in table else None
        kind = "page" if is_page(rel, on, src) else "piece"
        res.append((rel, kind, src, lint_text(src, kind)))
    return res


def counts_of(findings):
    c = {}
    for _line, rule in findings:
        c[rule] = c.get(rule, 0) + 1
    return c


def run(root, allow_path, seed):
    try:
        table = read_allow(allow_path) if os.path.exists(allow_path) or not seed else {}
    except (OSError, ValueError) as e:
        print("ui_lint: %s: %s" % (allow_path, e))
        return 1
    scanned = scan(root, table)
    if seed:
        for rel, kind, src, _fnd in scanned:
            if rel.startswith("ui/") and not os.path.basename(rel).startswith("scr_"):
                print("%-27s piece  TODO" % rel)
                continue
            c = counts_of(lint_text(src, "page"))
            if c:
                print("%-27s page   %s  TODO" % (rel, " ".join(str(c.get(r, 0)) for r in RULES)))
        return 0
    errs = []
    known = {rel for rel, *_ in scanned}
    for p in table:
        if p not in known:
            errs.append("%s: on the allowance table but not a scanned file (.c, or .h under ui/) under %s" % (p, root))
    pages = 0
    for rel, kind, src, fnd in scanned:
        if kind != "page":
            continue
        pages += 1
        lines = src.split("\n")
        c = counts_of(fnd)
        msgs = check_counts(rel, c, table)
        for r in RULES:
            if c.get(r, 0) > (table[rel][1].get(r, 0) if rel in table else 0):
                for ln, rule in fnd:
                    if rule == r:
                        errs.append("%s:%d: R%d: %s" % (rel, ln, r, lines[ln - 1].strip()))
        errs.extend(msgs)
    if errs:
        print("\n".join(errs))
        print("ui_lint: %d problems" % len(errs))
        return 1
    print("ui_lint: ok (%d files, %d pages)" % (len(scanned), pages))
    return 0


# (name, kind, source, expected {rule: count})
CASES = [
    ("R1 page", "page", "void f(void){ lv_button_create(p); }", {1: 1}),
    ("R1 piece", "piece", "void f(void){ lv_button_create(p); }", {}),
    ("R2 page", "page", "lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, 0);", {2: 1}),
    ("R2 piece", "piece", "lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, 0);", {}),
    ("R3 page", "page", "lv_obj_set_size(b, 10, 10);", {3: 1}),
    ("R3 piece", "piece", "lv_obj_set_size(b, 10, 10);", {}),
    ("R4 page", "page", "int x = 360;", {4: 1}),
    ("R4 piece", "piece", "int x = 360;", {}),
    ("R5 page", "page", 'ui_x(T("轉旋鈕", "turn"));', {5: 1}),
    ("R5 piece", "piece", 'ui_x(T("轉旋鈕", "turn"));', {}),
    ("comment lv_button_create", "page", "/* lv_button_create( */\n// lv_button_create(\n", {}),
    ("string lv_button_create", "page", 'puts("lv_button_create(");', {}),
    ("lv_timer_create", "page", "lv_timer_create(cb, 10, 0);", {}),
    ("lv_draw_buf_create counts as R1", "page", "lv_draw_buf_create(1, 2, 3, 4);", {1: 1}),
    ("event cb across lines", "page", "lv_obj_add_event_cb(b, cb,\n LV_EVENT_CLICKED, 0);", {2: 1}),
    ("LV_EVENT_DELETE", "page", "lv_obj_add_event_cb(b, cb, LV_EVENT_DELETE, 0);", {}),
    ("0x360", "page", "int x = 0x360;", {}),
    ("1360", "page", "int x = 1360;", {}),
    ("180.0f", "page", "float x = 180.0f;", {}),
    ("(360)", "page", "int x = (360);", {4: 1}),
    ("printf knob", "page", 'printf("knob=%d", k);', {}),
    ("T() Knob", "page", 'x = T("x", "Knob");', {5: 1}),
    ("T() plain", "page", 'x = T("設定", "Settings");', {}),
    ("include ui_geom.h page", "page", '#include "ui_geom.h"\n', {4: 1}),
    ("include ui_geom.h piece", "piece", '#include "ui_geom.h"\n', {}),
    ("clickable flag", "page", "lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);", {2: 1}),
    ("indev call, not the type", "page", "lv_indev_t *d = lv_indev_active();", {2: 1}),
    ("LV_ALIGN and align", "page", "lv_obj_align(o, LV_ALIGN_CENTER, 0, 0);", {3: 2}),
    ("positional helper", "page", "ui_text_button(p, T(\"a\", \"b\"), 1, 2);", {3: 1}),
    ("char literal quote", "page", "char c = '\"'; lv_button_create(p);", {1: 1}),
    ("indev type name is not a call", "page", "lv_indev_t *touch = NULL;", {}),
    ("two knob words on one line", "page", 'x = a ? T("旋鈕", "Knob") : T("旋鈕2", "Knob 2");', {5: 1}),
    ("two on one line", "page", "lv_label_create(p); lv_label_create(q);", {1: 2}),
    ("LV_EVENT_ALL", "page", "lv_obj_add_event_cb(b, cb, LV_EVENT_ALL, 0);", {2: 1}),
    ("set_align", "page", "lv_obj_set_align(o, 1);", {3: 1}),
    ("style x, y, align, min, max", "page", "lv_obj_set_style_x(o, 1, 0); lv_obj_set_style_y(o, 1, 0); lv_obj_set_style_align(o, 1, 0);\n"
                                            "lv_obj_set_style_min_width(o, 1, 0); lv_obj_set_style_max_height(o, 1, 0);", {3: 5}),
    ("flex and grid", "page", "lv_obj_set_flex_flow(o, 1); lv_obj_set_flex_align(o, 1, 1, 1); lv_obj_set_grid_dsc_array(o, 0, 0);", {3: 3}),
    ("text_align is not geometry", "page", "lv_obj_set_style_text_align(o, 1, 0);", {}),
    # final review I-2: each new pattern is a finding in a page
    ("style size", "page", "lv_obj_set_style_size(o, 90, 40, 0);", {3: 1}),
    ("style margin", "page", "lv_obj_set_style_margin_left(o, 4, 0);", {3: 1}),
    ("style transform_width", "page", "lv_obj_set_style_transform_width(o, 4, 0);", {3: 1}),
    ("style transform_height", "page", "lv_obj_set_style_transform_height(o, 4, 0);", {3: 1}),
    ("content_width", "page", "lv_obj_set_content_width(o, 90);", {3: 1}),
    ("content_height", "page", "lv_obj_set_content_height(o, 90);", {3: 1}),
    ("set_layout", "page", "lv_obj_set_layout(o, LV_LAYOUT_FLEX);", {3: 1}),
    ("local style geometry", "page", "lv_style_set_width(&st, 90); lv_style_set_x(&st, 20); lv_style_set_pad_all(&st, 2); lv_style_set_flex_flow(&st, 1);", {3: 4}),
    ("local style colour is not geometry", "page", "lv_style_set_bg_color(&st, c); lv_style_set_text_align(&st, 1);", {}),
    ("SINGLE_CLICKED", "page", "lv_obj_add_event_cb(b, cb, LV_EVENT_SINGLE_CLICKED, 0);", {2: 1}),
    ("DOUBLE_CLICKED", "page", "lv_obj_add_event_cb(b, cb, LV_EVENT_DOUBLE_CLICKED, 0);", {2: 1}),
    ("TRIPLE_CLICKED", "page", "lv_obj_add_event_cb(b, cb, LV_EVENT_TRIPLE_CLICKED, 0);", {2: 1}),
    ("VALUE_CHANGED", "page", "lv_obj_add_event_cb(b, cb, LV_EVENT_VALUE_CHANGED, 0);", {2: 1}),
    ("event code in a variable", "page", "lv_event_code_t c = 10; lv_obj_add_event_cb(b, cb, c, 0);", {2: 1}),
    ("constructor as a function pointer", "page", "lv_obj_t *(*mk)(lv_obj_t *) = lv_button_create; lv_obj_t *b = mk(p);", {1: 1}),
    ("class constructor", "page", "lv_obj_t *b = lv_obj_class_create_obj(&lv_button_class, p);", {1: 1}),
    # fx apps T6: R6 explanation text on a page (a T() literal of 12+ CJK characters or ending in 。 inside a text sink)
    ("r6_long_note", "page", 'ui_list_note(l, T("這是一段很長的說明文字超過十二個字", "x"));', {6: 1}),
    ("r6_period", "page", 'lv_label_set_text(l, T("好。", "ok."));', {6: 1}),
    ("r6_short", "page", 'lv_label_set_text(l, T("開", "on"));', {}),
    ("r6_piece_ignored", "piece", 'ui_list_note(l, T("這是一段很長的說明文字超過十二個字", "x"));', {}),
    ("r6_comment", "page", '/* ui_list_note(l, T("這是一段很長的說明文字超過十二個字。", "x")); */', {}),
    ("r6_fmt_and_static", "page", 'lv_label_set_text_fmt(l, T("這是一段很長的說明文字超過十二個字%d", "x"), 1); lv_label_set_text_static(l, T("好。", "ok"));', {6: 2}),
    ("r6_snprintf_catches", "page", 'snprintf(b, sizeof b, T("設定頁：USB 線插設定面，電腦用 Chrome 開上面網址。", "x"));', {6: 1}),
    ("r6_snprintf_short_passes", "page", 'snprintf(b, sizeof b, "%s: %d", T("睡眠", "Sleep"), 3);', {}),
    ("r6_joined_catches", "page", 'lv_label_set_text(l, T("這是一段很長" "的說明文字超過" "十二個字", "x"));', {6: 1}),
    ("r6_joined_short_passes", "page", 'lv_label_set_text(l, T("睡眠" "時間", "Sleep"));', {}),
    ("r6_not_a_sink", "page", 'x = T("這是一段很長的說明文字超過十二個字。", "x");', {}),
    ("a piece may do all of it", "piece", "lv_obj_set_style_size(o, 9, 4, 0); lv_obj_set_layout(o, 1); lv_style_set_width(&st, 9);\n"
                                          "lv_obj_add_event_cb(b, cb, LV_EVENT_VALUE_CHANGED, 0); lv_obj_add_event_cb(b, cb, c, 0);\n"
                                          "mk = lv_button_create; lv_obj_class_create_obj(&lv_button_class, p);", {}),
]

# (name, path, counts, allowance counts or None = not on the table, expected substring or None = no message)
ALLOW_CASES = [
    ("count above allowance", "ui/scr_x.c", {1: 3}, {1: 2}, "new hand-built UI"),
    ("count below allowance", "ui/scr_x.c", {1: 1}, {1: 2}, "lower the allowance to 1"),
    ("count equals allowance", "ui/scr_x.c", {1: 2}, {1: 2}, None),
    ("page not on the table", "ui/scr_y.c", {1: 1}, None, "new hand-built UI"),
    ("page not on the table, clean", "ui/scr_y.c", {}, None, None),
]


def selftest():
    bad = 0
    n = 0
    for name, kind, src, want in CASES:
        n += 1
        got = {}
        for _line, rule in lint_text(src, kind):
            got[rule] = got.get(rule, 0) + 1
        if got != want:
            print("ui_lint selftest FAIL: %s: expected %s, got %s" % (name, want, got))
            bad += 1
    n += 1
    lines = lint_text("/* a\n b */\nint x;\nint y = 360;\n", "page")
    if lines != [(4, 4)]:
        print("ui_lint selftest FAIL: line numbers: %s" % (lines,))
        bad += 1
    n += 1
    with tempfile.TemporaryDirectory() as d:        # the table's own rules: a piece needs a why and is never a ui/scr_*.c page
        for name, text, ok in (("piece with a why", "ui/ui_x.c piece shared piece\n", True),
                               ("piece without a why", "ui/ui_x.c piece\n", False),
                               ("piece for a scr_ file", "ui/scr_x.c piece because I said so\n", False)):
            f = os.path.join(d, "allow")
            with open(f, "w", encoding="utf-8") as fh:
                fh.write(text)
            try:
                read_allow(f)
                got = True
            except ValueError:
                got = False
            if got != ok:
                print("ui_lint selftest FAIL: %s: accepted=%s" % (name, got))
                bad += 1
            n += 1
    with tempfile.TemporaryDirectory() as d:        # headers under ui/ are scanned (final review I-2): a page header is a page, a piece header is not
        os.makedirs(os.path.join(d, "ui"))
        for nm, text in (("scr_x.h", "static inline void b(lv_obj_t *p) { lv_button_create(p); }\n"),
                         ("ui_x.h", "static inline void b(lv_obj_t *p) { lv_button_create(p); }\n"),
                         ("ui_lint.allow", "ui/ui_x.h piece shared piece header\n")):
            with open(os.path.join(d, "ui", nm), "w", encoding="utf-8") as fh:
                fh.write(text)
        got = {rel: (kind, len(fnd)) for rel, kind, _src, fnd in scan(d, read_allow(os.path.join(d, "ui", "ui_lint.allow")))}
        if got != {"ui/scr_x.h": ("page", 1), "ui/ui_x.h": ("piece", 0)}:
            print("ui_lint selftest FAIL: headers: %s" % (got,))
            bad += 1
        n += 1
    for name, path, counts, allow, want in ALLOW_CASES:
        n += 1
        table = {} if allow is None else {path: ("page", allow, "")}
        msgs = check_counts(path, counts, table)
        ok = (not msgs) if want is None else (len(msgs) == 1 and want in msgs[0])
        if not ok:
            print("ui_lint selftest FAIL: %s: %s" % (name, msgs))
            bad += 1
    if bad:
        print("ui_lint selftest: %d of %d cases failed" % (bad, n))
        return 1
    print("ui_lint selftest: ok (%d cases)" % n)
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="pages may not hand-build UI (R1-R6)")
    ap.add_argument("--root", default=os.path.join(here, "..", "..", "app", "main"))
    ap.add_argument("--allow", default=None)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--seed", action="store_true", help="print today's counts as allowance lines (first use only)")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    return run(a.root, a.allow or os.path.join(a.root, "ui", "ui_lint.allow"), a.seed)


if __name__ == "__main__":
    sys.exit(main())
