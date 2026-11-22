#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Host check for tools/gen/make_card_zip.py: every path in the zip is one the firmware uses (FW_PATHS, each proven by a literal in app/main
or components), no wifi/credential-looking name or content, the radio lists equal the firmware's text, keys.txt.example parses with the real
pc_extkeys parser (gcc, skipped if absent). usage: check_card_zip.py [zip]"""
import os, re, subprocess, sys, tempfile, zipfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen"))   # tools/gen: sibling tools live in other folders
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_card_zip as M

zpath = sys.argv[1] if len(sys.argv) > 1 else os.path.join(M.REPO, ".local", "card-template.zip")
fail = []
src = ""
for root in ("app/main", "components"):
    for dp, dn, fn in os.walk(os.path.join(M.REPO, root)):
        dn[:] = [d for d in dn if d not in ("managed_components", "build-host", "build-host-asan")]
        for f in fn:
            if f.endswith((".c", ".h", ".cpp")): src += open(os.path.join(dp, f), encoding="utf-8", errors="replace").read()
for pre, lit in M.FW_PATHS:
    if lit and lit not in src: fail.append(f"inventory path {pre}: literal {lit!r} not found in firmware source")
z = zipfile.ZipFile(zpath)
for n in z.namelist():
    if not any(n == p or (p.endswith("/") and (n.startswith(p) or n + "/" == p)) for p, _ in M.FW_PATHS): fail.append(f"{n}: not a path the firmware uses")
    if M.FORBIDDEN.search(n): fail.append(f"{n}: credential-looking name")
    if not n.endswith("/") and re.search(rb"(?i)(ssid|psk|password)\s*[=:]", z.read(n)) and "keys" not in n: fail.append(f"{n}: credential-looking content")
# scanned folders: what the firmware's scan accepts (None = any name, so nothing but the folder may be in the zip), per the citations in make_card_zip.py
SCAN = {"lists/": None, "vocab/": (".zip",), "chiptune/": (".zip",), "_player/loops/": (".jcl",), "_player/config/sound/": (".toml",), "photos/": (".jpg", ".jpeg")}
for n in z.namelist():
    if n.endswith("/"): continue
    for d, ext in SCAN.items():
        if n.startswith(d) and "/" not in n[len(d):] and (ext is None or n.lower().endswith(ext)):
            if not (d == "_player/config/sound/" and n.endswith(".toml")): fail.append(f"{n}: would be picked up by the {d} scan")
    if n.endswith("README.txt") and n != "README.txt" and (n.startswith(("export/", "lists/")) and n.count("/") == 1 and n.startswith("lists/")): fail.append(f"{n}: README in a folder with no provable skip")
fnt = [n for n in z.namelist() if n.startswith("_player/fonts/") and n not in ("_player/fonts/", "_player/fonts/README.txt")]
if sorted(fnt) not in ([], sorted(["_player/fonts/OFL.txt", "_player/fonts/ui-tc.ttf", "_player/fonts/ui-sc.ttf", "_player/fonts/ui-ext.ttf"])): fail.append(f"fonts: only none or ui-tc.ttf ui-sc.ttf ui-ext.ttf + OFL.txt allowed, got {fnt}")
if any(n.endswith(".ttf") for n in fnt) and ("_player/fonts/OFL.txt" not in fnt or b"SIL OPEN FONT LICENSE Version 1.1" not in z.read("_player/fonts/OFL.txt")): fail.append("fonts: OFL.txt (SIL OFL 1.1) must ship with the fonts")
if "_player/sleep/library.muk" in z.namelist():      # our generated pack: a stored zip whose scenes only name sounds that are in it
    import io, json
    mz = zipfile.ZipFile(io.BytesIO(z.read("_player/sleep/library.muk")))
    names = set(mz.namelist())
    if not {"default_scenes.json", "seed.json"} <= names or any(i.compress_type != zipfile.ZIP_STORED for i in mz.infolist()): fail.append("_player/sleep/library.muk: not a stored pack with scenes + seed")
    else:
        for sc in json.loads(mz.read("default_scenes.json"))["scenes"]:
            for l in sc["resolved_sounds"]:
                if l["sha256"] + ".flac" not in names: fail.append(f"library.muk scene {sc['id']}: sound missing")
else: fail.append("_player/sleep/library.muk missing from the zip")
if "_player/faces.zip" not in z.namelist(): fail.append("_player/faces.zip missing from the zip")
else:                                                  # the HUD faces plug-in: a stored zip (the firmware reads it in place) with the shared sheets and the 8 faces
    import io
    fz = zipfile.ZipFile(io.BytesIO(z.read("_player/faces.zip")))
    fn = set(fz.namelist())
    if any(i.compress_type != zipfile.ZIP_STORED for i in fz.infolist()): fail.append("_player/faces.zip: an entry is compressed (must be STORE)")
    for f in ("rd_clock", "rd_sw", "rd_cd", "rd_pomo", "sq_clock", "sq_sw", "sq_cd", "sq_pomo"):
        if f + "/face.txt" not in fn: fail.append(f"_player/faces.zip: {f}/face.txt missing")
    if "sheets/sheets.txt" not in fn: fail.append("_player/faces.zip: sheets/sheets.txt missing")
    if z.getinfo("_player/faces.zip").compress_type != zipfile.ZIP_STORED: fail.append("_player/faces.zip is deflated inside the card template (stored expected)")
import make_splash   # the default power-on pictures: one per boot mode, the hardware JPEG route's constraints and the safe circle
for _m in make_splash.MODES:
    _n = f"_player/splash/{_m}.jpg"
    if _n not in z.namelist(): fail.append(f"{_n} missing from the zip")
    else: fail += make_splash.check_jpeg(z.read(_n), _n)
if "_player/pack.txt" not in z.namelist(): fail.append("_player/pack.txt missing from the zip")
else:                                                  # the version stamp: pack=<PC_PACK_MIN>, a version, and a correct sha256 for every other file
    import hashlib
    L = z.read("_player/pack.txt").decode().splitlines()
    if not L or L[0] != f"pack={M.pack_rev()}" or len(L) < 3 or not L[1].startswith("version=") or len(L[1]) < 9: fail.append("_player/pack.txt: bad pack=/version= lines")
    got = {l[66:]: l[:64] for l in L[2:]}
    for n in z.namelist():
        if n.endswith("/") or n == "_player/pack.txt": continue
        if got.get(n) != hashlib.sha256(z.read(n)).hexdigest(): fail.append(f"_player/pack.txt: sha256 of {n} missing or wrong")
main, cn = M.radio_lists()
if z.read("_player/config/radio.txt") != main or z.read("_player/config/radio_cn.txt") != cn: fail.append("radio lists differ from radio_svc.c")
for n in z.namelist():
    if not n.endswith("/") and not n.endswith((".wav", ".zip", ".muk", ".ttf", ".jpg")):
        try: z.read(n).decode("utf-8")
        except UnicodeDecodeError: fail.append(f"{n}: not UTF-8")
gcc = subprocess.run(["which", "gcc"], capture_output=True).returncode == 0
if gcc:
    d = tempfile.mkdtemp(prefix="cardchk")
    open(d + "/k.txt", "wb").write(z.read("_player/config/keys.txt.example"))
    open(d + "/t.c", "w").write('#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include "pc/pc_extkeys.h"\n'
        'int main(int c,char**v){FILE*f=fopen(v[1],"rb");static char b[9000];size_t n=fread(b,1,8192,f);static pc_extkeys_cfg_t g;pc_extkeys_res_t r;'
        'pc_extkeys_parse(b,n,&g,&r);printf("%d %d\\n",r.ok,r.total);return !(r.ok&&r.from_file);}\n')
    inc = os.path.join(M.REPO, "components/player_core/include")
    cc = subprocess.run(["gcc", "-I" + inc, d + "/t.c", os.path.join(M.REPO, "components/player_core/src/pc_extkeys.c"), os.path.join(M.REPO, "components/player_core/src/pc_i18n.c"), os.path.join(M.REPO, "components/player_core/src/pc_i18n_data.c"), "-o", d + "/t"], capture_output=True, text=True)
    if cc.returncode: fail.append("keys parser build: " + cc.stderr[:200])
    elif subprocess.run([d + "/t", d + "/k.txt"]).returncode: fail.append("keys.txt.example rejected by pc_extkeys")
if fail:
    print("\n".join("FAIL " + f for f in fail)); sys.exit(1)
print(f"card zip ok: {len(z.namelist())} entries, all paths used by the firmware, no credentials" + ("" if gcc else " (keys parse skipped: no gcc)"))
