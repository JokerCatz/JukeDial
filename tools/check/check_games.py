#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""check_games: the game registry (app/main/ui/games.def, games hub G0) is well formed and every listed game is complete. Standard library only.

  check_games.py [--root .] [--selftest]

games.def has one `GAME(id, "zh", "en", scr)` line per game, in alphabetical order of id, ids unique, made of [a-z0-9_] and at most 16 characters (the bests are saved under "<id>.m0": pc_gamesave). For every game:
  - app/main/ui/scr_game_<id>.c exists and defines `const ui_screen_t <scr>` whose .name is "tools_game_<id>";
  - the same file defines `const ui_game_help_t game_help_<id>` with a non-empty .zh and .en (the rules the hub's 說明 shows);
  - each of the two rules texts (UTF-8 bytes) fits the help page's buffer (HELP_BUF in app/main/ui/ui_help.c, 4096): the game's own 說明 is never cut;
  - sim/test_games/<id>.sh exists (the game's sim test: played games, pictures are made by tools/simtools/ui_baseline.py);
and tools/simtools/ui_baseline.py and tools/simtools/leak_cycles.py read the same list (their games_ids() equals it), so a listed game has its picture (game_<id>) and its leak
cycle without a line of their own. Prints `check_games: ok (<n> games)`; on a problem `<file>:<line>: <reason>` per problem and exit 1."""
import argparse
import ast
import os
import re
import sys
import tempfile

LINE_RE = re.compile(r'^GAME\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(\w+)\s*\)\s*$')
ID_RE = re.compile(r"^[a-z0-9_]+$")


def strip_comments(src):
    return re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), src, flags=re.S)


def read_games(path):
    """-> (games [(line, id, zh, en, scr)], problems [(line, text)])"""
    games, problems = [], []
    text = strip_comments(open(path, encoding="utf-8").read())
    for n, l in enumerate(text.splitlines(), 1):
        l = l.strip()
        if not l.startswith("GAME("):
            if l and not l.startswith("//"): problems.append((n, f"not a GAME(...) line: {l[:50]}"))
            continue
        m = LINE_RE.match(l)
        if not m: problems.append((n, f"malformed GAME line: {l[:60]}")); continue
        games.append((n, m.group(1), m.group(2), m.group(3), m.group(4)))
    return games, problems


def help_buf(root):
    """HELP_BUF of ui_help.c (the buffer one help body is built in); 4096 when the define is not found (a synthetic tree)"""
    try: m = re.search(r"#define\s+HELP_BUF\s+(\d+)", open(os.path.join(root, "app", "main", "ui", "ui_help.c"), encoding="utf-8").read())
    except OSError: m = None
    return int(m.group(1)) if m else 4096


def check(root):
    base = os.path.join(root, "app", "main", "ui")
    defp = os.path.join(base, "games.def")
    rel = "app/main/ui/games.def"
    games, problems = read_games(defp)
    out = [(rel, n, t) for n, t in problems]
    ids = [g[1] for g in games]
    for n, gid, zh, en, scr in games:
        if not ID_RE.match(gid): out.append((rel, n, f"id {gid!r} is not [a-z0-9_]+"))
        if len(gid) > 16: out.append((rel, n, f"id {gid!r} is longer than 16 characters (pc_gamesave keys \"<id>.m0\" must fit 19: PC_GS_ID_MAX)"))
    for (n, a, *_), (m, b, *_) in zip(games, games[1:]):
        if a >= b: out.append((rel, m, f"{b} is out of order after {a} (alphabetical by id, unique)"))
    for n, gid, zh, en, scr in games:
        f = os.path.join(base, f"scr_game_{gid}.c")
        frel = f"app/main/ui/scr_game_{gid}.c"
        if not os.path.exists(f): out.append((rel, n, f"{frel} does not exist")); continue
        src = strip_comments(open(f, encoding="utf-8").read())
        m = re.search(r"\bconst\s+ui_screen_t\s+" + re.escape(scr) + r"\b[^;]*?\.name\s*=\s*\"([^\"]*)\"", src, flags=re.S)
        if not m: out.append((frel, 1, f"does not define `const ui_screen_t {scr}` with a .name"))
        elif m.group(1) != f"tools_game_{gid}": out.append((frel, 1, f"the page's name is {m.group(1)!r}, wanted \"tools_game_{gid}\""))
        h = re.search(r"\bconst\s+ui_game_help_t\s+game_help_" + re.escape(gid) + r"\s*=\s*\{(.*?)\};", src, flags=re.S)
        if not h: out.append((frel, 1, f"does not define `const ui_game_help_t game_help_{gid}`"))
        else:
            for lang in ("zh", "en"):
                m2 = re.search(r"\." + lang + r"\s*=\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+)", h.group(1))
                text = "".join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', m2.group(1))) if m2 else ""
                if len(text) < 8: out.append((frel, 1, f"game_help_{gid}.{lang} is missing or too short"))
                elif len(text.encode("utf-8")) >= help_buf(root): out.append((frel, 1, f"game_help_{gid}.{lang} is {len(text.encode('utf-8'))} bytes: the help page buffer is {help_buf(root)}"))
        if not os.path.exists(os.path.join(root, "sim", "test_games", f"{gid}.sh")): out.append((rel, n, f"sim/test_games/{gid}.sh does not exist"))
    tools = os.path.join(root, "tools", "simtools")
    sys.path.insert(0, tools)
    try:
        for mod, what in (("ui_baseline", "its pictures game_<id>"), ("leak_cycles", "its leak cycle")):
            for k in [k for k in sys.modules if k == mod]: del sys.modules[k]
            m = __import__(mod)
            if hasattr(m, "__file__") and os.path.dirname(os.path.abspath(m.__file__)) != os.path.abspath(tools):
                out.append((f"tools/simtools/{mod}.py", 1, "imported from another directory")); continue
            if m.games_ids() != ids: out.append((f"tools/simtools/{mod}.py", 1, f"games_ids() = {m.games_ids()} but games.def lists {ids}: {what} must come from the list"))
    finally:
        sys.path.remove(tools)
    return games, out


def run(root):
    games, problems = check(root)
    for f, n, t in problems: print(f"{f}:{n}: {t}")
    if problems: return 1
    print(f"check_games: ok ({len(games)} games)")
    return 0


def selftest():
    """a synthetic tree: one good game passes; each defect is reported"""
    def tree(defn, game_src, test=True):
        d = tempfile.mkdtemp()
        os.makedirs(os.path.join(d, "app", "main", "ui")); os.makedirs(os.path.join(d, "sim", "test_games")); os.makedirs(os.path.join(d, "tools", "simtools"))
        open(os.path.join(d, "app", "main", "ui", "games.def"), "w").write(defn)
        for gid, src in game_src.items(): open(os.path.join(d, "app", "main", "ui", f"scr_game_{gid}.c"), "w").write(src)
        if test:
            for gid in game_src: open(os.path.join(d, "sim", "test_games", f"{gid}.sh"), "w").write("")
        return d
    good = 'const ui_game_help_t game_help_aa = { .zh = "規則規則規則規則", .en = "Some rules here" };\nconst ui_screen_t scr_game_aa = { .name = "tools_game_aa", .create = c };\n'
    def fake(d, ids):      # replace the two tool modules with stubs that return the ids (the selftest is about the checks, not about those modules)
        for f in ("ui_baseline.py", "leak_cycles.py"):
            open(os.path.join(d, "tools", "simtools", f), "w").write(f"def games_ids(): return {ids!r}\n")
    cases = [
        ("good", 'GAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good}, True, ["aa"], 0),
        ("unsorted", 'GAME(bb, "乙", "B", scr_game_bb)\nGAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good, "bb": good.replace("aa", "bb")}, True, ["bb", "aa"], "out of order"),
        ("duplicate", 'GAME(aa, "甲", "A", scr_game_aa)\nGAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good}, True, ["aa", "aa"], "out of order"),
        ("missing file", 'GAME(aa, "甲", "A", scr_game_aa)\n', {}, True, ["aa"], "does not exist"),
        ("no help", 'GAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good.split("\n")[1] + "\n"}, True, ["aa"], "game_help_aa"),
        ("short help", 'GAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good.replace("Some rules here", "x")}, True, ["aa"], "too short"),
        ("help longer than the buffer", 'GAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good.replace("Some rules here", "x" * 5000)}, True, ["aa"], "help page buffer"),
        ("wrong name", 'GAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good.replace("tools_game_aa", "game_aa")}, True, ["aa"], "wanted"),
        ("no test", 'GAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good}, False, ["aa"], "test_games"),
        ("picture list drifted", 'GAME(aa, "甲", "A", scr_game_aa)\n', {"aa": good}, True, [], "must come from the list"),
        ("malformed", 'GAME(aa, "甲", scr_game_aa)\n', {"aa": good}, True, ["aa"], "malformed"),
    ]
    bad = 0
    for name, defn, srcs, test, tool_ids, want in cases:
        d = tree(defn, srcs, test)
        fake(d, tool_ids)
        games, problems = check(d)
        txt = " | ".join(t for _, _, t in problems)
        ok = (not problems) if want == 0 else (want in txt)
        if not ok: print(f"selftest FAIL {name}: wanted {want!r}, got {txt!r}"); bad += 1
    if bad: return 1
    print(f"check_games selftest: ok ({len(cases)} cases)")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    ap.add_argument("--selftest", action="store_true")
    o = ap.parse_args()
    sys.exit(selftest() if o.selftest else run(os.path.abspath(o.root)))
