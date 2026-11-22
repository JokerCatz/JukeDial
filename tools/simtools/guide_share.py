#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Guide image sharing: an image whose bytes are identical in all three languages lives once in docs/guide/img/, the rest per language.

usage: guide_share.py [--out docs/guide]      decide by content, move files, rewrite the Markdown links (also README*.md)

guide_shots.py and guide_web.py call share() after they write, so the decision is made at generation time from the bytes, never from a
hand-kept list: a text change in one language makes that image differ, and the next run puts it back into every language folder.
An image identical in only two languages stays per language. The link check is in tools/check/check_paths.py (check_links)."""
import argparse, glob, os, re, shutil, sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LANGS = ("zh-TW", "zh-CN", "en")
LINK = re.compile(r"(\]\()((?:\.\./)?img/|docs/guide/(?:(?:zh-TW|zh-CN|en)/)?img/)([^)\s]+)(\))")


def _same(paths):
    data = [open(p, "rb").read() for p in paths]
    return all(d == data[0] for d in data)


def share(out):
    """Rebalance docs/guide images by content; returns (shared count, per-language count)."""
    shared = os.path.join(out, "img"); os.makedirs(shared, exist_ok=True)
    for f in os.listdir(shared):   # un-share first (copy into the language folders) so every name is judged from fresh bytes
        if f.endswith(".png"):
            for l in LANGS:
                d = os.path.join(out, l, "img"); os.makedirs(d, exist_ok=True)
                if not os.path.exists(os.path.join(d, f)): shutil.copyfile(os.path.join(shared, f), os.path.join(d, f))
            os.remove(os.path.join(shared, f))
    names = sorted({f for l in LANGS for f in os.listdir(os.path.join(out, l, "img")) if f.endswith(".png")}) if all(os.path.isdir(os.path.join(out, l, "img")) for l in LANGS) else []
    ns = 0
    for f in names:
        ps = [os.path.join(out, l, "img", f) for l in LANGS]
        if all(os.path.exists(p) for p in ps) and _same(ps):
            shutil.move(ps[0], os.path.join(shared, f)); [os.remove(p) for p in ps[1:]]; ns += 1
    sharedset = set(os.listdir(shared))
    def fix(lang):
        def sub(m):
            if m.group(3) not in sharedset and m.group(3) not in names: return m.group(0)   # not one of ours (a photo, an outside file)
            sh = m.group(3) in sharedset
            if m.group(2).startswith("docs/"):
                pre = "docs/guide/img/" if sh else "docs/guide/%s/img/" % (lang or "en")
            else:
                pre = "../img/" if sh else "img/"
            return m.group(1) + pre + m.group(3) + m.group(4)
        return sub
    docs = [(l, p) for l in LANGS for p in glob.glob(os.path.join(out, l, "*.md"))]
    root = os.path.dirname(os.path.dirname(os.path.abspath(out)))
    for rd, lang in (("README.md", "en"), ("README.zh-TW.md", "zh-TW"), ("README.zh-CN.md", "zh-CN")):
        if os.path.exists(os.path.join(root, rd)): docs.append((lang, os.path.join(root, rd)))
    for lang, p in docs:
        t = open(p, encoding="utf-8").read(); n = LINK.sub(fix(lang), t)
        if n != t: open(p, "w", encoding="utf-8").write(n)
    return ns, len(names) - ns


def main():
    a = argparse.ArgumentParser(); a.add_argument("--out", default=os.path.join(REPO, "docs", "guide")); o = a.parse_args()
    ns, np_ = share(o.out); print(f"guide images: {ns} shared in img/, {np_} per language")


if __name__ == "__main__":
    main()
