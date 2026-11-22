#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The commented theme template (card: _player/themes/template.txt), written from app/main/ui/ui_tokens.inc (every token, its purpose and where it shows)
and the built-in default theme (neon, components/player_core/src/pc_theme_table.inc), so the file always lists exactly the tokens the firmware reads.
  gen_theme_template.py [out]      write (default: stdout)
  gen_theme_template.py --check    the C table and the nine core keys of ui_theme.c agree; prints "<n> tokens ok" (make lint-app)
The result, loaded as a theme, is the default look (help / help_text are left commented out: unset means they follow panel2 / fg)."""
import os, re, sys
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
DEF = os.path.join(ROOT, "app/main/ui/ui_tokens.inc")
CORE_KEYS = ["bg", "panel", "panel2", "fg", "dim", "a1", "a2", "a3", "on"]
STR = r'"((?:[^"\\]|\\.)*)"'

def parse():
    """([(key, zh, en)] core, [(key, n, [rgb], zh, en)] tokens)"""
    src = open(DEF, encoding="utf-8").read()
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    core = [(m[0], m[1], m[2]) for m in re.findall(r"^CORE\((\w+),\s*" + STR + r",\s*" + STR + r"\)", src, re.M)]
    toks = []
    def vals(s): return [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", s)]
    for m in re.finditer(r'^TOK\((\w+),\s*"(\w+)",\s*(\d+),\s*\(([^)]*)\),\s*' + STR + r",\s*" + STR + r"\)", src, re.M | re.S):
        toks.append((m[2], int(m[3]), vals(m[4]), m[5], m[6]))
    for m in re.finditer(r"^AMB\(" + STR + r",\s*(\w+),\s*\(([^)]*)\),\s*\(([^)]*)\),\s*\(([^)]*)\)\)", src, re.M):
        for k, v in enumerate(m.groups()[2:5], 1):
            toks.append(("amb_%s_%d" % (m[2], k), 7, vals(v), "環境光「%s」風格 %d%s" % (m[1], k, "：0 底 1-6 六個色調（1 也是外框）" if k == 1 else "：順序同風格 1"),
                         "Ambient light, scene %s, style %d%s" % (m[2], k, ": 0 background, 1-6 the six tones (1 is also the rim)" if k == 1 else ": same order")))
    return core, toks

def neon():
    row = re.search(r'\{\s*"neon",[^,]*,\s*([^}]*)\}', open(os.path.join(ROOT, "components/player_core/src/pc_theme_table.inc"), encoding="utf-8").read()).group(1)
    return dict(zip(CORE_KEYS, [int(v, 16) for v in re.findall(r"0x[0-9a-fA-F]+", row)]))

HEADER = """# 主題檔（繁中）：每行「名稱 = #RRGGBB」；# 或 ; 開頭是註解。複製這個檔案、改名成 <你的主題名>.txt，
#   再在設定檔 app.txt 寫 theme=<你的主題名>（或用網頁設定），重開機就會載入。沒寫的項目沿用預設；寫錯的行會被略過（序列埠只印一行紀錄）。
#   base = neon 可指定要沿用哪一個內建主題的顏色（預設 neon）。調色盤項目（有 .0 .1 ...）一個一個寫，例如 kuji.2 = #7BD88F。
# 主题文件（简中）：每行"名称 = #RRGGBB"；以 # 或 ; 开头的是注释。复制本文件、改名为 <你的主题名>.txt，
#   再在 app.txt 里写 theme=<你的主题名>（或用网页设置），重启后加载。没写的项目沿用默认；写错的行会被跳过（串口只打印一行日志）。
#   base = neon 可指定沿用哪个内置主题的颜色（默认 neon）。调色板项目（带 .0 .1 ...）逐项填写，例如 kuji.2 = #7BD88F。
# Theme file (English): one `name = #RRGGBB` per line; a line starting with # or ; is a comment. Copy this file, rename it <your-theme>.txt,
#   put theme=<your-theme> in app.txt (or set it on the web page) and reboot. Anything you leave out keeps the default; a bad line is skipped
#   (one log line on the serial port). `base = neon` picks which built-in theme the nine core colours start from (default neon).
#   Palette entries (the ones with .0 .1 ...) are set one by one, e.g. `kuji.2 = #7BD88F`.
# Every token below is listed with its default value: this file as it stands is the default look.
"""

def build():
    core, toks = parse()
    n = neon()
    out = [HEADER, "base = neon\n", "\n# ---- core colours (the nine colours of a built-in theme) ----\n"]
    for k, zh, en in core:
        out.append("# %s\n# %s\n%s = #%06X\n" % (zh, en, k, n[k]))
    out.append("\n# ---- extra tokens ----\n")
    for key, cnt, vs, zh, en in toks:
        out.append("# %s\n# %s\n" % (zh, en))
        for i, v in enumerate(vs):
            name = key if cnt == 1 else "%s.%d" % (key, i)
            out.append(("# %s = #<unset: follows %s>\n" % (name, {"help": "panel2", "help_text": "fg"}.get(key, "the theme"))) if v == 0xFFFFFFFF else "%s = #%06X\n" % (name, v))
    return "".join(out)

def check():
    core, toks = parse()
    assert [c[0] for c in core] == CORE_KEYS, "CORE keys differ from the nine core colours"
    c = open(os.path.join(ROOT, "app/main/ui/ui_theme.c"), encoding="utf-8").read()
    m = re.search(r"static const char \*const K\[\] = \{([^}]*)\}", c)
    assert re.findall(r'"(\w+)"', m.group(1)) == CORE_KEYS, "ui_theme.c core_slot keys differ from the nine core colours"
    for key, cnt, vs, zh, en in toks:
        assert len(vs) == cnt, "%s: %d defaults for n=%d" % (key, len(vs), cnt)
    keys = [t[0] for t in toks]
    assert len(set(keys)) == len(keys), "duplicate token key"
    t = build()
    assert t.count(" = #") >= sum(x[1] for x in toks) - 2
    print("%d tokens ok" % len(toks))

if __name__ == "__main__":
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    if "--check" in sys.argv: check()
    elif len(sys.argv) > 1: open(sys.argv[1], "w", encoding="utf-8").write(build())
    else: sys.stdout.write(build())
