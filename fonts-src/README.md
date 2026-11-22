# fonts-src: the original fonts and icon sources

The player's four UI fonts (`fonts/ui-tc.ttf`, `ui-sc.ttf`, `ui-ext.ttf`, `ui-icon.ttf`) are built from the files in this folder by `make fonts-all`
(see `docs/dev/fonts.md`). The files here are unmodified copies of the upstream releases; every build step is in `tools/gen/`.
Licence of all fonts: SIL Open Font License 1.1 (`OFL.txt`; each upstream's own copy with its copyright line is in `licenses/`). Icons: MIT and Apache-2.0 (licence files next to them).

## Noto fonts (SIL OFL 1.1)

| File | Upstream | Version | Bytes | SHA-256 |
|---|---|---|---|---|
| `noto-cjk/NotoSansTC-Regular.otf` | https://github.com/notofonts/noto-cjk, `Sans/SubsetOTF/TC/`, tag `Sans2.004` (commit 523d033d6cb47f4a80c58a35753646f5c3608a78) | 2.004 | 5683368 | `5bab0cb3c1cf89dde07c4a95a4054b195afbcfe784d69d75c340780712237537` |
| `noto-cjk/NotoSansSC-Regular.otf` | same, `Sans/SubsetOTF/SC/` | 2.004 | 8331336 | `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9` |
| `noto-extra/NotoSans.ttf` (Latin, Greek, Cyrillic; variable `wdth,wght`) | https://github.com/google/fonts, `ofl/notosans/NotoSans[wdth,wght].ttf`, commit 06bfe0d62a98375cb09c942fa18e68bd32cff057 (2026-11-22) | 2.015 | 2049096 | `bfb7bb691513f12e734dc346c03a03f784912432d7e3fa8e56efcf906fe86b3d` |
| `noto-extra/NotoSansKR.ttf` (Hangul; variable `wght`) | same commit, `ofl/notosanskr/NotoSansKR[wght].ttf` | 2.004-H2 | 10414588 | `194018e6b2b293a7964f037b25c0249ce1418bc9ab3c971060a03aa57861e252` |
| `noto-extra/NotoSansDevanagari.ttf` (variable `wdth,wght`) | same commit, `ofl/notosansdevanagari/NotoSansDevanagari[wdth,wght].ttf` | 2.007 | 641944 | `14ec4af41f27482216d1c2229f417ff9b1425e1babb014e57d1d40d03229853e` |
| `noto-extra/NotoSansSymbols.ttf` (variable `wght`) | same commit, `ofl/notosanssymbols/NotoSansSymbols[wght].ttf` | 2.003 | 372684 | `f7e7e04b4a24b6c78893d50cbfd2b2f6cae49617ab047bfef668d252adb128f7` |
| `noto-extra/NotoSansSymbols2.ttf` | same commit, `ofl/notosanssymbols2/NotoSansSymbols2-Regular.ttf` | 2.008 | 1233128 | `7d5fb73b7ca67a6798101741f5d280a3d016a56a197afcd4199dbb57b4b82a21` |
| `noto-extra/NotoSansMath.ttf` | same commit, `ofl/notosansmath/NotoSansMath-Regular.ttf` | 3.000 | 1015396 | `3f495fe933c06786e4d5f6d86b8ee70b6753a68ee3b9d87528726de0f6e2c47d` |
| `noto-extra/NotoEmoji.ttf` (monochrome emoji; variable `wght`) | same commit, `ofl/notoemoji/NotoEmoji[wght].ttf` | 3.002 | 1982596 | `de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551` |

The two CJK files are byte-identical to the files the first builds of the player used (checked by SHA-256). The `noto-extra` files are renamed (the upstream names contain
brackets); their contents are the upstream bytes. Variable fonts are pinned to their default instance when a font is built. Order in which `ui-ext.ttf` takes a character:
NotoSans, NotoSansKR, NotoSansDevanagari, NotoSansSymbols, NotoSansSymbols2, NotoSansMath, NotoEmoji (`EXT_SOURCES` in `tools/gen/make_font_fallback.py`).

## Icons (`icons/`, the layout `tools/gen/make_icons.py --src` expects)

| File | Upstream | Version | Licence | Bytes | SHA-256 |
|---|---|---|---|---|---|
| `icons/tabler/package/dist/fonts/tabler-icons.ttf` | npm `@tabler/icons-webfont`, https://github.com/tabler/tabler-icons (tarball SHA-256 `44d285b2f266b807cc864e2d4a831552fa8e6b1b3af38dd407501ea58dfb13ee`) | 3.52.0 | MIT (`icons/tabler/package/LICENSE`) | 1964868 | `cd4af77212fd4a83679362fb12c7e585078c2498719799d646e50518a5397cc6` |
| `icons/msym/package/material-symbols-rounded.woff2` | npm `material-symbols`, https://github.com/google/material-design-icons (tarball SHA-256 `ca62c2e360fbee53341610eb1d97c783c96480cb03aa135ba2ab67c290a473e4`) | 0.49.0 | Apache-2.0 (`icons/msym/package/LICENSE`) | 5438332 | `9f7fd471e65425ff504c00133f92d807f7acd0ebe853c1a27b669976e64474cc` |

Only the glyphs listed in `app/main/ui/icons.def` end up in `ui-icon.ttf`.

## Changing something

- Add a character: `docs/dev/fonts.md`. Replace a source font: put the new file here under the same name, update this table, run `make fonts-all`, then `make fonts-check` after copying the result to `fonts/`.
- Licence: a font built from these files (a subset, merged, renamed) may be shipped; keep `OFL.txt` with it. The Noto name is a trademark of Google LLC; the built files are named `ui-*.ttf`.
