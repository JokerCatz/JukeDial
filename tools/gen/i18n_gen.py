#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""i18n string table (phase A of the 3-language work; notes in the internal notes).

Texts are symbolic ids (Android style): T(S_BACK) in the sources, three texts per id in tools/gen/i18n/strings.tsv
(id <TAB> zh_tw <TAB> zh_cn <TAB> en <TAB> status <TAB> ctx; \\n \\t \\\\ escaped), the one file people edit. This script turns it into
app/main/ui/i18n_strings.h (enum S_xxx, S__COUNT) and i18n_strings.c (I18N_TW / I18N_CN / I18N_EN, const char *const arrays in flash, indexed by id).
T_RAW(zh_tw, en) is the escape hatch for a text that is not a plain literal pair (a table of names, a struct field, a built-up string): it has no
简中 text yet (zh-TW shows); lint counts them (phase B shrinks the number).

  i18n_gen.py            tsv -> i18n_strings.h / .c
  i18n_gen.py --check    lint (make lint-app, sim-test): the generated files are the tsv's; ids unique C names; every id has all three columns
                         and zh_cn takes the same printf conversions as zh_tw; every id used by the sources exists; unused ids are reported; T_RAW count is printed
                         (--raw lists them); a two-argument T( call is an error (it must be T_RAW)
  i18n_gen.py --stats    counts only
(The glyph check, tools/check/check_font_glyphs.py, reads i18n_strings.c as any other source: all three columns.) Standard library only."""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
SRC = os.path.join(REPO, 'app', 'main')
TSV = os.path.join(HERE, 'i18n', 'strings.tsv')
OUT_H = os.path.join(SRC, 'ui', 'i18n_strings.h')
OUT_C = os.path.join(SRC, 'ui', 'i18n_strings.c')
DATA_TSV = os.path.join(HERE, 'i18n', 'data.tsv')
CORE = os.path.join(REPO, 'components', 'player_core')
OUT_D = os.path.join(CORE, 'src', 'pc_i18n_data.c')
GENERATED = {os.path.normpath(OUT_H), os.path.normpath(OUT_C), os.path.normpath(OUT_D)}
EXT = ('.c', '.h', '.cpp', '.def')

def is_cjk(s):
    return any(0x3000 <= ord(c) <= 0x9FFF or 0xFF00 <= ord(c) <= 0xFFEF for c in s)

SIMPLE = {'n': b'\n', 't': b'\t', '\\': b'\\', '"': b'"', "'": b"'", 'r': b'\r', 'a': b'\a', 'e': b'\x1b', 'b': b'\b', 'f': b'\f', 'v': b'\v', '?': b'?'}

def tokens(text):
    """yield (kind, value, start, end): kind 's' a string literal (escapes decoded like C: \\x takes ALL following hex digits), 'i' identifier, 'p' punctuation; comments and char literals dropped"""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == '"':
            j, b = i + 1, bytearray()
            while j < n and text[j] != '"':
                ch = text[j]
                if ch == '\\' and j + 1 < n:
                    e = text[j + 1]
                    m = re.match(r'x([0-9a-fA-F]+)', text[j + 1:])
                    o = re.match(r'[0-7]{1,3}', text[j + 1:])
                    if m: b.append(int(m.group(1), 16) & 255); j += 1 + len(m.group(0))
                    elif o: b.append(int(o.group(0), 8) & 255); j += 1 + len(o.group(0))
                    elif e in SIMPLE: b += SIMPLE[e]; j += 2
                    else: b += e.encode(); j += 2
                else:
                    b += ch.encode('utf-8'); j += 1
            yield ('s', b.decode('utf-8', 'replace'), i, j + 1)
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == '\\' else 1
            i = j + 1
        elif c.isalpha() or c == '_':
            j = i
            while j < n and (text[j].isalnum() or text[j] == '_'): j += 1
            yield ('i', text[i:j], i, j); i = j
        elif c.isspace():
            i += 1
        else:
            yield ('p', c, i, i + 1); i += 1

def groups(toks):
    """merge adjacent string literals (C concatenation)"""
    out = []
    for t in toks:
        if t[0] == 's' and out and out[-1][0] == 's': out[-1] = ('s', out[-1][1] + t[1], out[-1][2], t[3])
        else: out.append(t)
    return out

SKIP_DEF = {'font.h', 'i18n.h', 'i18n.c'}   # the T / T_RAW definitions and the language state

def source_files():
    """app/main (every .c .h .cpp .def), components/player_core src + include and the simulator's fakes (sim/fake_*.c: they draw texts through T too)"""
    for dp, dn, fns in os.walk(SRC):
        dn[:] = [d for d in dn if d not in ('managed_components', 'build')]
        for fn in sorted(fns):
            p = os.path.join(dp, fn)
            if fn.endswith(EXT) and os.path.normpath(p) not in GENERATED: yield p
    for sub in ('src', 'include/pc'):                      # the player core's texts (noise names, fx apps, ...) too: TZ() / data.tsv
        d = os.path.join(CORE, sub)
        for fn in sorted(os.listdir(d)):
            p = os.path.join(d, fn)
            if fn.endswith(('.c', '.h')) and os.path.normpath(p) not in GENERATED: yield p
    simd = os.path.join(REPO, 'sim')
    for fn in sorted(os.listdir(simd)):
        if fn.startswith('fake_') and fn.endswith('.c'): yield os.path.join(simd, fn)

def esc(s): return s.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n')
def unesc(s): return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), s)

def read_tsv():
    rows = []
    if os.path.isfile(TSV):
        for ln in open(TSV, encoding='utf-8'):
            if ln.startswith('#') or not ln.strip('\n'): continue
            f = ln.rstrip('\n').split('\t')
            f += [''] * (6 - len(f))
            rows.append((f[0], unesc(f[1]), unesc(f[2]), unesc(f[3]), f[4], f[5]))
    return rows

def write_tsv(rows):
    head = ['# id <TAB> zh_tw <TAB> zh_cn <TAB> en <TAB> status <TAB> ctx (optional: why two ids carry the same texts).  Edit zh_cn and status (untranslated | translated | reviewed | same); a new id: add a row, run python3 tools/gen/i18n_gen.py.',
            '# The ids are the names in the sources: T(S_BACK). \\n \\t \\\\ are escaped.']
    open(TSV, 'w', encoding='utf-8').write('\n'.join(head + ['\t'.join((r[0], esc(r[1]), esc(r[2]), esc(r[3]), r[4], r[5])) for r in rows]) + '\n')

def read_data():
    rows = []
    if os.path.isfile(DATA_TSV):
        for ln in open(DATA_TSV, encoding='utf-8'):
            if ln.startswith('#') or not ln.strip('\n'): continue
            f = ln.rstrip('\n').split('\t')
            f += [''] * (3 - len(f))
            rows.append((unesc(f[0]), unesc(f[1]), f[2]))
    return rows

def write_data(rows):
    head = ['# zh_tw literal <TAB> zh_cn <TAB> status (untranslated | translated | reviewed | same).  The data texts: every CJK string literal of the sources that is no T(S_xxx) text (tables, struct fields, formats, help bodies).',
            '# The literal in the source is the key: TZ("...") / T_RAW("...", en) / the help and game tables show the zh_cn text in 簡中 (pc_i18n_cn).  \\n \\t \\\\ are escaped.  New literals: python3 tools/gen/i18n_gen.py --sync-data']
    rows = sorted(rows, key=lambda r: r[0].encode('utf-8'))
    open(DATA_TSV, 'w', encoding='utf-8').write('\n'.join(head + ['\t'.join((esc(r[0]), esc(r[1]), r[2])) for r in rows]) + '\n')

def cstr(s):
    o = []
    for ch in s:
        if ch == '\\': o.append('\\\\')
        elif ch == '"': o.append('\\"')
        elif ch == '\n': o.append('\\n')
        elif ch == '\t': o.append('\\t')
        elif ord(ch) < 32 or ord(ch) == 127: o.append('\\%03o' % ord(ch))
        else: o.append(ch)
    return '"' + ''.join(o) + '"'

def render(rows):
    h = ['// SPDX-License-Identifier: LGPL-2.1-or-later', '/* GENERATED by tools/gen/i18n_gen.py from tools/gen/i18n/strings.tsv: do not edit. The string ids of T(S_xxx). */', '#pragma once', '',
         'typedef enum {']
    h += ['    %s,' % r[0] for r in rows] + ['    S__COUNT', '} i18n_id_t;', '',
          'extern const char *const I18N_TW[S__COUNT], *const I18N_CN[S__COUNT], *const I18N_EN[S__COUNT];', '']
    c = ['// SPDX-License-Identifier: LGPL-2.1-or-later', '/* GENERATED by tools/gen/i18n_gen.py from tools/gen/i18n/strings.tsv: do not edit. One text per language and id (flash; no RAM). */', '#include "i18n_strings.h"', '']
    for name, col in (('I18N_TW', 1), ('I18N_CN', 2), ('I18N_EN', 3)):
        c.append('const char *const %s[S__COUNT] = {' % name)
        c += ['    [%s] = %s,' % (r[0], cstr(r[col])) for r in rows]
        c += ['};', '']
    return '\n'.join(h), '\n'.join(c)

def render_data(drows):
    drows = sorted(drows, key=lambda r: r[0].encode('utf-8'))   # strcmp order (UTF-8 keeps code point order)
    d = ['// SPDX-License-Identifier: LGPL-2.1-or-later', '/* GENERATED by tools/gen/i18n_gen.py from tools/gen/i18n/data.tsv: do not edit. The 簡中 text of every data literal, sorted by the zh-TW literal (pc_i18n_cn: binary search; flash, no RAM). */',
         'typedef struct { const char *tw, *cn; } pc_i18n_pair_t;', '', 'const pc_i18n_pair_t PC_I18N_DATA[] = {']
    d += ['    { %s, %s },' % (cstr(r[0]), cstr(r[1])) for r in drows]
    d += ['};', 'const unsigned PC_I18N_DATA_N = %d;' % len(drows), '']
    return '\n'.join(d)

SPEC = re.compile(r'%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z|j|t)?[a-zA-Z%]')

TW_ONLY = ('软体', '网路', '档案', '程式', '资讯', '荧幕', '萤幕', '记忆体', '使用者', '预设', '清单', '影片', '位元', '连线', '取样', '设定', '滑鼠', '视讯', '储存', '搜寻', '质量管理', '建置', '一列', '状态列', '目前')   # Taiwan wording that a character conversion leaves behind

def cn_defects(v):
    """what is wrong with a zh_cn text: a Han character outside GB2312 (a Traditional form left over: the font pack has GB2312 + Big5 level 1), or a Taiwan-only word"""
    out = []
    for ch in v:
        if 0x4E00 <= ord(ch) <= 0x9FFF:
            try: ch.encode('gb2312')
            except UnicodeEncodeError: out.append(ch)
    out += [w for w in TW_ONLY if w in v]
    return out

def lint(rows):
    bad, seen = 0, set()
    for r in rows:
        if not re.fullmatch(r'S_[A-Z0-9_]+', r[0]) or r[0] == 'S__COUNT': print(f'i18n_gen: bad id {r[0]!r}'); bad = 1
        if r[0] in seen: print(f'i18n_gen: duplicate id {r[0]}'); bad = 1
        seen.add(r[0])
        for name, v in zip(('zh_tw', 'zh_cn', 'en'), r[1:4]):
            if not v.strip(): print(f'i18n_gen: {r[0]} has no {name} text'); bad = 1
        d = cn_defects(r[2])
        if d: print(f'i18n_gen: {r[0]}: zh_cn has Traditional characters or Taiwan wording: {" ".join(d)}'); bad = 1
        specs = [sorted(SPEC.findall(v)) for v in r[1:4]]
        if specs[0] != specs[1]: print(f'i18n_gen: {r[0]}: zh_cn has other printf conversions than zh_tw: {specs[1]} vs {specs[0]}'); bad = 1   # a translation must take the arguments the call site passes
        if specs[0] != specs[2]: print(f'i18n_gen: note {r[0]}: en and zh_tw take other printf conversions ({specs[2]} vs {specs[0]}; extra arguments are harmless, a missing one is not)')
    by = {}
    for r in rows: by.setdefault(r[1:4], []).append(r)
    for k, rs in by.items():
        if len(rs) > 1 and any(not r[5] for r in rs): print(f'i18n_gen: note {", ".join(r[0] for r in rs)} carry identical texts: merge them, or give each a ctx note (the last column) saying why the meaning differs')
    return bad

def cjk_literals():
    """{literal: first file} of every CJK string literal in the scanned sources (the T(S_xxx) texts are no literals of the sources)"""
    out = {}
    for p in source_files():
        if os.path.basename(p) in SKIP_DEF: continue
        for t in groups(tokens(open(p, encoding='utf-8', errors='replace').read())):
            if t[0] == 's' and is_cjk(t[1]): out.setdefault(t[1], os.path.relpath(p, REPO))
    return out

# (scr_ambient / scr_breath: rule_zh / style_zh go through T_RAW in game_ring.c)  files whose `.zh = "..."` / `_zh` fields are shown through TZ() / T_RAW / the help piece at the consuming site, or are simulator data that stays 繁中
ZH_FIELD_OK = ('ui_help_texts.c', 'game_style.c', 'scr_ambient.c', 'scr_breath.c', 'fake_noise_engine.c', 'fake_bt_svc.c', 'fake_radio_svc.c', 'fake_sd.c')

def unwrapped_zh():
    """[(file, literal)]: a CJK literal right after `zh ?` or in a `.zh =` / `_zh =` field that no TZ( / T_RAW( wraps (it would show 繁中 in 简中)"""
    out = []
    for p in source_files():
        b = os.path.basename(p)
        if b in SKIP_DEF or b in ZH_FIELD_OK or b.startswith('scr_game_'): continue   # scr_game_*.c: .zh is the help body (ui_help.c wraps TZ)
        t = groups(tokens(open(p, encoding='utf-8', errors='replace').read()))
        for k, x in enumerate(t):
            if x[0] == 'i' and x[1] == 'T_RAW' and k + 3 < len(t) and t[k + 1][1] == '(' and t[k + 2][0] == 'i' and t[k + 3][0] == 's':
                out.append((os.path.relpath(p, REPO), f'T_RAW({t[k + 2][1]} "..."): a macro in front of the literal: the data key is the literal alone, put the macro outside T_RAW'))
            if x[0] != 's' or not is_cjk(x[1]) or k < 2: continue
            a, c = t[k - 1], t[k - 2]
            if a[1] == '?' and c[0] == 'i' and c[1].endswith('zh'): out.append((os.path.relpath(p, REPO), x[1]))
            elif a[1] == '=' and c[0] == 'i' and (c[1] == 'zh' or c[1].endswith('_zh')) and k >= 3 and t[k - 3][1] == '.': out.append((os.path.relpath(p, REPO), x[1]))
    return out

def lint_data(drows, lits):
    bad, seen = 0, set()
    for f, l in unwrapped_zh(): print(f'i18n_gen: {f}: CJK literal {l[:24]!r} after `zh ?` or in a .zh field without TZ() / T_RAW (it stays 繁中 in 簡中; wrap it, or list the file in ZH_FIELD_OK)'); bad = 1
    for r in drows:
        if r[0].startswith(' ') and not r[1].startswith(' ') or r[0].endswith(' ') and not r[1].endswith(' '): print(f'i18n_gen: data row {r[0][:30]!r}: the key has a leading/trailing space, zh_cn must keep it (ui_fxparam_name cuts band names at the space)'); bad = 1
    for r in drows:
        if r[0] in seen: print(f'i18n_gen: duplicate data row {r[0][:30]!r}'); bad = 1
        seen.add(r[0])
        d = cn_defects(r[1])
        if d: print(f'i18n_gen: data row {r[0][:30]!r}: zh_cn has Traditional characters or Taiwan wording: {" ".join(d)}'); bad = 1
        if not r[1]: print(f'i18n_gen: data row {r[0][:30]!r} has no zh_cn text'); bad = 1
        if sorted(SPEC.findall(r[0])) != sorted(SPEC.findall(r[1])): print(f'i18n_gen: data row {r[0][:30]!r}: zh_cn has other printf conversions than zh_tw'); bad = 1
    for l, p in sorted(lits.items()):
        if l not in seen: print(f'i18n_gen: CJK literal {l[:30]!r} ({p}) has no row in data.tsv (python3 tools/gen/i18n_gen.py --sync-data)'); bad = 1
    for k in sorted(seen - set(lits)): print(f'i18n_gen: unused data row {k[:30]!r}')
    return bad

def usage():
    used, raw, two = {}, [], []
    for p in source_files():
        if os.path.basename(p) in SKIP_DEF or os.path.normpath(p).startswith(CORE + os.sep): continue   # the player core has no ids (its macros S_ISDIR... are no texts); it uses TZ() / data.tsv
        g = groups(tokens(open(p, encoding='utf-8', errors='replace').read()))
        for k, t in enumerate(g):
            if t[0] == 'i' and re.fullmatch(r'S_[A-Z0-9][A-Z0-9_]*', t[1]): used.setdefault(t[1], os.path.relpath(p, REPO))
            if t[0] == 'i' and t[1] == 'T_RAW': raw.append(os.path.relpath(p, REPO))
            if t[0] == 'i' and t[1] == 'T' and k + 1 < len(g) and g[k + 1][1] == '(' and not (k + 3 < len(g) and g[k + 2][0] == 'i' and g[k + 2][1].startswith('S_') and g[k + 3][1] == ')'):
                two.append(os.path.relpath(p, REPO))
    return used, raw, two

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ''
    rows = read_tsv()
    used, raw, two = usage()
    unused = [r[0] for r in rows if r[0] not in used]
    drows = read_data()
    untr = sum(1 for r in rows if r[4] == 'untranslated')
    duntr = sum(1 for r in drows if r[2] == 'untranslated')
    print(f'i18n_gen: {len(rows)} ids, {untr} untranslated, {len(unused)} unused, {len(raw)} T_RAW call sites; {len(drows)} data rows, {duntr} untranslated')
    if mode == '--stats': return 0
    if mode == '--raw':
        for p in sorted(set(raw)): print(f'  {p}: {raw.count(p)}')
        return 0
    if mode == '--sync-data':
        lits = cjk_literals(); have = {r[0] for r in drows}
        add = [(l, l, 'untranslated') for l in lits if l not in have]
        write_data(drows + add); print(f'i18n_gen: {len(add)} data rows added'); return 0
    h, c = render(rows)
    dtxt = render_data(drows)
    if mode in ('--check', '--strict'):
        bad = lint_data(drows, cjk_literals())
        if mode == '--strict' and (untr or duntr): print('i18n_gen: --strict: untranslated rows left'); bad = 1
        bad |= lint(rows)
        import web_i18n_gen   # the settings page's table (tools/gen/i18n/web_strings.tsv, tools/web/i18n.js) is checked with the device's
        bad |= web_i18n_gen.check(mode == '--strict')
        for u in unused: print(f'i18n_gen: unused id {u}')
        for i in sorted(set(used) - {r[0] for r in rows}): print(f'i18n_gen: {i} ({used[i]}) is not in the tsv'); bad = 1
        for p in sorted(set(two)): print(f'i18n_gen: {p}: T( with something else than one id (use T_RAW or an id)'); bad = 1
        for p, t in ((OUT_H, h), (OUT_C, c), (OUT_D, dtxt)):
            if not os.path.isfile(p) or open(p, encoding='utf-8').read() != t: print(f'i18n_gen: {os.path.relpath(p, REPO)} is not the tsv\'s (run tools/gen/i18n_gen.py)'); bad = 1
        return bad
    if lint(rows): return 1
    open(OUT_H, 'w', encoding='utf-8').write(h)
    open(OUT_C, 'w', encoding='utf-8').write(c)
    open(OUT_D, 'w', encoding='utf-8').write(dtxt)
    return 0

if __name__ == '__main__':
    if sys.argv[1:2] in (["-h"], ["--help"]):          # usage only: before any side effect
        print(__doc__); sys.exit(0)
    sys.exit(main())
