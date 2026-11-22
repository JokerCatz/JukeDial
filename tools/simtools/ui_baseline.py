#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Screenshot baseline of every screen (UI refactor safety net, 2026-11-11).
usage: ui_baseline.py PORT OUTDIR [--inst] [--ext] [--safe]     (PORT: by-id path of the main board, or "sim" for the desktop simulator)
On the simulator --inst adds a second run, `sim --mode inst`, whose pictures (inst_play*, inst_chips, inst_kits, inst_set*, inst_pads*, inst_metro*,
inst_loop*, inst_file_ring, inst_fx_*) go into the same OUTDIR; --ext one more, `sim --mode ext` (ext_media, ext_layers, ext_errors, ext_armed; simulator only: the board's USB mode has no console on its side of the plug); --safe one more, `sim --mode safe` (safe).
Opens each page with its console command, saves <name>.png (uses tools/simtools/shot.py grab), goes back to the main
menu (checked with uitop). --inst: instrument mode (the "inst" command reboots the board into it; leaves it again)."""
import argparse, glob, json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # tools/(root): sibling tools live in other folders
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cfg_keep

FP = os.environ.get("SIM_FP") == "1"          # the square FakePod simulator (make sim-fp): the ring is a grid, a left flick turns its page
NEXT_TAP = 'simflick left' if FP else None   # simulator only (sim_con): the bt_p2 step below sends it only when the console is the simulator's
FORT = ["kuji", "jiao", "dice", "wheel", "gacha", "tarot"]
FACES = ["classic", "minimal", "digital", "info", "sun", "rings", "moon", "hud"]
TOOLS = ["dice", "slot", "lunch", "people", "ideas"]
FX = ["pitch", "rainbow", "ripple", "comet", "spectrum", "off"]
TOP = re.compile(rb"APP uitop (\S+)")
BRIGHT = re.compile(rb"APP cfg brightness=(\S*)")
RGU = re.compile(rb"APP cfg rg_untag=(\S*)")
# pages whose commands exist only in the simulator (simtap, simknob, simnoise, simbt): the board run skips them, there is no board picture (sim/known_diffs.txt says so)
SIM_ONLY = {"tools_tuner_listen", "btaudio_aac_end", "wired_play", "btaudio_idle", "btaudio_aac", "wired_page", "meta_out_wired", "meta_out_bt", "tool_counter", "tool_counter8", "clock_drink", "clock_habits", "hud_cd_last10", "hud_sw_laps", "hud_pomo", "scene_custom", "scene_layer_ring", "scene_delete_armed", "pair", "status_wired", "bt_p2", "ratetest_tier", "ratetest_done",
            "settings_card", "thumbcache_page", "thumbcache_page_paused", "card_gpt", "cardfmt_warn", "cardfmt_code", "cardfmt_arm", "cardfmt_run", "cardfmt_done", "speaker_meta", "nowfp_a", "nowfp_a_paused", "nowfp_a_long", "nowfp_a_nocover", "nowfp_b", "nowfp_b_paused", "nowfp_b_long", "nowfp_b_nocover", "nowfp_b_hidden", "nowfp_a_sleep", "nowfp_a_noise", "nowfp_grid", "cardtest_idle", "cardtest_done", "cardtest_ladder"}   # cardfmt: simsd is a simulator command   # bt_p2: the board has no tap to turn the ring's page (final review M8); ratetest_*: simdac is a simulator command
# the settings the page steps write (clock faces, vol 0, the axis shots, the pages that record themselves as the last screen): read before the
# first page, put back after the last one and on error (board batch A F6; the simulator run fails when any setting is left changed, tools/simtools/sim_con.py)
KEEP = ["now_fp", "clockface", "swface", "cdface", "pomoface", "cd_set", "volume", "brightness", "alarm_on", "alarm_min", "alarm_days", "cast_q", "last_screen", "dac_max_rate", "lang"]
KEEP_INST = ["inst"]                 # the instrument's one settings line


def games_ids():
    """the game ids of app/main/ui/games.def, in file order (one GAME(id, ...) line each; comments are not read)"""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "app", "main", "ui", "games.def")
    text = re.sub(r"/\*.*?\*/", "", open(path, encoding="utf-8").read(), flags=re.S)
    return re.findall(r"^\s*GAME\(\s*(\w+)\s*,", text, flags=re.M)


class Con:
    def __init__(self, port):
        from boardport import open_port      # only the board path needs pyserial (boardport imports it when it opens)
        global shotmod
        import shot as shotmod
        self.s = open_port(port, drain_s=0.4, unopened=True)   # DTR/RTS untouched: no reset

    def drain(self, secs):
        end, out = time.time() + secs, b""
        while time.time() < end: out += self.s.read(4096)
        return out

    def cmd(self, c, wait=1.0):
        self.s.write((c + "\n").encode()); return self.drain(wait)

    def top(self):
        for _ in range(3):
            m = TOP.search(self.cmd("uitop", 0.6))
            if m: return m.group(1).decode()
        return "?"

    def home(self, root):
        for _ in range(10):
            if self.top() == root: return
            self.cmd("back", 0.8)
        print(f"WARN: could not get back to {root}, at {self.top()}", flush=True)

    failed = []                              # pages whose grab stayed incomplete after one retry

    def shot(self, path):
        if shotmod.grab(self.s, path): return
        print(f"WARN: incomplete grab of {path}, retrying", flush=True)
        if not shotmod.grab(self.s, path):
            Con.failed.append(os.path.basename(path)[:-4])


def pages(c, root, out, only=None, on_page=None):
    """on_page(name): called once per page, after the page is built and right before its picture is taken (tools/check/ui_boxes_check.py hangs its `uiboxes clip` there).
    The settings in KEEP are put back at the end, also when a step fails."""
    keeping(c, KEEP, lambda: page_steps(c, root, out, only, on_page))


def keeping(c, keys, steps):
    """steps() with the settings `keys` read first and put back after it, also when it fails (tools/simtools/cfg_keep.py)"""
    send = lambda x: c.cmd(x, 0 if hasattr(c, "page_start") else 0.6)   # the simulator answers at once (no virtual time passes: the pictures stay as they were)
    saved = cfg_keep.read(send, keys)
    try:
        steps()
    except BaseException:
        try: cfg_keep.restore(send, saved)
        except Exception as e: print(f"WARN: settings not put back: {str(e).splitlines()[0]}", flush=True)   # the step's own error is the one reported
        raise
    cfg_keep.restore(send, saved)


def page_steps(c, root, out, only, on_page):
    def snap(name, cmds, wait=1.2):
        if only is not None and name not in only: return
        if name in SIM_ONLY and not hasattr(c, "page_start"): return   # the board console has none of these commands
        if hasattr(c, "page_start"): c.page_start(name)     # the simulator may set the clock before the page is built
        for x in cmds: c.cmd(x, 1.0 if x != cmds[-1] else wait)
        if on_page: on_page(name)
        c.shot(os.path.join(out, name + ".png"))
        c.home(root)

    def shoot(name):                    # a picture of a page that is already open (the playing ring, the album ring)
        if only is not None and name not in only: return
        if on_page: on_page(name)
        c.shot(os.path.join(out, name + ".png"))
    for name, cmd in [("menu", "uipush menu"), ("mp3", "mp3ui"), ("tracks_all", "list"), ("tracks0", "uipush tracks0"),
                      ("settings", "setui"), ("favs", "favui"), ("radio", "uipush radio"), ("bt", "btui"),
                      ("noise", "noiseui"), ("scene", "sceneui 0"), ("colors", "uipush colors"), ("chip", "chipui"),
                      ("explain_settings", "uipush help"), ("meta", "uipush meta"), ("message", "uipush message"), ("tools", "tools")]:
        snap(name, [cmd])
    for t in TOOLS: snap(f"tools_{t}", [f"tools {t}"], 1.5)
    snap("fortune", ["uipush fortune"])
    for f in FORT: snap(f"fortune_{f}", ["uipush fortune", f"fortune {f}"], 1.5)
    snap("vhome", ["vocab"]); snap("vdecks", ["vocab decks"]); snap("vquiz", ["vocab start"], 1.5)
    snap("clock", ["clock"], 1.5)
    for f in FACES: snap(f"clockface_{f}", ["clock", f"clockface {f}"], 1.5)
    c.cmd("cfgdel clockface", 0.5)                               # the default face for the clock pages below; the user's face comes back at the end
    for w in ["clockmenu", "alarm", "pomo", "help"]: snap(f"show_{w}", ["clock", f"show {w}"], 1.5)
    # HUD task (2026-11-18): the stopwatch and the countdown (default faces), their HUD faces, the pomodoro's HUD, the face axis cell of a ring
    # `screen on` first: the idle timeout turns the (simulated) backlight off after enough virtual time since the last wake and `clock` / `show` / `timers` do not wake it:
    # these five pictures were all-black / bare background in the baselines (found 2026-11-18; flat_shots now fails such a run)
    for w in ["stopwatch", "countdown"]: snap(f"show_{w}", ["screen on", "clock", f"show {w}"], 1.5)
    snap("hud_cd_last10", ["screen on", "clock", "show countdown", "timers face hud", "timers cd set 30", "timers cd start", "timers cd jump 9"], 0.6)
    c.cmd("timers cd reset", 0.3)
    snap("hud_sw_laps", ["screen on", "clock", "show stopwatch", "timers face hud", "timers sw start", "@1.3", "timers sw lap", "@2.1", "timers sw lap", "@0.9", "timers sw lap"], 0.7)
    c.cmd("timers sw reset", 0.3)
    snap("hud_pomo", ["screen on", "cfgset pomoface hud", "clock", "show pomo", "pomo"], 1.2)
    c.cmd("pomo stop", 0.3)
    snap("pomo_face_ring", ["cfgset pomoface rings", "clock", "show pomo", "popring -2", "popring 0"], 1.2)
    snap("stopwatch_ring", ["cfgdel swface", "clock", "show stopwatch", "popring -2"], 1.2)
    snap("countdown_ring", ["cfgdel cdface", "clock", "show countdown", "popring -2"], 1.2)
    for k in ["swface", "cdface", "pomoface"]: c.cmd(f"cfgdel {k}", 0.3)
    # now playing: silent track, paused, menu open
    for x in ["vol 0", "track 1"]: c.cmd(x, 1.0)
    c.drain(3)
    c.cmd("nowmenu -2", 1.0); c.cmd("nowmenu -1", 1.0)         # open the menu, the centre = pause
    shoot("now_paused")
    c.cmd("nowmenu -2", 1.0)
    shoot("now_menu")
    # the board's grab of now_menu outlasts the ring's 4 s timeout (board batch A F5): open the ring again when it closed, right before its second page
    if b"APP now menu=1" not in c.cmd("nowmenu -5", 0 if hasattr(c, "page_start") else 0.6): c.cmd("nowmenu -2", 1.0)
    c.cmd("nowmenu -4", 1.0)                                    # the playing ring's second page (UI unify T3)
    shoot("now_menu_p2")
    c.cmd("nowmenu -1", 0.5); c.cmd("stop", 1.0)
    c.home(root)
    if FP:                                                     # the square now-playing page (now_fp.c, 2026-11-20): style A cover + buttons, B full cover; playing, paused, a long title, no cover (track 11), the grid style
        for st, nm, play in ((0, "a", "simtap 384 160"), (1, "b", "simtap 230 246")):
            c.cmd(f"cfgset now_fp {st}", 0.3)
            snap(f"nowfp_{nm}", ["vol 0", "track 1", "@2"]); c.cmd("stop", 0.5)
            snap(f"nowfp_{nm}_paused", ["vol 0", "track 1", "@2", play, "@1"]); c.cmd("stop", 0.5)
            snap(f"nowfp_{nm}_long", ["vol 0", "track 3", "@2"]); c.cmd("stop", 0.5)
            snap(f"nowfp_{nm}_nocover", ["vol 0", "track 11", "@2"]); c.cmd("stop", 0.5)
        c.cmd("cfgset now_fp 1", 0.3)                           # style B after 4 s without a touch: the bare cover (the overlay hides itself)
        snap("nowfp_b_hidden", ["vol 0", "track 1", "@6"]); c.cmd("stop", 0.5)
        c.cmd("cfgset now_fp 0", 0.3)                           # style A with a sleep timer (the remaining time at the end of the format line) and a source without previous (noise: the column is centred)
        snap("nowfp_a_sleep", ["vol 0", "sleep 5", "track 1", "@2"]); c.cmd("sleep 0", 0.3); c.cmd("stop", 0.5)
        snap("nowfp_a_noise", ["vol 0", "color 2", "@2"]); c.cmd("stop", 0.5)
        c.cmd("cfgset now_fp 2", 0.3)
        snap("nowfp_grid", ["vol 0", "track 1", "@2"]); c.cmd("stop", 0.5)
        c.cmd("cfgdel now_fp", 0.3)
    snap("album_ring", ["album", "albummenu -2"])                # the album ring (UI unify T3)
    # the page kit's test pages (UI unify T4; console.c): the controls of every piece, on five pages because the slots overlap
    for k in ["kit", "kit2", "kit3", "kit4", "kit5", "kit7"]: snap(k, [f"uipush {k}"])
    # fx apps T7: the main menu's second page and the effects pictures (fx_steps: the card's sound is read first and put back after)
    snap("menu_p2", ["uipush menu", "screen on", "knob 1"] + ([] if FP else ["simtap 180 180"] if hasattr(c, "page_start") else []))   # the tap opens the page the knob selected (simulator only: the board console has no tap, its picture stays page 1)
    fx_steps(c, snap, only)
    # fx apps T14: after the clock pages (sim/diff_times.txt is fitted to the order of the pages before them): the Wi-Fi and radio help pages, the radio list at its end row
    snap("explain_wifi", ["help open wifi"]); snap("explain_radio", ["help open radio"])
    # fx apps T15: the help pages of the slot pay list, the Bluetooth ring, the alarm list and the pomodoro ring
    snap("explain_slot", ["help open slot"]); snap("explain_bt", ["help open bt"]); snap("explain_alarm", ["help open alarm"]); snap("explain_pomo", ["help open pomo"])
    snap("radio_end", ["uipush radio", "screen on", "knob 200"], 1.0)   # the radio list scrolled to its end row 在電腦上編輯電台 (screen on first: the first knob event only wakes a dark screen — Batch D board run; knob 200 reaches the end of a long card list; 1 s settle)
    # fx apps T13: the untagged-level row borrowed (tap +1, knob -1) and the reboot row armed; the value is given back after the shots
    m = RGU.search(c.cmd("cfg rg_untag", 0.6))
    snap("settings_dacmax_axis", ["wired open", "wired key dacmax", "knob 1"])   # hires T12: the wired-max-rate row now has three values: the borrowed knob (tap = 96k, knob = 192k)
    if m: c.cmd(f"seti rg_untag {m.group(1).decode()}" if m.group(1) else "cfgdel rg_untag", 0.5)
    snap("settings_reboot_armed", ["setui", "knob 6", "settings key reboot"])   # knob 10 since the 取樣率測試 row (hi-res Batch C merge), 11 since the 封面快取 row (hires T8e): the armed row peeks in at the bottom as before
    # the axis rows of UI unify T5: the borrowed row in the selection colour; each knob -1 gives the value back (nothing of the user's changes)
    m = BRIGHT.search(c.cmd("cfg brightness", 0.6))                 # the value is given back after the shot (also 100: the tap wraps to 10 and the knob stops there)
    snap("settings_bright_axis", ["setui", "knob 1", "knob 2", "settings key bright", "knob -1"])   # the knobs scroll the brightness row into view first (the first one may only wake the screen)
    if m: c.cmd(f"seti brightness {m.group(1).decode()}" if m.group(1) else "cfgdel brightness", 0.5)
    snap("alarm_hour_axis", ["clock", "show alarm", "alarm tap 1", "knob -1"], 1.5)
    snap("bt_p2", ["btui"] + ([NEXT_TAP or "simtap 279 279"] if hasattr(c, "page_start") else []))   # fx apps T15: the Bluetooth ring's second page (現況, 清除配對, 說明 since the hi-res merge); simulator only (SIM_ONLY: the board run skips it)
    # UI unify T6: the scene editor with four layers, the first layer's ring, and the delete row armed (the knob scrolls it into view; positions read from `uiboxes`)
    snap("scene_custom", ["simnoise 30 4 custom", "sceneui 0"])
    snap("scene_layer_ring", ["simnoise 30 3 custom", "sceneui 0", "simtap 180 98"])
    snap("pair", ["simbt pair 123456", "uipush pair"])          # UI unify T7: the pairing ring with the digits (a pending prompt of the fake)
    snap("pomo_ring", ["clock", "show pomo", "popring -2"], 1.5)   # UI unify T7: the pomodoro's ring while idle (centre 開始, no stop cell)
    snap("scene_delete_armed", ["simnoise 30 3 custom", "sceneui 0", "simknob 9", "simtap 180 266"])
    snap("status_idle", ["help open status"])                    # hires T7: the 現況 page, nothing playing
    snap("status_wired", ["seti dac_max_rate 96000", "simaudiorate 96000 1", "mp3ui", "mp3 1", "help open status"], 1.5)   # a 96k MP3 on the wired output (the fake's source), 96 kHz native
    c.cmd("stop", 0.5)
    snap("ratetest_tier", ["simdac 96000", "setui", "settings key ratetest"], 1.0)           # hires T11: the rate test's page in tier 1 (the fake listener has not tapped yet)
    snap("ratetest_done", ["simdac 96000", "setui", "settings key ratetest"], 4.5)           # the fake listener confirms both tiers (the build's top is 96k): the result note and 重新測試
    if hasattr(c, "page_start"): c.cmd("simdac off", 0.2); c.cmd("seti dac_max_rate 96000", 0.2)   # simulator only: the test stored its result; put back what status_wired left
    # cardfmt: the settings list with the 記憶卡 row, the card page, the flow's warning, code pad (3 of 10), final row, progress and 完成, for a GPT card of the
    # fake SD layer (simsd: simulator only); the fake's format is not a real one (its 1.5 s run, then mounted). The card is put back to mounted after them.
    to_card = ["screen on", "simsd fail gpt", "setui", "settings key card"]   # screen on: a screen that goes dark mid-flow sends the flow back to the warning
    konami = [f"codepad tap {k}" for k in "uuddlrlrba"]
    snap("settings_card", ["screen on", "simsd fail gpt", "setui"])
    snap("card_gpt", to_card)
    snap("cardfmt_warn", to_card + ["cardfmt row"])
    snap("cardfmt_code", to_card + ["cardfmt row", "cardfmt continue", "codepad tap u", "codepad tap u", "codepad tap d"])
    snap("cardfmt_arm", to_card + ["cardfmt row", "cardfmt continue"] + konami)
    snap("cardfmt_run", to_card + ["cardfmt row", "cardfmt continue"] + konami + ["cardfmt arm", "cardfmt arm"], 0.5)
    snap("cardfmt_done", to_card + ["cardfmt row", "cardfmt continue"] + konami + ["cardfmt arm", "cardfmt arm"], 2.5)
    if hasattr(c, "page_start"): c.cmd("simsd ok", 0.2)
    # 記憶卡診斷 (cardtest): the page with no result, then after a raw read and a file test on the fake card (simsd speed: raw 3.5 / file read-back 2.0 MB/s), and its ladder scrolled to the end; simulator only
    cardtest_open = ["simsd speed 350 800 400 200", "setui", "settings key cardtest"]
    snap("cardtest_idle", cardtest_open)
    snap("cardtest_done", cardtest_open + ["cardtest raw", "@5", "cardtest tap file", "cardtest tap file", "@8", "knob 6"], 1.0)
    snap("cardtest_ladder", cardtest_open + ["cardtest raw", "@5", "cardtest tap file", "cardtest tap file", "@8", "knob 80"], 1.0)
    if hasattr(c, "page_start"): c.cmd("simsd speed 350 800 400 380", 0.2)
    # AVRCP 1.6 step 1: speaker mode's playing page with the phone's track (the fake phone: simbtmeta; the fake cover is drawn from the file's bytes), simulator only
    snap("speaker_meta", ["vol 0", "simbt stream", "simbtmeta title 夜航星圖", "simbtmeta artist 紙飛機工廠", "simbtmeta album 山間小火車", "simbtmeta art /sdcard/_player/config/radio.txt", "uipush now", "rot 0 50"], 1.5)
    for x in (["simbt stop", "simbtmeta clear", "rot 3 50"] if hasattr(c, "page_start") else []): c.cmd(x, 0.2)   # the stream, the track and the turn as they were (rot 3 50: the page's default)
    # sfx task 2: the 音效庫 page (the sound bank's list, top of it; no random draw)
    snap("sfx_bank", ["tools sfxbank"], 0.8)
    # BTAUDIO: the Bluetooth audio page (headset not connected / an AAC-only headset: the actual is capped and says why), the Wired output page, and the details page's 輸出 block (wired, Bluetooth);
    # simulator only (the fake headset and the fake audio source are console-driven); the settings are given back after the shots
    snap("btaudio_idle", ["simbt mode 2", "btaudio open"], 0.5)
    snap("btaudio_aac", ["simbt mode 2", "simbt caps aac,sbc", "simbt connect", "btaudio open"], 1.5)
    snap("btaudio_aac_end", ["simbt mode 2", "simbt caps aac,sbc", "simbt connect", "btaudio open", "knob 8"], 1.5)   # the list scrolled to its info lines: the headset's best and the actual
    snap("wired_page", ["wired open"], 0.5)
    snap("wired_play", ["vol 0", "mp3ui", "simaudiorate 96000 1", "mp3 1", "wired open"], 1.5)   # the live line of a 96k file at the round board's 48k ceiling: halved, with its reason
    snap("meta_out_wired", ["vol 0", "mp3ui", "simaudiorate 96000 1", "mp3 1", "uipush meta", "knob 8"], 1.5)
    snap("meta_out_bt", ["vol 0", "simbt mode 2", "simbt caps aac,sbc", "simbt connect", "mp3ui", "simaudiorate 96000 1", "mp3 1", "simsink 1", "uipush meta", "knob 8"], 1.5)
    for x in (["simsink 0", "stop", "simbt mode 0", "simbt caps ldac,aptxhd,aptx,aac,sbc"] if hasattr(c, "page_start") else []): c.cmd(x, 0.3)
    # ideas G2: 多組計數器 (4 and 8 counters; each sets its own count, names and style and zeroes first: nothing depends on what games.txt held)
    snap("tool_counter", ["screen on", "tools counter", "counter set 4 0 0", "counter reset", "counter tap 1", "counter tap 1", "counter tap 1", "counter tap 2", "counter tap 3", "counter tap 3", "counter tap 3", "counter tap 3", "counter sel 1"], 0.5)
    snap("tool_counter8", ["screen on", "tools counter", "counter set 8 3 0", "counter reset"] + [f"counter tap {i}" for i in (0, 1, 1, 2, 2, 2, 5, 5, 5, 5, 7)] + ["counter sel 2"], 0.5)
    # ideas G7: 喝水提醒 and 習慣打卡 (the console sets everything, the picture does not depend on what games.txt held; the sim sets the clock before the page)
    snap("clock_drink", ["screen on", "show drink", "drink set 1 2 9 21 8 3", "drink tick"], 0.5)
    snap("clock_habits", ["screen on", "show habits", "habit reset", "habit set 4", "habit seed 0 ..###.#", "habit seed 1 #######", "habit tap 2", "habit sel 0"], 0.5)
    # ideas G5 氛圍頁: the field's own steps held BEFORE the scene is set (no virtual-time phase), each scene seeded and stepped a fixed number of times, the default brightness and tone
    AMB = ["screen on", "tools ambient", "arena hold 1", "games mode 0", "games style 0"]
    snap("tool_ambient_lava", AMB + ["games rule 0", "arena game bright 3", "arena game seed 3", "arena step 150", "arena game rare", "arena step 3"], 0.5)
    snap("tool_ambient_fish", AMB + ["games rule 1", "arena game bright 3", "arena game seed 3", "arena step 150", "arena game poke 30 -20", "arena step 6"], 0.5)
    snap("tool_ambient_fire", AMB + ["games rule 2", "arena game bright 3", "arena game seed 3", "arena step 150"], 0.5)
    snap("tool_ambient_night", AMB + ["games rule 3", "arena game bright 3", "arena game seed 3", "arena step 120"], 0.5)
    # ideas G7 呼吸教練: the field's own steps held BEFORE the pattern is set, the box pattern stepped a fixed number of times (no virtual-time phase): late in the inhale, then well into the exhale
    BR = ["screen on", "uipush breath", "arena hold 1", "games mode 0", "games style 0", "games rule 1"]
    snap("noise_breath_in", BR + ["arena step 90"], 0.5)
    snap("noise_breath_out", BR + ["arena step 330"], 0.5)
    # bubble task: the last player page, because opening a sheet draws from the shared generator (ui_rng) and every random page after it (wheel, quiz, clock faces) would move
    snap("tools_bubbles", ["tools bubbles"], 1.5)
    snap("tools_torch", ["tools torch"], 0.5)   # the flashlight: pure white (after the clock pages: every page costs virtual time and the second hands move)
    snap("tools_tuner", ["tools tuner"], 0.5)   # the tuner's list; the two pages and the voice list under it (the listen page's picture needs the simulator's synthetic microphone: SIM_ONLY)
    snap("tools_tuner_listen", ["tools tuner", "tuner listen", "simtone 440 -12"], 1.6)
    snap("tools_tuner_tone", ["tools tuner", "tuner tone", "tuner note 52"], 0.5)
    snap("tools_tuner_voice", ["tools tuner", "tuner tone", "tuner wave 3", "tuner voice"], 0.5)
    # games G0: the hub and one picture per line of app/main/ui/games.def (a new game gets its picture without a line here: the first move on a fresh board, the computer's answer in it)
    if hasattr(c, "page_start"): c.cmd("gamesave clear", 0.2)   # G2: the simulator's bests start empty whatever an earlier run saved (the board run keeps the card's, which `gamesave clear` would wipe: the result card's best line of game_result may differ there: known_diffs)
    snap("games_hub", ["games"], 1.5)
    # G3 節拍點擊: its picture is the metronome mode, the field's own steps held, 46 fixed steps after the start (rings in flight); the others: the first tap on the beat
    DOM = ["screen on", "games open dominoes", "games mode 0", "games style 0", "games rule 0", "games dom deal 1", "games dom gold -1"]   # G4: the deal 1 of the page's own seed (the device opened 6|6, your turn); the device's timer is held
    GAME_PIC_CMDS = {"dominoes": DOM + ["games dom hold"], "beat": ["screen on", "games open beat", "games mode 0", "games style 0", "games rule 1", "arena hold 1", "arena tap 0 0", "arena step 46"]}
    GAME_PIC_CMDS["puzzle15"] = ["screen on", "games open puzzle15", "games mode 0", "games style 0", "games rule 0", "games count 0"]   # G4 15 拼圖: the first shuffle (count 0), nothing moved
    SPIRO = ["screen on", "games open spiro", "games mode 0", "games style 0", "games rule 0", "games colour 1", "arena hold 1", "arena game gold 0", "arena game new 2 3 0"]   # G5 萬花尺: the wheels 8:3, pen 100 %, hypo, set at once (the page's own seed moves with every open), the field's own steps held
    GAME_PIC_CMDS["spiro"] = SPIRO + ["arena step 35"]   # mid-draw: 175 of 360 segments (the first picture of the run opens the page: nothing else moves it)
    # G6 旋轉擋板: held, started (no own shots), the shield turned to 300 with a combo of 4 (wider), three shots in flight (one bounced out, one golden): put by console, so no step's phase is in it; the square layout the same on the square guard line
    DEFL = ["screen on", "games open deflect", "games mode 0", "games style 0", "games rule 0", "arena hold 1", "arena game spawn 0", "arena tap 0 0", "arena game deg 300", "arena game combo 4",
            "arena game shot 0 -60 -55 0.5 0.5", "arena game shot 1 70 20 0 0 back", "arena game shot 2 -20 80 0 0 gold", "arena game shot 3 30 -80 0 0"]
    GAME_PIC_CMDS["deflect"] = DEFL
    snap("game_deflect_square", DEFL + ["arena game shape 1", "arena tap 0 0", "arena game deg 300", "arena game combo 4", "arena game shot 0 -60 -55 0.5 0.5", "arena game shot 1 70 20 0 0 back",
                                        "arena game shot 2 -20 80 0 0 gold", "arena game shot 3 30 -80 0 0"], 0.5)
    LIFE = ["screen on", "games open life", "games mode 0", "games style 0", "games rule 0", "arena hold 1", "games edge 0", "games glow 0", "games pat 1"]   # G6 生命遊戲: the field's own steps held, generations by console (`arena game gen`), the soup from a fixed generator state
    GAME_PIC_CMDS["life"] = LIFE + ["games cell 2", "arena game seed 25 12345", "arena game gen 40"]   # 3 px random after 40 generations
    for g in games_ids(): snap(f"game_{g}", GAME_PIC_CMDS.get(g, [f"games open {g}", "board tap 4"]), 2.5 if g not in GAME_PIC_CMDS else 0.5)
    # G0b game feel (names in GAME_FEEL_PICS): a rule variant of each game, the result card, every other style of 井字棋; each sets its rule and style
    # (they are kept while the device is on, so the later ones never depend on the earlier) and plays two players, so the device's choices do not enter the picture
    snap("game_ttt_vanish", ["games open ttt", "games mode 2", "games style 0", "games rule 1", "board tap 0", "board tap 8", "board tap 1", "board tap 7", "board tap 5", "board tap 3"], 1.0)
    snap("game_c4_wrap", ["screen on", "games open c4", "games mode 2", "games style 0", "games rule 1"] + [f"board tap {c}" for c in (5, 3, 6, 3, 0, 3, 1)], 0.45)   # the line and the glow done, the card not yet ("screen on" first, G1: console moves are not input, so with more pictures before it the idle timeout fell inside the 0.45 s and the card came at once)
    snap("game_result", ["games open ttt", "games mode 2", "games style 0", "games rule 0", "board tap 0", "board tap 3", "board tap 1", "board tap 4", "board tap 2"], 1.2)
    snap("game_simon_play", ["games open simon", "games mode 0", "games style 0", "games rule 0", "games count 0", "sectors hub"], 0.9)   # G1 (GAME_FEEL_PICS): 四色記憶, the first colour of round 1 lit (count 0: the same game whatever ran before)
    # G1 環形打磚塊 (the ui_arena piece): the field's own steps held, a serve, then 23 fixed steps (`arena step`): the first brick broken 4 steps ago, its burst in flight
    snap("game_breakout_play", ["screen on", "games open breakout", "games mode 0", "games style 0", "games rule 0", "arena hold 1", "arena tap 0 0", "arena step 23"], 0.5)
    # G2 保險箱開鎖 (the ui_dial piece): the safe opened (count 0 = the first combination 35, 67, 84; each number put on the dial and confirmed), the door ring and the card up
    snap("game_spiro_done", SPIRO + ["games rule 1", "arena game finish", "arena step 20"], 0.5)   # G5: the three layers of 8:3 finished, the end effects over
    snap("game_life_gun", LIFE + ["games cell 1", "games glow 2", "arena game clear", "arena game place 2 60 60 0", "arena game gen 90"], 0.5)   # G6: 2 px, the Gosper gun after 90 generations (three gliders out), a 3-generation afterglow
    snap("game_safe_open", ["screen on", "games open safe", "games mode 0", "games style 0", "games rule 0", "games count 0", "dial game pos 35", "dial tap 0 0", "dial game pos 67",
                            "dial tap 0 0", "dial game pos 84", "dial tap 0 0"], 1.2)
    snap("game_beat_hit", ["screen on", "games open beat", "games mode 0", "games style 0", "games rule 1", "arena hold 1", "arena tap 0 0", "arena step 46", "arena tap 0 0", "arena step 20", "arena tap 0 0", "arena step 3"], 0.5)   # G3: two perfect hits on the beat, their bursts in flight
    # G3 疊塔 (the ui_arena field): a few floors (count seeded by the simulator's fixed boot: 4 perfect, one cut at x 10 with its piece falling), and after a miss the card up
    # G4 骨牌 (the ui_cards piece): the chain of nine tiles in two snaking rows after both sides played by the device's rules, a playable tile of your hand lifted and the chain's ends ringed
    snap("game_dominoes_mid", DOM + ["games dom auto 9", "games dom hold", "games dom hand 1"], 0.5)
    snap("game_tower_play", ["screen on", "games open tower", "games mode 0", "games style 0", "games rule 0", "arena hold 1", "arena game gold 0", "arena tap 0 0", "arena game put 4",
                             "arena game x 10", "arena tap 0 0", "arena step 4"], 0.5)
    snap("game_tower_over", ["screen on", "games open tower", "games mode 0", "games style 0", "games rule 0", "arena hold 1", "arena game gold 0", "arena tap 0 0", "arena game put 5",
                             "arena game x 70", "arena tap 0 0"], 1.2)
    # G3 電台調頻尋寶 (ui_dial with the proximity cue): count 0 = stations 46, 71, 87; the dial near the first (the glow lit), and one station locked (the mark on the rim)
    snap("game_radio_lock", ["screen on", "games open radio", "games mode 0", "games style 0", "games rule 0", "games count 0", "dial game on 0", "dial tap 0 0", "dial game pos 66"], 0.5)
    # G6 旋鈕小玩意 (ui_dial, rule = the toy): a = the wind-up wound to 17 of a sweet spot at 20 after one release (the last spot on the rim, the glow lit); b = fishing mid-catch (the zone on the rim, a tight line); c = the card of three releases
    snap("game_knobtoy_a", ["screen on", "games open knobtoy", "games mode 0", "games style 0", "games rule 0", "games count 0", "dial hold 1", "dial game sweet 20", "dial game set 20", "dial tap 0 0", "dial game set 17"], 0.5)
    snap("game_knobtoy_b", ["screen on", "games open knobtoy", "games mode 0", "games style 0", "games rule 1", "games count 0", "dial hold 1", "dial tap 60 0", "dial game fish 40", "dial game set 52", "dial game line 70", "dial game catch 420"], 0.5)
    snap("game_knobtoy_c", ["screen on", "games open knobtoy", "games mode 0", "games style 0", "games rule 0", "games count 0", "dial hold 1"] + ["dial game sweet 20", "dial game set 18", "dial tap 0 0"] * 3, 1.2)
    # G6b 旋轉迷宮 (the maze rules of the same page, ui_arena; pictures by console steps): the gravity maze with the marble in the second channel and gravity turned (the rim marker), the tilt maze
    # (square grid, holes, goal, tilt marker), the attract maze at its start
    snap("game_knobtoy_maze", ["screen on", "games open knobtoy", "games mode 0", "games style 0", "games rule 2", "games count 0", "arena hold 1", "arena game gap 0 90", "arena tap 0 0", "arena step 40", "arena game theta 30", "arena step 25"], 0.5)
    snap("game_knobtoy_maze_b", ["screen on", "maze grav 0 0", "games open knobtoy", "games mode 0", "games style 0", "games rule 5", "games count 0", "arena hold 1", "arena tap 0 0", "maze grav 60 20", "arena step 40"], 0.5)
    snap("game_knobtoy_maze_c", ["screen on", "games open knobtoy", "games mode 1", "games style 1", "games rule 4", "games count 0", "arena hold 1", "arena tap 0 0", "arena step 15"], 0.5)
    # G4 15 拼圖 (ui_board tiles): solved with the card up (almost: one slide left, tapped), and the other two styles on the first shuffle (letters; gradient on 3x3)
    snap("game_puzzle15_solved", ["screen on", "games open puzzle15", "games mode 0", "games style 0", "games rule 0", "games almost 40", "board tap 15"], 1.2)
    snap("game_puzzle15_style2", ["screen on", "games open puzzle15", "games mode 0", "games style 1", "games rule 0", "games count 0"], 0.5)
    snap("game_puzzle15_style3", ["screen on", "games open puzzle15", "games mode 1", "games style 2", "games rule 2", "games count 0"], 0.5)
    # G4 猜歌 (ui_sectors with a clip from the fake player): the question (count 0, no golden, the clip playing 0.5 s), and the card after a 5-question game answered right (the song put back)
    snap("game_guess_ask", ["screen on", "games open guess", "games mode 1", "games style 0", "games rule 0", "games count 0", "games golden 0", "sectors hub"], 0.5)
    snap("game_guess_result", ["screen on", "games open guess", "games mode 0", "games style 0", "games rule 0", "games count 0", "games golden 0", "sectors hub"] + ["games right", "@1.5"] * 5, 1.2)
    # G4b the shared exit: the 返回 pill over the bubble sheet (held: no 4 s timer), the pause overlay of 環形打磚塊 in flight and of 保險箱開鎖 (both mode C, the bubble under the scrim)
    snap("exit_bubbles", ["screen on", "tools bubbles", "exit show"], 0.5)
    snap("exit_pause_breakout", ["screen on", "games open breakout", "games mode 0", "games style 0", "games rule 0", "arena hold 1", "arena tap 0 0", "arena step 23", "exit pause"], 0.5)
    snap("exit_pause_safe", ["screen on", "games open safe", "games mode 0", "games style 0", "games rule 0", "games count 0", "exit pause"], 0.5)
    # hires T8e (last: the virtual time they take moves no other page's timer phase): the 封面快取 page while the pass builds (the fake pack filled to exactly 4 of 11 and held (simthumbpack fill 4: no virtual-time phase in the count)) and paused by its 暫停; simulator only (simthumbpack)
    snap("thumbcache_page", ["simthumbpack fill 4", "covers open"], 0.8)
    snap("thumbcache_page_paused", ["covers open", "covers tap pause"], 0.8)
    if hasattr(c, "page_start"): c.cmd("simthumbpack full", 0.2)   # the fixture's complete pack again, not paused
    # i18n A: three sample pages per language (the menu, 設定, a help page) in 簡中 and English; the default language (繁中) is every picture above
    for lg in ("cn", "en"):
        snap(f"lang_{lg}_menu", [f"lang {lg}", "uipush menu"]); snap(f"lang_{lg}_settings", ["setui"]); snap(f"lang_{lg}_help", ["uipush help"])
    # i18n B: busy pages in 簡中 (the playing page, a game with its HEAD row, a long help body), and the first-boot picker (shown in the default language: its cells carry their own)
    snap("lang_cn_now", ["lang cn", "vol 0", "track 1", "@1", "uipush now"], 1.0); c.cmd("stop", 0.5)
    snap("lang_cn_game", ["lang cn", "screen on", "games open ttt", "games mode 1", "games style 0", "games rule 0"], 0.5)
    snap("lang_cn_explain", ["lang cn", "help open card"], 0.5)
    snap("lang_cn_noise", ["lang cn", "noiseui"], 0.5); snap("lang_cn_colors", ["uipush colors"], 0.5)   # width check (fix round 1): the noise pages in 簡中
    snap("lang_pick", ["lang tw", "uipush langpick"], 0.5)
    c.cmd("lang tw", 0.5)
    if FP and hasattr(c, "page_start"): c.cmd("simtime 2026-11-20 10:30:10", 0.2)   # the quick settings show the clock's minute: freeze it for the square run (its script grows with every new picture, the minute used to flip); the round run is left as it was (its baseline holds the old minute)
    snap("qs", ["screen on", "qs"], 0.5)                        # the quick-settings page (B, one capsule per row, icon switches)
    snap("qs_armed", ["screen on", "qs", "qs tap bt"], 0.3)     # a switch that needs a reboot, after its first tap: the power icon and the line under the switches


LANG_PICS = ("lang_cn_menu", "lang_cn_settings", "lang_cn_help", "lang_en_menu", "lang_en_settings", "lang_en_help",
             "lang_cn_now", "lang_cn_game", "lang_cn_explain", "lang_cn_noise", "lang_cn_colors", "lang_pick")   # i18n A + B: sim/test.sh counts them
LANG_INST_PICS = ("inst_lang_cn_play", "inst_lang_cn_set", "inst_lang_cn_pads")   # the instrument app run (--inst)
SIM_ONLY |= set(LANG_PICS) | set(LANG_INST_PICS)   # the board's baseline has none of them (sim/known_diffs.txt)


GAME_FEEL_PICS = ("game_ttt_vanish", "game_c4_wrap", "game_result")   # G0b: sim/test.sh counts them with the games' pictures
GAME_FEEL_PICS += ("game_simon_play",)   # G1: 四色記憶 with a sector lit (the idle page is game_simon, from games.def)
GAME_FEEL_PICS += ("game_breakout_play",)   # G1: 環形打磚塊 23 fixed steps after the serve (the idle page is game_breakout, from games.def)
GAME_FEEL_PICS += ("game_beat_hit",)   # G3: 節拍點擊 after two hits (the idle page is game_beat, from games.def)
GAME_FEEL_PICS += ("game_dominoes_mid",)   # G4: 骨牌 mid-game with a tile selected (the deal is game_dominoes, from games.def)
GAME_FEEL_PICS += ("exit_bubbles", "exit_pause_breakout", "exit_pause_safe")   # G4b: the shared exit (the 返回 pill, the pause overlays)
GAME_FEEL_PICS += ("game_tower_play", "game_tower_over")   # G3: 疊塔 a few floors with a falling piece, and the result card (the idle page is game_tower, from games.def)
GAME_FEEL_PICS += ("game_spiro_done",)   # G5: 萬花尺 finished, three layers (the mid-draw page is game_spiro, from games.def)
GAME_FEEL_PICS += ("game_life_gun",)   # G6: 生命遊戲 a Gosper gun at 2 px (the 3 px soup is game_life, from games.def)
GAME_FEEL_PICS += ("game_safe_open",)   # G2: 保險箱開鎖 opened (the idle page is game_safe, from games.def)
GAME_FEEL_PICS += ("game_deflect_square",)   # G6: 旋轉擋板 on the square guard line (the round layout is game_deflect, from games.def)
GAME_FEEL_PICS += ("game_puzzle15_solved", "game_puzzle15_style2", "game_puzzle15_style3")   # G4: 15 拼圖 solved with its card, letters, gradient with hints on 5x5 (the idle page is game_puzzle15, from games.def)
GAME_FEEL_PICS += ("game_guess_ask", "game_guess_result")   # G4: 猜歌 a question with its clip playing, and the result card (the idle page is game_guess, from games.def)
GAME_FEEL_PICS += ("game_knobtoy_a", "game_knobtoy_b", "game_knobtoy_c", "game_knobtoy_maze", "game_knobtoy_maze_b", "game_knobtoy_maze_c")   # G6: 旋鈕小玩意: wind-up after a release, fishing mid-catch, the card (the idle page is game_knobtoy, from games.def)
GAME_FEEL_PICS += ("game_radio_lock",)   # G3: 電台調頻尋寶 with a station locked and the glow near the next (the idle page is game_radio, from games.def)


FX_PICS = ("fx_list", "fx_list_aptx", "fx_comp", "fx_comp_axis", "fx_eq", "fx_vwide", "fx_vroom", "fx_help")
FXEXP = re.compile(rb"APP fxexp (.*)")
FXSTATE = re.compile(rb"APP fx state n=\d+ mode=(\d+) from=(\S*)")
FXGAIN = re.compile(rb"APP fx app=gain .* gain_db=(\S+)")
FXON = re.compile(rb"APP fx app=(\w+) cls=\w+ on=(\d)")
FXLINE = re.compile(rb"APP fx app=(\w+) cls=\w+ on=\d run=\d look=\w+ ?(.*)")
FXRC = re.compile(rb"APP cmd fx rc=(-?\d+)")


def fx_read(c):
    """(the sound as TOML text lines, mode bits, the preset it came from) read through the console (`fx export`, `fx state full`)."""
    text = [m.group(1).decode() for m in FXEXP.finditer(c.cmd("fx export", 1.5))]
    st = c.cmd("fx state full", 1.5)
    m, g = FXSTATE.search(st), FXGAIN.search(st)
    if not text or not m: raise RuntimeError("fx_steps: cannot read the card's sound (fx export / fx state)")
    return text, int(m.group(1)), m.group(2).decode(), (g.group(1).decode() if g else "0"), [(a.decode(), int(o)) for a, o in FXON.findall(st)], [(m2.group(1).decode(), kv.split("=")[0], kv.split("=")[1]) for m2 in FXLINE.finditer(st) for kv in m2.group(2).decode().split()]


def fx_restore(c, saved):
    """Puts the saved sound back and persists it. A preset it came from: `fx import <file>` (stored, keeps the name). Otherwise the protocol op fx_live (it applies the text but does
    not store), then a pair of changes on the first app (on, off or the reverse: back to the same state) that makes the service write fx.txt; there is no op that stores a loose TOML
    without writing a preset file. Then the mode bits, and a check that the sound now equals the saved one (RuntimeError otherwise)."""
    text, mode, frm, gain, ons, vals = saved
    m = FXRC.search(c.cmd(f"fx import {frm}", 1.0)) if frm else None
    if not (m and m.group(1) == b"0"):
        c.cmd(json.dumps({"op": "fx_live", "id": 1, "text": "\n".join(l for l in text if l.strip() not in ("enabled = false", "on = false")) + "\n"}, ensure_ascii=False), 1.5)
        for app, on in ons: c.cmd(f"fx on {app} {on}", 0.2)      # the import takes the values (the "enabled = false" and "on = false" lines are left out: the importer skips the values of an entry that says so); an app the exported text leaves without "enabled" keeps its state: set it as saved
        for app, k, v in vals: c.cmd(f"fx set {app} {k} {v}", 0.1)   # every value `fx state` shows (the importer leaves those of an app that is off, and the bands past the count, as they were)
        tmp = "6" if float(gain) == 7.0 else "7"          # a pair of real changes (inside gain_db's -24..12) that makes the service write fx.txt; the temporary value must differ from the saved one, else both writes are no-ops
        c.cmd(f"fx set gain gain_db {tmp}", 0.5); c.cmd(f"fx set gain gain_db {gain}", 0.5)
    c.cmd(f"fx mode {mode}", 0.5)
    now = fx_read(c)
    if now[:3] != saved[:3] or now[4] != ons or now[5] != vals: raise RuntimeError(f"fx_steps: the card's sound was not put back (from {frm!r} -> {now[2]!r})")


FX_LOUD = "!!! fx_steps: the restore did NOT complete: the card's sound (_player/config/fx.txt) may be changed. Restore it from the copy taken before the run, or `fx import` the preset it came from. !!!"


def fx_parse(data):
    """The content of a config/fx.txt as {key: value} (lines `key=value`; blank lines, `#` comment lines and a missing trailing newline do not matter)."""
    d = {}
    for ln in (data or b"").decode("utf-8", "replace").splitlines():
        ln = ln.strip()
        if not ln or ln.startswith("#") or "=" not in ln: continue
        k, v = ln.split("=", 1)
        d[k.strip()] = v.strip()
    return d


def fx_kept(after, before):
    """The stored file after the run keeps everything the one before held: every key of `before` is in `after` with the same value. Keys only `after` has
    are allowed: an older firmware's file lacks the keys of the apps added since (tube, velvet_widener ...) and the rewrite adds them with their defaults.
    Returns (ok, added_keys)."""
    a, b = fx_parse(after), fx_parse(before)
    return all(k in a and a[k] == v for k, v in b.items()), sorted(set(a) - set(b))


def fx_first_diff(after, before):
    """The first key of `before` that is missing or changed in `after`, as text with both values (for the failure message)."""
    a, b = fx_parse(after), fx_parse(before)
    for k, v in b.items():
        if k not in a: return f"{k}: before {v!r}, after missing"
        if a[k] != v: return f"{k}: before {v!r}, after {a[k]!r}"
    return None


def fx_steps(c, snap, only):
    """The effects pictures. They change the sound (effects on, a preset, the aptX mode): the sound is read first, put back after the last step, also on an error, and checked equal.
    A preset file that is not on the card (`fx import` rc != 0) skips that picture with a line, it never writes a preset. Any way out where the restore did not complete (error, Ctrl-C, port error)
    prints FX_LOUD. The last line says what was verified: on the simulator the stored config/fx.txt is compared by content (key=value, any order) with the one from before (after the 3 s save delay); on a real port the console
    cannot read the file, so only the live state is checked (copy config/fx.txt off the card before the run)."""
    if only is not None and not any(n in only for n in FX_PICS): return
    saved = fx_read(c)
    path = os.path.join(c.root, "_player", "config", "fx.txt") if hasattr(c, "root") else None
    before = open(path, "rb").read() if path and os.path.exists(path) else None
    done = False                                             # False at any way out where the restore did not complete or the stored file differs
    try:
        try:
            snap("fx_list", ["fx off", "fx on compressor 1", "uipush fx"])
            snap("fx_list_aptx", ["fx off", "fx on compressor 1", "fx on freeverb 1", "fx mode 2", "uipush fx"])
            c.cmd("fx mode 0", 0.5)
            snap("fx_comp", ["fx off", "fx on compressor 1", "fxpage compressor"])
            snap("fx_comp_axis", ["fx off", "fx on compressor 1", "fxpage compressor", "fxui tap 2"])
            snap("fx_vwide", ["fx off", "fx on velvet_widener 1", "fxpage velvet_widener"])
            if only is None or "fx_eq" in only:
                m = FXRC.search(c.cmd("fx import warm.toml", 1.0))
                if m and m.group(1) == b"0": snap("fx_eq", ["fxpage equalizer"])
                else: print("SKIP fx_eq: the preset warm.toml is not on this card", flush=True)
            snap("fx_vroom", ["fx off", "fx on velvet_room 1", "fx mode 64", "fxpage velvet_room"])   # T17: on and paused under a Bluetooth sink, the reason in the state row
            c.cmd("fx mode 0", 0.5)
            snap("fx_help", ["uipush fx", "fxui tap -1"])
        finally:
            fx_restore(c, saved)
            done = True                                      # (an error of a step still propagates after this; the sound is back)
        if path:
            c.drain(4)                                       # the service stores 3 s after the last change
            after = open(path, "rb").read() if os.path.exists(path) else None
            if before is None:                               # no fx.txt yet (a fresh card, the simulator fixture): the run stored the saved sound, which is the live state checked above
                print("fx_steps: sound restored; there was no fx.txt before the run, the run stored the saved sound (live state verified)", flush=True)
            elif not fx_kept(after, before)[0]:
                done = False
                raise RuntimeError("fx_steps: the stored config/fx.txt differs from the one before the run (" + fx_first_diff(after, before) + ")")
            else:
                added = fx_kept(after, before)[1]
                print("fx_steps: sound restored; the stored fx.txt content was verified identical" + (f" (the rewrite added {len(added)} keys of newer apps with their defaults)" if added else ""), flush=True)
        else:
            print("fx_steps: sound restored; only the LIVE state was verified (the stored copy cannot be read here: compare config/fx.txt with the copy taken before the run)", flush=True)
    finally:
        if not done: print(FX_LOUD, flush=True)


def inst_pages(c, out, on_page=None):
    """The instrument app's pictures (UI unify T10, T11, spec section 7): the play page and its ring, the chips and kits lists, the settings list
    (top, scrolled to the end, an axis row borrowed), the pads, metronome and looper pages with their rings, the looper's more list and file ring,
    and the six ring effects over the play page (each 0.3 s after one note).
    on_page(name) as in pages(). "inst fx" is in memory only (gone at the reboot); "inst page 0" goes back to the play page after each.
    The instrument's settings line (cfg "inst", written by the axis steps) is put back at the end, also when a step fails."""
    keeping(c, KEEP_INST, lambda: inst_steps(c, out, on_page))


def inst_steps(c, out, on_page):
    def snap(name, cmds, wait=1.0, after=()):
        for x in cmds: c.cmd(x, 1.0 if x != cmds[-1] else wait)
        if on_page: on_page(name)
        c.shot(os.path.join(out, name + ".png"))
        for x in after: c.cmd(x, 0)            # undo what the shot needed (a note released: nothing drones on the board); no wait: the next shots keep their timing
        c.cmd("inst page 0", 0.5)
    snap("inst_play", ["inst page 0"])
    snap("inst_play_ring", ["inst page 0", "popring -2"])
    snap("inst_chips", ["inst page 4"])
    snap("inst_kits", ["inst page 3", "inst row 5"])           # INST_ROW_KIT
    snap("inst_set", ["inst page 3"])
    snap("inst_set_end", ["inst page 3", "inst knob 20"])      # the list scrolled to its last row
    snap("inst_set_axis", ["inst page 3", "inst row 1", "inst knob -1"])   # INST_ROW_MAP borrowed (selection colour); the knob gives the step back
    # UI unify T11: pads, metronome, looper, the looper's more list and file ring ("popring -2" = a tap on the page; on the pads page it is the hub's tap)
    snap("inst_pads", ["inst page 1"])
    snap("inst_pads_ring", ["inst page 1", "popring -2"])
    snap("inst_metro", ["inst page 2"])
    snap("inst_metro_ring", ["inst page 2", "popring -2"])
    snap("inst_loop", ["inst page 5"])
    snap("inst_loop_ring", ["inst page 5", "popring -2"])
    snap("inst_loopmore", ["inst page 5", "popring -2", "popring 4"])      # the ring's 更多 cell
    snap("inst_file_ring", ["inst page 5", "popring -2", "popring 4", "inst row 2"])   # the first loop file's ring (rows: save, export MOD, the files); the board's card needs a loop file
    # the ring effects over the play page, each right after one note (T11: the LED ring shows something; the simulator's note is its fake engine's)
    note = "siminst note 60 %d" if hasattr(c, "page_start") else "inst note 60 %d"   # the simulator's fake engine / the board's console
    for fx in FX: snap(f"inst_fx_{fx}", ["inst page 0", f"inst fx {fx}", note % 100], 0.3, [note % 0])
    c.cmd("inst fx pitch", 0.5)
    # 簡中 width check (i18n B fix round 1): the play page, the settings list and the pads page of the instrument app
    if hasattr(c, "page_start"):   # simulator only (the board's baseline has none)
        for nm, cmds in (("inst_lang_cn_play", ["lang cn", "inst page 0"]), ("inst_lang_cn_set", ["inst page 3"]), ("inst_lang_cn_pads", ["inst page 1"])): snap(nm, cmds)
        c.cmd("lang tw", 0.5)


def ext_pages(c, out, on_page=None):
    """The external mode's pictures (外接模式 T2, spec 2026-11-15-external-mode.md section 6), simulator only: the default media layer with the computer attached,
    the exit cell armed while it uses the card (the warning in the centre; the second tap is never sent), the three-layer sample config (ext cfg @layers; the computer
    ejects, the model reads it) on its second layer, the rejected sample (@bad) on the 設定 page; T3: the question after the unplug and the Bluetooth side with a computer connected. The fake's state only moves forward, so the order is fixed.
    No setting is touched."""
    def snap(name, cmds, wait=1.0):
        for x in cmds: c.cmd(x, 1.0 if x != cmds[-1] else wait)
        if on_page: on_page(name)
        c.shot(os.path.join(out, name + ".png"))
    snap("ext_media", ["simusb mount"])
    snap("ext_sound_status", ["ext uac play 44100 24 2"])   # T5: the computer streams 44.1 kHz / 24 bit to the sound card: the centre says the format and the underrun / overrun counts
    c.cmd("ext uac stop", 0.5)                        # (back to alt 0: the pictures below are what they were)
    snap("ext_armed", ["ext arm"])                    # the exit cell, first tap (the in-use warning)
    c.cmd("ext disarm", 0.5)
    c.cmd("ext page", 0.3)
    snap("ext_sound_cell", ["ext funcarm"])         # T4: the USB mix cell (second ring page) armed: the next mix and the reconnect warning
    c.cmd("ext disarm", 0.3)
    c.cmd("ext cfg @layers", 0.3)
    snap("ext_layers", ["ext eject", "ext layer 1"])
    c.cmd("simusb unplug", 0.3); c.cmd("ext cfg @bad", 0.3); c.cmd("simusb mount", 0.3)
    snap("ext_errors", ["ext eject", "ext errors"])
    c.cmd("back", 0.5)
    c.cmd("ext cfg @layers", 0.2)                    # (the keys.txt is read at the next eject: the default layer shows)
    snap("ext_ble_ask", ["simusb unplug"])           # T3: the cable went: the question "use Bluetooth?" (pc_xport)
    c.cmd("ext ask yes", 0.5); c.cmd("ext ble conn", 0.5)
    snap("ext_ble_conn", ["ext key 0"])              # Bluetooth side, a computer connected: the centre says so and shows its address


def safe_pages(c, out, on_page=None):
    """The safe-mode screen (UI unify T13, spec 2.8), simulator only (`sim --mode safe`; on the board the console's `safeshow` draws it over a running image,
    which is a photo for a human): one static picture."""
    c.cmd("uitop", 1.0)
    if on_page: on_page("safe")
    c.shot(os.path.join(out, "safe.png"))


def find_root(c):
    root = c.top()
    for _ in range(10):                     # pop to the bottom of the stack: that is the root the pages return to
        c.cmd("back", 0.8)
        t = c.top()
        if t == root: break
        root = t
    print(f"root screen: {root}", flush=True)
    return root


FONTMISS = re.compile(rb"APP fontmiss total=(\d+) list=(\S+)(?: more=(\d+))?")


def font_miss_report(c):
    """The run ends with the console's `fontmiss` line (glyphs no font has); total > 0 is a failure LINE of the summary, not of the exit code
    (the installed fonts still miss a few characters until the standard-set fonts are swapped in)."""
    out = c.cmd("fontmiss", 1.0)
    m = FONTMISS.search(out)
    print((m.group(0) if m else b"APP fontmiss no answer").decode(errors="replace"), flush=True)
    if m and int(m.group(1)) > 0:
        print(f"FONT MISS total={m.group(1).decode()} list={m.group(2).decode()}" + (f" more={m.group(3).decode()}" if m.group(3) else ""), flush=True)


def flat_ok():
    """sim/flat_ok.txt: picture names (one per line, '#' comments, name + reason) that are deliberately one colour (a white torch page)"""
    out = set()
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "sim", "flat_ok.txt")
    if os.path.exists(path):
        for l in open(path, encoding="utf-8"):
            l = l.strip()
            if l and not l.startswith("#"): out.add(l.split()[0])
    return out


def flat_shots(outdir, only=None):
    """names of the pictures in outdir that are ONE flat colour and not in sim/flat_ok.txt: a blank page compared with a blank baseline is a diff that can never fail
    (2026-11-18: the HUD baselines were black and show_countdown / show_stopwatch the bare background). Needs Pillow (without it: a notice, no check)."""
    try: from PIL import Image
    except ImportError:
        print("NOTICE: no Pillow: the flat-picture guard is skipped", flush=True); return []
    ok, bad = flat_ok(), []
    for f in sorted(glob.glob(os.path.join(outdir, "*.png"))):
        name = os.path.basename(f)[:-4]
        if name in ok or (only is not None and name not in only): continue
        with Image.open(f) as im:
            if all(lo == hi for lo, hi in im.convert("RGB").getextrema()): bad.append(name)
    return bad


def sim_run(outdir, src_root=None, extra=(), only=None, times=None, mode=None, clean=True):
    """The whole simulator flow (start, the first commands, every page, the exit): a simulator that dies anywhere in it is reported as
    FAILED with the command that was running; the temp root is removed in every case. Returns the exit code.
    src_root: copied as the data root instead of sim/fixture; extra: more sim arguments (--time ...); only: page names to shoot; times: page -> UTC instant for the virtual clock right before its shot.
    mode="inst": the instrument app (sim --mode inst) and its pictures instead of the player's; clean=False keeps the pictures already in outdir."""
    from sim_con import SimCon, SimCrash
    c = None
    try:
        if clean:
            for f in glob.glob(os.path.join(outdir, "*.png")): os.remove(f)      # no stale picture of an earlier run may pass for this one
        c = SimCon(extra_args=extra, src_root=src_root, times=times, strict=True, mode=mode)
        c.cmd("mem", 1.0)                   # a plain command first: the test playback is not counted
        if mode == "inst":
            inst_pages(c, outdir)
        elif mode == "ext":
            ext_pages(c, outdir)
        elif mode == "safe":
            safe_pages(c, outdir)
        else:
            pages(c, find_root(c), outdir, only)
            font_miss_report(c)
        rc = c.close()
    except (SimCrash, OSError) as e:
        print(f"FAILED: {e}", flush=True)
        return 1
    finally:
        if c: c.close()                     # idempotent: ends the process if it still runs, removes the temp root
    bad = c.sanitizer_output()
    if rc or bad:
        print(f"FAILED: simulator exit code {rc}" + "".join("\n  " + l for l in bad[:10]), flush=True)
        return 1
    flat = flat_shots(outdir, only)
    if flat:
        print("FAILED: single-colour picture(s) (a blank page; sim/flat_ok.txt lists the deliberate ones): " + " ".join(flat), flush=True)
        return 1
    if c.settings_changed:                  # a step that moved a setting did not put it back (board batch A F6)
        print("FAILED: settings changed by the run" + "".join("\n  " + l for l in c.settings_changed), flush=True)
        return 1
    return 0


def sim_run_all(outdir, src_root=None, extra=(), only=None, times=None, inst=False, player=True, ext=False, safe=False):
    """sim_run for the player (unless player=False), then (inst) once more for the instrument app and (ext) once more for the external mode into the same
    directory (--only: each only when it names one of its pages: inst_..., ext_...)."""
    rc = sim_run(outdir, src_root, extra, only, times) if player else 0
    if not player:
        for f in glob.glob(os.path.join(outdir, "*.png")): os.remove(f)
    if not rc and inst and (only is None or any(n.startswith("inst_") for n in only)):
        rc = sim_run(outdir, src_root, extra, only, times, mode="inst", clean=False)
    if not rc and ext and (only is None or any(n.startswith("ext_") for n in only)):
        rc = sim_run(outdir, src_root, extra, only, times, mode="ext", clean=False)
    if not rc and safe and (only is None or "safe" in only):
        rc = sim_run(outdir, src_root, extra, only, times, mode="safe", clean=False)
    return rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port"); ap.add_argument("outdir"); ap.add_argument("--inst", action="store_true"); ap.add_argument("--ext", action="store_true"); ap.add_argument("--safe", action="store_true")
    ap.add_argument("--sim-root", help="simulator only: the data root to copy (default sim/fixture)")
    ap.add_argument("--sim-time", help="simulator only: --time value (UTC) for the sim")
    ap.add_argument("--only", help="simulator only: comma separated page names")
    a = ap.parse_args()
    sim = a.port == "sim"
    if not sim and not a.port.startswith("/dev/serial/by-id/"): sys.exit("by-id ports only")
    os.makedirs(a.outdir, exist_ok=True)
    if sim:
        sys.exit(sim_run_all(a.outdir, a.sim_root, ["--time", a.sim_time] if a.sim_time else [], set(a.only.split(",")) if a.only else None, None, a.inst, True, a.ext, a.safe))
    if a.ext: sys.exit("--ext: simulator only (the board's USB mode has no console on its side of the plug)")
    if a.safe: sys.exit("--safe: simulator only (on the board `safeshow` draws the screen; a photo is a human item)")
    c = Con(a.port)
    c.cmd("mem", 1.0)                       # a plain command first: the test playback is not counted
    if a.inst:
        c.cmd("inst", 1.0); c.s.close()
        time.sleep(12)                      # reboot into the instrument app
        c = Con(a.port); c.cmd("mem", 1.0)
        inst_pages(c, a.outdir)
        c.cmd("inst exit", 1.0); c.s.close(); time.sleep(12)
        if Con.failed:
            print("FAILED (incomplete screenshots, do not use as baseline): " + " ".join(Con.failed), flush=True)
            sys.exit(1)
        return
    root = find_root(c)
    pages(c, root, a.outdir)
    font_miss_report(c)
    if Con.failed:
        print("FAILED (incomplete screenshots, do not use as baseline): " + " ".join(Con.failed), flush=True)
        sys.exit(1)


GAME_FEEL_PICS += ("tool_ambient_lava", "tool_ambient_fish", "tool_ambient_fire", "tool_ambient_night")   # G5: 氛圍頁 four scenes (counted with the games' pictures)

GAME_FEEL_PICS += ("noise_breath_in", "noise_breath_out")   # G7: 呼吸教練 late in the inhale and well into the exhale (counted with the games' pictures)

if __name__ == "__main__":
    main()
