#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Release-guide screenshots (docs/guide/<lang>/img/<id>[_round|_square].png), simulator only, fixture data only, deterministic.
usage: guide_shots.py [--out DIR] [--lang en,zh-TW,zh-CN] [--board round,square] [--only id,id] [--keep-going]
Every shot is a list of console/simulator commands (SHOTS below) run in a fresh fixture copy of the simulator (tools/simtools/sim_con.py), seed 1, a fixed
virtual clock. Round = `make sim` (360x360), square = `make sim-fp` (460x460). Shots that exist on both boards are written as <id>_round / <id>_square;
round-only ones as <id>. Afterwards run `guide_shots.py --compose` (gestures, hero) and tools/simtools/guide_web.py (web tool); `--optimize` shrinks PNGs losslessly.
No firmware change; pages are the existing look."""
import argparse, os, subprocess, sys, shutil
from guide_share import share   # identical-in-all-languages images move to docs/guide/img/ after every run
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen"))   # tools/gen: sibling tools live in other folders
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from sim_con import SimCon, SimCrash

LANGS = {"en": "en", "zh-TW": "tw", "zh-CN": "cn"}
BOARDS = {"round": os.path.join(REPO, "sim", "build", "sim"), "square": os.path.join(REPO, "sim", "build-fp", "sim")}
NOW = "2026-11-16 02:08:35"   # UTC; the simulator zone is UTC+8, so the clocks read 10:08:35
NOWPLAY = ["vol 0", "track 3", "@80", "uipush now", "@0.05"]   # shot right after the page opens: the title marquee is at rest, fully readable
VIZT = os.environ.get("GUIDE_VIZT", "13.6")


NAMES = {   # invented album / track / artist names per language (no real artists); keys are the zh-TW originals of tools/simtools/sim_fixture.py
    "en": {"在那個沒有風也沒有雲的夏天午後我們一起走過長長的河堤並且約定明年再來": "Summer Afternoon", "夜航星圖": "Night Voyage Atlas", "白鷺合唱團": "Egrets", "紙飛機工廠": "Paper Plane Works", "小橘子樂團": "Orange", "山間小火車": "Mountain Little Train", "阿拓與風": "Ato",
           "港口的燈": "Harbour Light", "星圖": "Star Chart", "遲到的潮汐": "Late Tide", "紙飛機": "Paper Plane", "午睡": "Afternoon Nap", "さくらの風": "Cherry Breeze", "頭班車": "First Train",
           "隧道裡的口哨": "Whistling in the Tunnel", "月台上的熱茶": "Hot Tea on the Platform", "終點站": "Last Stop", "Sim FM 一號": "Lantern FM", "Sim 新聞台": "Harbour Talk", "Sim 古典": "Quiet Strings"},
    "zh-TW": {"白鷺合唱團": "白鷺團", "小橘子樂團": "小橘團", "阿拓與風": "阿拓", "在那個沒有風也沒有雲的夏天午後我們一起走過長長的河堤並且約定明年再來": "夏日午後", "さくらの風": "櫻花的風", "Sim FM 一號": "燈籠電台", "Sim 新聞台": "港口新聞", "Sim 古典": "靜夜弦樂"},
    "zh-CN": {"在那個沒有風也沒有雲的夏天午後我們一起走過長長的河堤並且約定明年再來": "夏日午后", "さくらの風": "樱花的风", "夜航星圖": "夜航星图", "白鷺合唱團": "白鹭团", "紙飛機工廠": "纸飞机工厂", "小橘子樂團": "小橘团", "山間小火車": "山间小火车", "阿拓與風": "阿拓", "港口的燈": "港口的灯", "星圖": "星图",
              "遲到的潮汐": "迟到的潮汐", "紙飛機": "纸飞机", "午睡": "午睡", "頭班車": "头班车", "隧道裡的口哨": "隧道里的口哨", "月台上的熱茶": "站台上的热茶", "終點站": "终点站",
              "Sim FM 一號": "灯笼电台", "Sim 新聞台": "港口新闻", "Sim 古典": "静夜弦乐"},
}
FIX = {}
ORIG = {}


def fixture(lang):
    """a fixture data root for the language (invented names, cfg lang set before boot), made once per run under a temp dir"""
    if lang in FIX: return FIX[lang]
    import tempfile, copy, sim_fixture as F
    ORIG.setdefault("a", (copy.deepcopy(F.ALBUMS), copy.deepcopy(F.RADIO))); F.ALBUMS, F.RADIO = copy.deepcopy(ORIG["a"][0]), copy.deepcopy(ORIG["a"][1])
    nm = NAMES[lang]; tr = lambda x: nm.get(x, x)
    F.ALBUMS = [(tr(a), tr(r), [(f, tr(t) if t else t, sec, d, c) for f, t, sec, d, c in items]) for a, r, items in F.ALBUMS]
    F.RADIO = [(tr(n), u) for n, u in F.RADIO]
    d = tempfile.mkdtemp(prefix="guide_fix_"); F.sys.argv = ["x", d]; F.main()
    ap = os.path.join(d, "_player", "config", "app.txt"); t = open(ap, encoding="utf-8").read().replace("lang=0", f"lang={ {'zh-TW': 0, 'zh-CN': 1, 'en': 2}[lang] }")
    open(ap, "w", encoding="utf-8").write(t + "tour_done=1\n")
    FIX[lang] = d; return d


def games(g, extra, mode=0, style=0, rule=0):
    return ["screen on", f"games open {g}", f"games mode {mode}", f"games style {style}", f"games rule {rule}", *extra]


# id: (boards, mode, commands, settle seconds). boards: "RF" both, "R" round only. mode: sim --mode (None = player).
S = {
    "langpick": ("R", None, ["lang tw", "uipush langpick"], 0.5),
    "tour-step": ("RF", None, ["screen on", "tour"], 0.8),
    "tour-step2": ("RF", None, ["screen on", "tour", "simflick up"], 0.8),
    "tour-step3": ("RF", None, ["screen on", "tour", "simflick up", "simflick down", "simflick up"], 0.8),
    "tour-step4": ("RF", None, ["screen on", "tour", "simflick up", "simflick down", "simflick up", "simflick left"], 0.8),
    "home": ("RF", None, ["screen on", "uipush menu"], 0.8),
    "page-bubble": ("RF", None, ["screen on", "uipush menu", "knob 1"], 0.3),
    "now-playing": ("RF", None, NOWPLAY, 1.0),
    "library": ("RF", None, ["uipush tracks0"], 1.0),
    "radio-list": ("RF", None, ["uipush radio"], 1.0),
    "bt-devices": ("RF", None, ["btui"], 1.0),
    "clock-face": ("RF", None, ["clock", "clockface classic"], 1.5),
    "alarm": ("R", None, ["alarm 7 30", "clock", "show alarm"], 1.5),
    "pomodoro": ("R", None, ["clock", "show pomo", "pomo"], 1.5),
    "cast-broadcast": ("R", None, ["btui"], 1.0, {"SIM_STATE": "bt=broadcast"}),
    "drink-habits": ("RF", None, ["screen on", "show habits", "habit reset", "habit set 4", "habit seed 0 ..###.#", "habit seed 1 #######", "habit tap 2", "habit sel 0"], 0.5),
    "toys-menu": ("RF", None, ["tools"], 1.0),
    "game-breakout": ("RF", None, games("breakout", ["arena hold 1", "arena tap 0 0", "arena step 23"], rule=0), 0.5),
    "game-safe": ("RF", None, games("safe", ["games count 0", "dial game pos 35", "dial tap 0 0", "dial game pos 67"]), 0.5),
    "game-tower": ("RF", None, games("tower", ["arena hold 1", "arena game gold 0", "arena tap 0 0", "arena game put 4", "arena game x 10", "arena tap 0 0", "arena step 4"]), 0.5),
    "game-spiro": ("RF", None, games("spiro", ["games colour 1", "arena hold 1", "arena game gold 0", "arena game new 2 3 0", "arena step 35"]), 0.5),
    "instrument-play": ("RF", "inst", ["inst page 1"], 1.0),
    "ambient-lava": ("RF", None, ["screen on", "tools ambient", "arena hold 1", "games mode 0", "games style 0", "games rule 0", "arena game bright 3", "arena game seed 3", "arena step 150", "arena game rare", "arena step 3"], 0.5),
    "noise-colors": ("RF", None, ["uipush colors"], 0.8),
    "breath": ("RF", None, ["screen on", "uipush breath", "arena hold 1", "games mode 0", "games style 0", "games rule 1", "arena step 90"], 0.5),
    "quick-settings": ("RF", None, [*NOWPLAY, "screen on", "@0.05", "simflick down"], 0.8),
    "settings": ("RF", None, ["setui"], 0.8),
    "ext-mode": ("RF", "ext", ["simusb mount"], 1.0),
    "cast-radio-mode": ("R", "cast", [], 2.0),
    "viz-bars": ("RF", None, ["screen on", "vol 0", "track 3", "viz bars", "@VIZT"], 1.0),
    "viz-ring": ("RF", None, ["screen on", "vol 0", "track 3", "viz ring", "@VIZT"], 1.0),
    "btaudio-page": ("RF", None, ["simbt mode 2", "simbt caps aac,sbc", "simbt connect", "btaudio open"], 1.5),
    "btaudio-info": ("RF", None, ["simbt mode 2", "simbt caps aac,sbc", "simbt connect", "btaudio open", "knob 8"], 1.5),
    "wired-page": ("RF", None, ["vol 0", "mp3ui", "simaudiorate 96000 1", "mp3 1", "wired open"], 1.5),
    "details-out-wired": ("RF", None, ["vol 0", "mp3ui", "simaudiorate 96000 1", "mp3 1", "uipush meta", "knob 8"], 1.5),
    "details-out-bt": ("RF", None, ["vol 0", "simbt mode 2", "simbt caps aac,sbc", "simbt connect", "mp3ui", "simaudiorate 96000 1", "mp3 1", "simsink 1", "uipush meta", "knob 8"], 1.5),
    "popup-card": ("RF", None, games("breakout", ["arena hold 1", "arena step 20", "simflick up"]), 0.8),
    # 2026-11-22 firmware: the FakePod now-playing styles and nine-grid, the settings groups, time setting, card diagnostics, the maze toy, the USB drop
    "now-info": ("F", None, ["cfgset now_fp 0", *NOWPLAY], 1.0),
    "now-disc": ("F", None, ["cfgset now_fp 2", *NOWPLAY], 1.0),
    "ninegrid-cover": ("F", None, [*NOWPLAY, "simtap 230 230"], 0.6),
    "ninegrid-viz": ("F", None, ["screen on", "vol 0", "track 3", "viz bars", "@VIZT", "simtap 230 230"], 0.6),
    "volume-pill": ("RF", None, [*NOWPLAY, "screen on", "@1", "knob 8"], 0.6, {"SIM_SHOT_TOP": "1"}),
    "settings-sound": ("RF", None, ["setui", "settings group sound"], 0.8),
    "settings-value": ("RF", None, ["setui", "settings key bright"], 0.8),
    "timeset": ("RF", None, ["setui", "settings key time"], 0.8),
    "cardtest": ("RF", None, ["simsd speed 350 800 400 200", "setui", "settings key cardtest", "cardtest raw", "@5", "cardtest tap file", "cardtest tap file", "@8", "knob 4"], 1.0),
    "cardtest-ladder": ("RF", None, ["simsd speed 350 800 400 200", "setui", "settings key cardtest", "cardtest raw", "@5", "cardtest tap file", "cardtest tap file", "@8", "knob 14"], 1.0),
    "game-maze-gravity": ("R", None, ["screen on", "games open knobtoy", "games mode 0", "games style 0", "games rule 2", "games count 0", "arena hold 1", "arena game gap 0 90", "arena tap 0 0", "arena step 40", "arena game theta 30", "arena step 25"], 0.5),
    "game-maze-tilt": ("F", None, ["screen on", "maze grav 0 0", "games open knobtoy", "games mode 0", "games style 0", "games rule 5", "games count 0", "arena hold 1", "arena tap 0 0", "maze grav 60 20", "arena step 40"], 0.5),
    "usb-lost": ("R", "ext", ["simusb mount", "simusb lost", "ext ask no"], 1.0),
    "clock-default": ("RF", None, ["cfgdel clockface", "clock"], 1.5),
}


def shoot(lang, board, sid, outdir, keep_going):
    boards, mode, cmds, wait = S[sid][:4]; env = S[sid][4] if len(S[sid]) > 4 else {}
    if board == "round" and "R" not in boards or board == "square" and "F" not in boards: return None
    name = sid + ("_" + board if boards == "RF" else "")
    path = os.path.join(outdir, lang, "img", name + ".png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    os.environ["SIM_FP"] = "1" if board == "square" else "0"
    os.environ["SIM_GUIDE"] = "1"                                    # sim-only: the guide's cover art and music-like spectrum signal (sim_cover.c, fake_audio_svc.c)
    old = {k: os.environ.get(k) for k in env}; os.environ.update(env)
    c = SimCon(binary=BOARDS[board], extra_args=["--seed", "1", "--time", NOW], mode=mode, strict=False, cfg_guard=False, src_root=fixture(lang))
    for k, v in old.items(): os.environ.pop(k, None) if v is None else os.environ.__setitem__(k, v)
    try:
        cmds = [x.replace("VIZT", VIZT) for x in cmds]
        for x in cmds: c.cmd(x, 1.0 if x != cmds[-1] else wait)
        c.shot(path)
    except SimCrash as e:
        print(f"MISSING {lang}/{name}: {str(e)[:200]}", flush=True)
        if not keep_going: raise
        return None
    finally:
        c.close()
    return path


def optimize(root):
    from PIL import Image
    n = 0
    for dp, _, fs in os.walk(root):
        for f in fs:
            if not f.endswith(".png"): continue
            p = os.path.join(dp, f); im = Image.open(p); im.load()
            tmp = p + ".tmp"; im.save(tmp, "PNG", optimize=True)
            if os.path.getsize(tmp) < os.path.getsize(p): os.replace(tmp, p); n += 1
            else: os.remove(tmp)
    print(f"optimised {n} PNGs")


# ---- gesture diagrams and hero images (compose step: needs the shots above) ----
LABELS = {  # id -> {lang: text}
    "gesture-flick-up": {"en": "Flick up: back / menu", "zh-TW": "快速上滑：返回／選單", "zh-CN": "快速上滑：返回／菜单"},
    "gesture-flick-down": {"en": "Flick down: quick settings", "zh-TW": "快速下滑：快速設定", "zh-CN": "快速下滑：快速设置"},
    "gesture-flick-lr": {"en": "Flick left / right: page", "zh-TW": "左右快滑：換頁", "zh-CN": "左右快滑：换页"},
    "gesture-drag": {"en": "Slow drag: scroll", "zh-TW": "慢速拖曳：捲動", "zh-CN": "慢速拖动：滚动"},
    "gesture-hold": {"en": "Hold 3 s: home", "zh-TW": "按住 3 秒：回首頁", "zh-CN": "按住 3 秒：回首页"},
}
SYSCJK = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
FONT = {"en": None, "zh-TW": "ui-tc.ttf", "zh-CN": "ui-sc.ttf"}


def label_font(lang, sz):
    """the font of a composed label: zh-CN uses the system Noto Sans CJK SC (the card's ui-sc.ttf is a subset: even lowercase letters and most labels would draw as boxes)"""
    from PIL import ImageFont
    if lang == "zh-CN" and os.path.exists(SYSCJK): return ImageFont.truetype(SYSCJK, sz, index=2)
    return ImageFont.truetype(os.path.join(REPO, "fonts", FONT[lang] or "ui-tc.ttf"), sz)


def arrow(d, p0, p1, col, w):
    import math
    d.line([p0, p1], fill=col, width=w)
    a = math.atan2(p1[1] - p0[1], p1[0] - p0[0]); L = w * 4
    for s in (-0.5, 0.5):
        d.line([p1, (p1[0] - L * math.cos(a + s), p1[1] - L * math.sin(a + s))], fill=col, width=w)


CARD = {  # the /sdcard tree of docs/guide/*/12-card.md: (indent, name, note per language)
    "en": ("Your content: music, photos ...", "everything the player makes (back up by copying it)", "settings: lang, setup_url ...", "station lists", "external-mode key layers", "effect presets", "Wi-Fi (never share)", "library font set (optional)", "white-noise scene pack", "machine files, safe to delete"),
    "zh-TW": ("你的內容：音樂、相片…", "播放器產生的一切（複製即備份）", "設定：lang、setup_url…", "電台清單", "外接模式按鍵層", "音效預設", "Wi-Fi（絕不外流）", "曲庫字型組（選用）", "白噪音場景包", "機器檔，可刪除（會重建）"),
    "zh-CN": ("你的内容：音乐、照片…", "播放器产生的一切（复制即备份）", "设置：lang、setup_url…", "电台列表", "外接模式按键层", "音效预设", "Wi-Fi（绝不外传）", "曲库字体组（可选）", "白噪音场景包", "机器文件，可删除（会重建）"),
}


def card_layout(out):
    from PIL import Image, ImageDraw, ImageFont
    rows = [(0, "/sdcard/", None), (1, "export/ ...", 0), (1, "_player/", 1), (2, "config/app.txt", 2), (2, "config/radio*.txt", 3), (2, "config/keys.txt", 4), (2, "config/sound/*.toml", 5),
            (2, "config/wifi.txt", 6), (2, "fonts/ui-*.ttf", 7), (2, "sleep/library.muk", 8), (2, "index, album, loops, logs", 9)]
    for lang in LANGS:
        f = label_font(lang, 22); fm = label_font(lang, 20)
        im = Image.new("RGB", (1100, 40 + 40 * len(rows)), (10, 12, 20)); d = ImageDraw.Draw(im)
        for i, (ind, name, note) in enumerate(rows):
            y = 20 + 40 * i; x = 30 + 40 * ind
            if ind: d.line([(x - 22, y + 4), (x - 22, y + 18), (x - 6, y + 18)], fill=(90, 100, 130), width=2)
            d.text((x, y), name, font=f, fill=(255, 190, 40) if name.startswith("_player") else (120, 220, 255))
            if note is not None: d.text((420, y + 1), CARD[lang][note], font=fm, fill=(200, 205, 220))
        im.save(os.path.join(out, lang, "img", "card-layout.png"))


def theme_gallery(out):
    """theme-gallery_<board>.png: the home page of the default look and the ten card themes on one image (names per language); icon-sheet.png: the menu icon map"""
    import tempfile, shutil, gen_card_themes as G, theme_sheets as T
    from PIL import Image, ImageDraw, ImageFont
    tmp = tempfile.mkdtemp(prefix="gal_"); names = ["default"] + list(G.THEMES); tiles = {}
    for lang in LANGS:
        for fp in (False, True):
            board = "square" if fp else "round"
            for n in names:
                T.run(n, fp, tmp, lang); tiles[(lang, board, n)] = Image.open(os.path.join(tmp, f"{n}_home.png")).convert("RGB")
    shutil.rmtree(tmp, ignore_errors=True)
    for lang in LANGS:
        fnt = lambda sz: label_font(lang, sz)
        for board in ("round", "square"):
            t0 = tiles[(lang, board, "default")]; tw = 230; th = tw; cols = 4; rows = (len(names) + cols - 1) // cols; lab = 30
            im = Image.new("RGB", (cols * tw, rows * (th + lab)), (10, 12, 20)); d = ImageDraw.Draw(im); f = fnt(18)
            for i, n in enumerate(names):
                x, y = (i % cols) * tw, (i // cols) * (th + lab); im.paste(tiles[(lang, board, n)].resize((tw - 8, th - 8)), (x + 4, y + 4))
                nm = {"en": "Default (Neon)", "zh-TW": "預設（霓虹）", "zh-CN": "默认（霓虹）"}[lang] if n == "default" else G.THEMES[n][{"en": 2, "zh-TW": 0, "zh-CN": 1}[lang]]
                d.text((x + tw // 2 - d.textlength(nm, font=f) / 2, y + th), nm, font=f, fill=(255, 255, 255))
            im.save(os.path.join(out, lang, "img", f"theme-gallery_{board}.png"))
        import make_icons as MI   # the menu icon map in this language: names from tools/gen/icon_names_l10n.tsv (cn / en), icon_names_zh.txt (tw)
        sd = tempfile.mkdtemp(prefix="icons_")
        MI.sheet(sd, os.path.join(REPO, "fonts", "ui-icon.ttf"), SYSCJK if lang == "zh-CN" and os.path.exists(SYSCJK) else os.path.join(REPO, "fonts", FONT[lang] or "ui-tc.ttf"), LANGS[lang])   # ui-sc.ttf is a subset: the sheet names would draw as boxes
        src = os.path.join(sd, "icon_menu_map.png" if LANGS[lang] == "tw" else f"icon_menu_map_{LANGS[lang]}.png")
        Image.open(src).convert("RGB").save(os.path.join(out, lang, "img", "icon-sheet.png")); shutil.rmtree(sd, ignore_errors=True)


def compose(out):
    card_layout(out)
    theme_gallery(out)
    from PIL import Image, ImageDraw, ImageFont, ImageFilter
    col = (255, 190, 40)
    for lang in LANGS:
        img = os.path.join(out, lang, "img")
        fp = FONT[lang]
        for board, base in (("round", {"gesture-flick-up": "clock-face", "gesture-flick-down": "ambient-lava", "gesture-flick-lr": "toys-menu", "gesture-drag": "library", "gesture-hold": "clock-face"}),
                            ("square", {"gesture-flick-up": "clock-face", "gesture-flick-down": "ambient-lava", "gesture-flick-lr": "toys-menu", "gesture-drag": "library", "gesture-hold": "clock-face"})):
            for gid, src in base.items():
                im = Image.open(os.path.join(img, f"{src}_{board}.png")).convert("RGB")
                W = im.width; im = Image.blend(im, Image.new("RGB", im.size, (0, 0, 0)), 0.35)
                d = ImageDraw.Draw(im, "RGBA"); w = max(4, W // 60); m = W // 2; k = W * 0.30
                if gid == "gesture-flick-up": arrow(d, (m, m + k), (m, m - k), col, w)
                elif gid == "gesture-flick-down": arrow(d, (m, m - k), (m, m + k), col, w)
                elif gid == "gesture-flick-lr": arrow(d, (m - W * .05, m), (m - k, m), col, w); arrow(d, (m + W * .05, m), (m + k, m), col, w)
                elif gid == "gesture-drag":
                    arrow(d, (m, m + k * .8), (m, m - k * .6), col, w)
                    for t in (0.3, 0.55, 0.8): d.ellipse([m - w, m + k * (0.8 - t * 1.4) - w, m + w, m + k * (0.8 - t * 1.4) + w], fill=col)
                else:
                    r = W * .13; d.ellipse([m - r, m - r, m + r, m + r], outline=(255, 255, 255, 120), width=w)
                    d.arc([m - r, m - r, m + r, m + r], -90, 200, fill=col, width=w + 2); d.ellipse([m - w * 2, m - w * 2, m + w * 2, m + w * 2], fill=col)
                f = label_font(lang, W // 16) if fp else ImageFont.load_default(W // 16)
                t = LABELS[gid][lang]; tw = d.textlength(t, font=f)
                strip = W // 8                                              # the caption sits in a strip under the picture: no UI text is covered
                canvas = Image.new("RGB", (W, W + strip), (10, 12, 20)); canvas.paste(im, (0, 0)); d = ImageDraw.Draw(canvas)
                d.text((m - tw / 2, W + (strip - W // 16) / 2 - 2), t, font=f, fill=(255, 255, 255)); im = canvas
                im.save(os.path.join(img, f"{gid}_{board}.png"))
        # hero: round + square side by side, dark background, 3-4 screens each
        pick = ("now-playing", "viz-bars", "game-breakout", "home")
        rs = [Image.open(os.path.join(img, f"{n}_round.png")).convert("RGB") for n in pick]
        ss = [Image.open(os.path.join(img, f"{n}_square.png")).convert("RGB") for n in pick]
        Wd, Hd = 1600, 900; hero = Image.new("RGB", (Wd, Hd), (10, 12, 20))
        def circ(x):
            m_ = Image.new("L", x.size, 0); ImageDraw.Draw(m_).ellipse([0, 0, x.width - 1, x.height - 1], fill=255); o = Image.new("RGB", x.size, (10, 12, 20)); o.paste(x, mask=m_); return o
        for i, x in enumerate(rs):
            x = circ(x).resize((340, 340)); hero.paste(x, (40 + (i % 2) * 360, 80 + (i // 2) * 380))
        for i, x in enumerate(ss):
            x = x.resize((340, 340)); hero.paste(x, (860 + (i % 2) * 360, 80 + (i // 2) * 380))
        hero.save(os.path.join(img, "hero-screens.png"))
        # hero-boards / hero-round / hero-fakepod stand-ins (the guide names them as photos: these are simulator renders, same cover)
        Image.open(os.path.join(img, "now-playing_round.png")).save(os.path.join(img, "hero-round.png"))
        Image.open(os.path.join(img, "now-playing_square.png")).save(os.path.join(img, "hero-fakepod.png"))


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--out", default=os.path.join(REPO, "docs", "guide"))
    a.add_argument("--lang", default=",".join(LANGS)); a.add_argument("--board", default="round,square")
    a.add_argument("--compose", action="store_true"); a.add_argument("--only"); a.add_argument("--keep-going", action="store_true"); a.add_argument("--optimize", action="store_true")
    o = a.parse_args()
    if o.compose:
        compose(o.out); (optimize(o.out) if o.optimize else None); share(o.out); return
    only = o.only.split(",") if o.only else list(S)
    got = 0
    for lang in o.lang.split(","):
        for board in o.board.split(","):
            for sid in only:
                if shoot(lang, board, sid, o.out, o.keep_going): got += 1
    print(f"{got} shots")
    if o.optimize: optimize(o.out)
    share(o.out)


if __name__ == "__main__":
    main()
