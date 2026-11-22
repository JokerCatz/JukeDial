#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Builds card-template.zip: the TF card's directory format, holding only what we may redistribute (stdlib only).
Contents: the folder skeleton (each folder with a README.txt in 繁中/简中/English), config templates, our own generated demo drum kit.
The station lists are NOT copied: they are parsed out of app/main/radio_svc.c (DEFAULTS / DEFAULTS_CN), so there is one source.
Fonts (OFL, approved) are included only when the local make fonts-build output exists (never in git, never downloaded). Never included: Wi-Fi files, music, caches (_player/index, _player/album: the firmware makes them).
usage: make_card_zip.py [--out FILE] [--list]     (deterministic: fixed mtimes, sorted entries; default out .local/card-template.zip)"""
import argparse, os, re, subprocess, sys, tempfile, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MTIME = (2026, 1, 1, 0, 0, 0)

# Every card path the firmware uses (inventory: the internal notes, the internal notes).
# (zip path prefix or exact, source-literal that proves the firmware uses it)
FW_PATHS = [
    ("_player/", "PC_INT_NAME"), ("_player/config/", 'PC_INT "/config"'), ("_player/config/radio.txt", "radio.txt"), ("_player/config/radio_cn.txt", "radio_%s.txt"),
    ("_player/config/keys.txt.example", "keys.txt"), ("_player/config/sound/", 'PC_CFG_DIR "/sound"'),
    ("export/", 'PC_SD "/export"'), ("lists/", 'PC_SD "/lists"'), ("vocab/", 'PC_SD "/vocab"'), ("photos/", 'PC_SD "/photos"'),
    ("fortune/", 'PC_SD "/fortune"'), ("chiptune/", 'PC_SD "/chiptune"'), ("_player/fonts/", 'PC_INT "/fonts"'), ("_player/sleep/", 'PC_INT "/sleep"'),
    ("_player/themes/", 'PC_INT "/themes/'), ("instrument/", 'PC_SD "/instrument"'), ("instrument/kits/", 'PC_SD "/instrument/kits"'), ("_player/loops/", 'PC_INT "/loops"'),
    ("_player/faces.zip", "PC_FACES_ZIP"), ("_player/splash/", "PC_SPLASH_DIR"), ("_player/splash.jpg", "PC_SPLASH_JPG"),   # splash.jpg: the user's own general picture, OPTIONAL, never shipped (no art for it); splash/<mode>.jpg: the pack's default per-mode pictures (tools/gen/make_splash.py)
     ("_player/pack.txt", "PC_PACK_TXT"), ("README.txt", None),
]
# the device-owned folders live under _player/ (components/player_core/include/pc/pc_paths.h); the keys of READMES and the source files below are the short names
INTERNAL = (("config/", "_player/config/"), ("themes/", "_player/themes/"), ("fonts/", "_player/fonts/"), ("sleep/", "_player/sleep/"), ("instrument/loops/", "_player/loops/"))
def internal(p):
    for a, b in INTERNAL:
        if p.startswith(a): return b + p[len(a):]
    return p
FORBIDDEN = re.compile(r"wifi|passw|credential|secret|\.env|\.pem|\.key$", re.I)

def c_strings(src, name):
    """the string literals of `static const char *const NAME...` in radio_svc.c, joined, C escapes (octal \\ooo, \\n) decoded to UTF-8"""
    m = re.search(r"\b" + name + r"\b[^=]*=\s*(.*?);\s*\n", src, re.S)
    body = m.group(1)
    out = bytearray()
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', body):
        i = 0
        while i < len(lit):
            c = lit[i]
            if c != "\\": out += c.encode(); i += 1; continue
            n = lit[i + 1]
            if n in "01234567":
                j = i + 1
                while j < len(lit) and j < i + 4 and lit[j] in "01234567": j += 1
                out.append(int(lit[i + 1:j], 8)); i = j
            else:
                out += {"n": b"\n", "t": b"\t", "\\": b"\\", '"': b'"'}[n]; i += 2
    return bytes(out)

def radio_lists():
    src = open(os.path.join(REPO, "app/main/radio_svc.c"), encoding="utf-8").read()
    return c_strings(src, "DEFAULTS"), c_strings(src, "DEFAULTS_CN")

def font_dir():
    """make fonts-build's output (generated, never in git): $FONTS_OUT, else .local/fontsets/out of this checkout or of the main checkout"""
    if os.environ.get("FONTS_OUT"): return os.path.abspath(os.environ["FONTS_OUT"])
    cands = [os.path.join(REPO, ".local", "fontsets", "out")]
    try:
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=REPO, capture_output=True, text=True).stdout.strip()
        if common: cands.append(os.path.join(os.path.dirname(common), ".local", "fontsets", "out"))
    except OSError: pass
    for c in cands:
        if all(os.path.isfile(os.path.join(c, f)) for f in FONT_NAMES): return c
    return cands[0]

FONT_NAMES = ("ui-tc.ttf", "ui-sc.ttf", "ui-ext.ttf")
def faces_zip():
    """the HUD faces plug-in (tools/gen/make_faces.py: one STORE-method zip, deterministic): built here when Pillow and the fonts are there, else .local/faces.zip (make faces-zip)"""
    try:
        import make_faces
        return make_faces.pack(make_faces.build()[1])
    except Exception as e:
        f = os.path.join(REPO, ".local", "faces.zip")
        if not os.path.isfile(f): raise SystemExit(f"make_card_zip: no faces.zip ({e}); run make faces-zip")
        return open(f, "rb").read()

def splash_files():
    """the default power-on pictures, _player/splash/<mode>.jpg (tools/gen/make_splash.py: icons on black, 360 x 360 for the round panel (centred on black on the square one), deterministic): built here when Pillow and fonts/ui-icon.ttf are there, else .local/splash/ (make splash).
    Treated like faces.zip: the pack is built with them or not at all. They are optional FOR THE FIRMWARE (a card without them boots as it always did), so PC_PACK_MIN is not raised."""
    try:
        import make_splash
        make_splash.need_pillow()
        return {f"_player/splash/{m}.jpg": make_splash.encode(make_splash.render(m)) for m in make_splash.MODES}
    except BaseException as e:   # SystemExit from the tool's STOP lines too
        out = {}
        for m in ("player", "usb", "inst", "cast", "charge"):
            f = os.path.join(REPO, ".local", "splash", m + ".jpg")
            if not os.path.isfile(f): raise SystemExit(f"make_card_zip: no splash pictures ({e}); run make splash")
            out[f"_player/splash/{m}.jpg"] = open(f, "rb").read()
        return out

def font_files():
    d = font_dir()
    if not all(os.path.isfile(os.path.join(d, f)) for f in FONT_NAMES): return {}
    out = {"fonts/" + f: open(os.path.join(d, f), "rb").read() for f in FONT_NAMES}
    out["_player/fonts/OFL.txt"] = open(os.path.join(REPO, "tools", "OFL-1.1.txt"), "rb").read()
    return out

def three(tc, sc, en): return f"{tc}\n\n{sc}\n\n{en}\n".encode()

READMES = {
"": three(
"""TF 卡範本（繁體中文）
把這個 zip 的內容解壓到「剛格式化好的 FAT32 或 exFAT 卡」根目錄（MBR 分割區，單一檔案小於 2 GiB）。
資料夾：_player 機器自己用的（裡面有 config 設定、fonts 字型、sleep 白噪音庫，備份就複製它）、export 音樂（一個專輯一個資料夾，放 MP3）、lists 字型清單包、vocab 單字卡、photos 相簿（JPEG）、
fortune 運勢文字、chiptune 晶片音樂（每個系統一個 zip）、fonts 字型、sleep 白噪音庫、instrument 樂器（鼓組、循環錄音）。
索引與縮圖（_player 裡的 index、album）由機器自己建立，不要放。第一次開機會出現語言選擇頁（繁中／简中／English），之後可在設定改。
此範本不含任何音樂與 Wi-Fi 設定，要自己放。
字型：fonts 裡的 ui-tc.ttf、ui-sc.ttf、ui-ext.ttf 是 Noto Sans 的子集（SIL OFL 1.1，授權文字在 _player/fonts/OFL.txt）。機器本身也有內建在快閃記憶體的字型包，沒有這些檔案也能用；網頁工具可以依你的曲庫重新產生這些字型。
注意：這個範本是給「新的、空的卡」用的。解壓到已有內容的卡上，會取代卡上原有的 _player/sleep/library.muk（其他同名檔案也會被覆蓋），請先備份。
授權：_player/sleep/library.muk 是本專案自己合成產生的（tools/gen/make_noise_pack.py），以 CC0 釋出。
HUD 錶面：_player/faces.zip 是時鐘、碼錶、倒數、番茄鐘的 HUD 錶面套件（一個不壓縮的 zip）。少了它，錶面選項會顯示「缺 faces.zip」，各功能改用內建的一般錶面。""",
"""TF 卡模板（简体中文）
把这个 zip 的内容解压到「刚格式化好的 FAT32 或 exFAT 卡」根目录（MBR 分区，单个文件小于 2 GiB）。
文件夹：_player 机器自己用的（里面有 config 设置、fonts 字体、sleep 白噪音库，备份就复制它）、export 音乐（一张专辑一个文件夹，放 MP3）、lists 字体列表包、vocab 单词卡、photos 相册（JPEG）、
fortune 运势文字、chiptune 芯片音乐（每个系统一个 zip）、fonts 字体、sleep 白噪音库、instrument 乐器（鼓组、循环录音）。
索引和缩略图（_player 裡的 index、album）由机器自己生成，不要放。第一次开机会出现语言选择页（繁中／简中／English），之后可在设置里改。
此模板不含任何音乐和 Wi-Fi 设置，要自己放。
字体：fonts 里的 ui-tc.ttf、ui-sc.ttf、ui-ext.ttf 是 Noto Sans 的子集（SIL OFL 1.1，许可文本在 _player/fonts/OFL.txt）。机器本身也有内置在闪存里的字体包，没有这些文件也能用；网页工具可以按你的曲库重新生成这些字体。
注意：这个模板是给「新的、空的卡」用的。解压到已有内容的卡上，会替换卡上原有的 _player/sleep/library.muk（其他同名文件也会被覆盖），请先备份。
许可：_player/sleep/library.muk 是本项目自己合成生成的（tools/gen/make_noise_pack.py），以 CC0 发布。
HUD 表盘：_player/faces.zip 是时钟、码表、倒数、番茄钟的 HUD 表盘包（一个不压缩的 zip）。少了它，表盘选项会显示「缺少表盘包」，各功能改用内置的普通表盘。""",
"""TF card template (English)
Unzip this into the root of a freshly formatted FAT32 or exFAT card (MBR partition table, every file under 2 GiB).
Folders: _player the player's own (config settings, fonts, sleep noise library inside; copy it to back up), export music (one folder per album, MP3 inside), lists glyph-pack lists, vocab word decks, photos album (JPEG),
fortune fortune texts, chiptune chip music (one zip per system), fonts fonts, sleep white-noise library, instrument (drum kits, looper takes).
Indexes and thumbnails (index, album inside _player) are made by the player: do not add them. The first boot shows a language page
(繁中 / 简中 / English); change it later in settings. This template holds no music and no Wi-Fi settings: add those yourself.
Fonts: ui-tc.ttf, ui-sc.ttf and ui-ext.ttf in fonts are subsets of Noto Sans (SIL OFL 1.1, licence text in _player/fonts/OFL.txt). The player also has a built-in font pack in flash, so it works without these files; the web tool can rebuild them for your library.
Note: this template is for a NEW, empty card. Unzipping it over a card that already has files replaces your _player/sleep/library.muk (and any other file of the same name): back up first.
Licence: _player/sleep/library.muk is generated by this project (tools/gen/make_noise_pack.py) and released under CC0.
HUD faces: _player/faces.zip is the HUD face pack of the clock, stopwatch, countdown and pomodoro (one uncompressed zip). Without it the face choice shows "No faces.zip" and each feature uses its plain built-in face."""),
"config": three(
"""config：設定資料夾（在 _player 裡）。app.txt、stats.txt、games.txt 由機器寫入，不要手改。radio.txt（預設清單）與 radio_cn.txt（简中清單）是電台清單，
一行一台：名稱|網址|備援網址|備援網址（只支援 http）。再加一份 radio_<名稱>.txt 就多一份清單。keys.txt.example 是外接模式按鍵範例，改名為 keys.txt 才會生效。
sound 放音效預設（.toml）。Wi-Fi 設定要自己從網頁或機器上設，不在範本裡。
說明頁的設定網址：app.txt 的 setup_url 可放多個網址（空白隔開，最多 4 個，第一個是主要，其餘備用），說明頁每個網址一個 QR。例：setup_url=https://github.com/JokerCatz/JukeDial https://gitee.com/jokercatz/JukeDial""",
"""config：设置文件夹。app.txt、stats.txt、games.txt 由机器写入，不要手改。radio.txt（默认列表）和 radio_cn.txt（简中列表）是电台列表，
一行一台：名称|网址|备用网址|备用网址（只支持 http）。再加一份 radio_<名称>.txt 就多一份列表。keys.txt.example 是外接模式按键示例，改名为 keys.txt 才会生效。
sound 放音效预设（.toml）。Wi-Fi 设置要自己从网页或机器上设置，不在模板里。
说明页的设置网址：app.txt 的 setup_url 可放多个网址（空格隔开，最多 4 个，第一个是主要，其余备用），说明页每个网址一个 QR。例：setup_url=https://github.com/JokerCatz/JukeDial https://gitee.com/jokercatz/JukeDial""",
"""config: settings. app.txt, stats.txt and games.txt are written by the player; do not edit them. radio.txt (default list) and radio_cn.txt
(Simplified Chinese list) are station lists, one per line: name|url|backup url|backup url (http only). Add radio_<name>.txt for one more list.
keys.txt.example is a sample key map for external mode: rename it to keys.txt to use it. sound holds sound presets (.toml).
Wi-Fi is set from the web page or the device, never from this template.
Help-page addresses: setup_url in app.txt may hold several addresses (split by spaces, up to 4, the first is the main one, the rest are mirrors); the help page shows one QR per address. Example: setup_url=https://github.com/JokerCatz/JukeDial https://gitee.com/jokercatz/JukeDial"""),
"config/sound": three(
"""_player/config/sound：音效預設（.toml）。warm.toml 是我們自己寫的範例。""",
"""_player/config/sound：音效预设（.toml）。warm.toml 是我们自己写的示例。""",
"""_player/config/sound: sound presets (.toml). warm.toml is a sample we wrote."""),
"themes": three(
"""_player/themes：配色主題（每個一個 .txt）。template.txt 列出每個顏色項目與用途；複製、改名、改顏色，再在 app.txt 寫 theme=<名稱>。""",
"""_player/themes：配色主题（每个一个 .txt）。template.txt 列出每个颜色项目与用途；复制、改名、改颜色，再在 app.txt 里写 theme=<名称>。""",
"""_player/themes: colour themes (one .txt each). template.txt lists every colour token and what it is for; copy it, rename it, change the colours, and put theme=<name> in app.txt."""),
"export": three(
"""export：音樂。一個專輯一個資料夾，裡面放 MP3（可放 cover.jpg 當封面）。範本沒有音樂，請放自己擁有的檔案。""",
"""export：音乐。一张专辑一个文件夹，里面放 MP3（可放 cover.jpg 当封面）。模板没有音乐，请放自己拥有的文件。""",
"""export: music. One folder per album with MP3 files (cover.jpg is the cover). The template has no music: add files you own."""),
"lists": three(
"""lists：字型清單包（zip，由 tools/gen/make_lists.py 產生）。範本沒有，請用工具產生再放進來。""",
"""lists：字形列表包（zip，由 tools/gen/make_lists.py 生成）。模板里没有，请用工具生成后放进来。""",
"""lists: glyph-pack lists (zip, made by tools/gen/make_lists.py). None in the template: generate and add them."""),
"vocab": three(
"""vocab：單字卡（zip，由 tools/gen/make_vocab.py 產生）。機器內建一組範例，這裡放自己的。""",
"""vocab：单词卡（zip，由 tools/gen/make_vocab.py 生成）。机器内置一组示例，这里放自己的。""",
"""vocab: word decks (zip, made by tools/gen/make_vocab.py). One sample is built in; put your own here."""),
"photos": three(
"""photos：相簿。放 JPEG（可有子資料夾）；機器第一次會轉成縮圖快取（_player/album）。""",
"""photos：相册。放 JPEG（可有子文件夹）；机器第一次会转成缩略图缓存（_player/album）。""",
"""photos: album. Put JPEG files here (subfolders work); the first run converts them into a cache (_player/album)."""),
"fortune": three(
"""fortune：運勢文字。omikuji.txt.example 與 wheel.txt.example 是格式範例，改名（去掉 .example）才會取代內建文字。cards.txt 由機器寫入（抽卡收藏）。""",
"""fortune：运势文字。omikuji.txt.example 和 wheel.txt.example 是格式示例，改名（去掉 .example）才会取代内置文字。cards.txt 由机器写入（抽卡收藏）。""",
"""fortune: fortune texts. omikuji.txt.example and wheel.txt.example show the formats; rename them (drop .example) to replace the built-in texts. cards.txt is written by the player (gacha collection)."""),
"chiptune": three(
"""chiptune：晶片音樂。每個系統一個 zip（例如 nes.zip），裡面是 NSF、GBS、VGM 等遊戲音樂檔。範本沒有，請放自己合法取得的檔案。""",
"""chiptune：芯片音乐。每个系统一个 zip（例如 nes.zip），里面是 NSF、GBS、VGM 等游戏音乐文件。模板里没有，请放自己合法取得的文件。""",
"""chiptune: chip music. One zip per system (for example nes.zip) holding NSF, GBS, VGM and similar files. None in the template: add files you obtained legally."""),
"fonts": three(
"""fonts：字型。ui-tc.ttf、ui-sc.ttf、ui-ext.ttf（由 make fonts-build 從 Noto 字型產生，SIL OFL 1.1，見 OFL.txt）。沒有這些檔案時用機器內建的字型包。""",
"""fonts：字体。ui-tc.ttf、ui-sc.ttf、ui-ext.ttf（由 make fonts-build 从 Noto 字体生成，SIL OFL 1.1，见 OFL.txt）。没有这些文件时使用机器内置的字体包。""",
"""fonts: ui-tc.ttf, ui-sc.ttf, ui-ext.ttf (made by make fonts-build from Noto fonts, SIL OFL 1.1, see OFL.txt). Without them the built-in font pack is used."""),
"sleep": three(
"""sleep：白噪音庫 library.muk（zip）。範本附的是本專案自己合成的情境包（噪音、雨、海浪、風、溪流、營火、夜蟲，CC0）；可換成自己的 library.muk。噪音顏色不需要檔案。""",
"""sleep：白噪音库 library.muk（zip）。模板附带的是本项目自己合成的情景包（噪音、雨、海浪、风、溪流、营火、夜虫，CC0）；可换成自己的 library.muk。噪音颜色不需要文件。""",
"""sleep: the white-noise library library.muk (zip). The template carries the scene pack this project synthesised (noise, rain, waves, wind, stream, campfire, night insects; CC0); replace it with your own library.muk. The noise colours need no file."""),
"instrument": three(
"""instrument：樂器。kits 放鼓組與取樣。""",
"""instrument：乐器。kits 放鼓组和采样。""",
"""instrument: the instrument app. kits holds drum kits and samples."""),
"instrument/kits": three(
"""instrument/kits：一個鼓組一個資料夾，裡面有 kit.txt 與 WAV。demo 是我們自己合成的示範鼓組。""",
"""instrument/kits：一个鼓组一个文件夹，里面有 kit.txt 和 WAV。demo 是我们自己合成的示范鼓组。""",
"""instrument/kits: one folder per kit with kit.txt and WAV files. demo is a drum kit we synthesised."""),
"instrument/loops": three(
"""_player/loops：循環錄音（loop_001.jcl 等），由機器寫入。""",
"""_player/loops：循环录音（loop_001.jcl 等），由机器写入。""",
"""_player/loops: looper takes (loop_001.jcl and so on), written by the player."""),
}

PLAYER_README = three(
"""_player：機器自己用的資料夾（設定、字型、白噪音庫 sleep、索引、記錄）。不要放自己的檔案；備份整張卡時複製這個資料夾就夠了。""",
"""_player：机器自己用的文件夹（设置、字体、白噪音库 sleep、索引、日志）。不要放自己的文件；备份时复制这个文件夹就够了。""",
"""_player: the player's own folder (settings, fonts, the white-noise library in sleep, indexes, logs). Do not put your own files here; to back up the device, copy this folder.""")

KEYS_EXAMPLE = """# keys.txt: external-mode key map (rename from keys.txt.example). One item per line, # starts a comment. 外接模式按鍵設定。
# layer NAME (<= 12 bytes) / knob volume|scroll|arrows|none / key NAME consumer 0xHH | combo ctrl+c | text "Hello\\n" | macro ctrl+a, wait 50, ctrl+c
# limits: 8 layers, 8 keys per layer, text ASCII only (<= 64 bytes), macro <= 16 steps, wait <= 2000 ms, file <= 8 KB
layer media
knob  volume
key   播放  consumer 0xCD
key   下首  consumer 0xB5
layer edit
key   複製  combo ctrl+c
key   簽名  text "Hello\\n"
key   巨集  macro ctrl+a, wait 50, ctrl+c
"""
OMIKUJI_EXAMPLE = "# omikuji.txt: one verse per line, RANK|verse (RANK = 大吉 吉 中吉 小吉 末吉 凶, or * / no bar for any); / starts a new line. 籤詩。\n大吉|風來帆自滿/今天的歌剛剛好\n凶|雨後見虹/先聽一首慢歌\n*|隨機播放也是一種緣分\n"
WHEEL_EXAMPLE = "# wheel.txt: one choice per line (<= 48 bytes), at least 2; otherwise the wheel picks music folders. 轉盤選項。\n聽搖滾\n聽爵士\n聽輕音樂\n"

def demo_kit():
    d = tempfile.mkdtemp(prefix="cardkit")
    subprocess.run([sys.executable, os.path.join(REPO, "tools/gen/make_demo_kit.py"), d], check=True, stdout=subprocess.DEVNULL)
    return {f"instrument/kits/demo/{f}": open(os.path.join(d, f), "rb").read() for f in sorted(os.listdir(d))}

# folders whose scan could list a README.txt (lists/: lists_ctl.c:52 and ui/scr_lists.c:147 take any name pc_list_path accepts): no per-folder README, the text is a section of the top README
# Provably skipped elsewhere: chiptune chip_ctl.c:57 (.zip), vocab ui/scr_vocab.c:485 (.zip), instrument/loops inst_loopfile.c:25 (.jcl), config/sound fx_state.c:357 (.toml),
# photos pc_album.c:17 (.jpg/.jpeg), export pc_library.c:62 (top level: sub-directories only), instrument/kits inst_kit.c:32 (opendir on a file fails),
# config pc_radio.c:244 (radio.txt / radio_*.txt only); fortune, fonts, sleep, instrument: fixed file names, no directory scan.
NO_README = ["lists"]

def git_version():
    """YYYY.MM.DD-<short> of HEAD: the same string tools/web_publish.py gives the release (the commit's date: the same tree gives the same pack)"""
    g = lambda *x: subprocess.run(["git", "-C", REPO, *x], capture_output=True, text=True).stdout.strip()
    return g("log", "-1", "--format=%cd", "--date=format:%Y.%m.%d") + "-" + g("rev-parse", "--short", "HEAD")

def pack_rev():
    """PC_PACK_MIN of components/player_core/include/pc/pc_pack.h: the pack revision this tree's firmware wants (bump rule in that header)"""
    return int(re.search(r"#define\s+PC_PACK_MIN\s+(\d+)", open(os.path.join(REPO, "components/player_core/include/pc/pc_pack.h")).read()).group(1))

def pack_stamp(files, version):
    """_player/pack.txt: pack=<rev>, version=<release>, then 'sha256  path' for every other file (sorted); read by app/main/pack_stamp.c (first two lines only)"""
    import hashlib
    lines = [f"pack={pack_rev()}", f"version={version}"] + [f"{hashlib.sha256(b).hexdigest()}  {p}" for p, b in sorted(files.items()) if not p.endswith("/")]
    return ("\n".join(lines) + "\n").encode()

def build(version=None):
    """{path: bytes}; a path ending in / is a folder"""
    main, cn = radio_lists()
    files = {"README.txt": READMES[""], "config/radio.txt": main, "config/radio_cn.txt": cn,
             "config/keys.txt.example": KEYS_EXAMPLE.encode(),
             "config/sound/warm.toml": open(os.path.join(REPO, "sim/fixture/_player/config/sound/warm.toml"), "rb").read(),
             "fortune/omikuji.txt.example": OMIKUJI_EXAMPLE.encode(), "fortune/wheel.txt.example": WHEEL_EXAMPLE.encode()}
    import gen_theme_template
    files["themes/template.txt"] = gen_theme_template.build().encode("utf-8")
    import gen_card_themes
    files.update({k: v.encode("utf-8") for k, v in gen_card_themes.build().items()})
    for d, t in READMES.items():
        if d and d not in NO_README: files[d + "/README.txt"] = t
    files["README.txt"] += b"".join(b"\n---- " + d.encode() + b"/ ----\n" + READMES[d] for d in NO_README)
    files.update(demo_kit())
    fonts = font_files()
    if fonts: files.update(fonts)
    else: print("make_card_zip: no fonts in the zip (no make fonts-build output; searched " + font_dir() + ")", file=sys.stderr)
    import make_noise_pack
    files["_player/sleep/library.muk"] = make_noise_pack.build()[0]
    files = {internal(p): b for p, b in files.items()}
    files["_player/faces.zip"] = faces_zip()
    files.update(splash_files())
    files["_player/README.txt"] = PLAYER_README
    files["_player/pack.txt"] = pack_stamp(files, version or git_version())
    dirs = {""}
    for p in files:
        parts = p.split("/")[:-1]
        for i in range(1, len(parts) + 1): dirs.add("/".join(parts[:i]))
    dirs |= set(NO_README)
    for d in dirs - {""}: files[d + "/"] = b""
    return dict(sorted(files.items()))

def write(out, files):
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p, b in files.items():
            zi = zipfile.ZipInfo(p, MTIME)
            zi.create_system = 3
            zi.external_attr = (0o40755 << 16 | 0x10) if p.endswith("/") else (0o100644 << 16)
            zi.compress_type = zipfile.ZIP_STORED if p.endswith(("/", ".muk", "_player/faces.zip")) else zipfile.ZIP_DEFLATED
            z.writestr(zi, b)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, ".local", "card-template.zip"))
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--version", help="release version for _player/pack.txt (default: the HEAD date + short hash, as web_publish.py)")
    a = ap.parse_args()
    files = build(a.version)
    for p in files:
        if FORBIDDEN.search(p): sys.exit(f"make_card_zip: forbidden name {p}")
    if a.list:
        if not any(p.startswith("fonts/ui") for p in files): print("(no fonts: no make fonts-build output found)")
        for p, b in files.items(): print(f"{len(b):8d}  {p}")
        return
    write(a.out, files)
    print(f"{a.out}: {sum(1 for p in files if not p.endswith('/'))} files, {sum(1 for p in files if p.endswith('/'))} folders")

if __name__ == "__main__":
    main()
