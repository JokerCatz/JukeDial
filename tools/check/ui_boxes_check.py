#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""ui_boxes_check: the tap-target rules on every page of the simulator's shot set (UI unification spec section 7, T4).

  ui_boxes_check.py sim [--only a,b] [--mode inst|ext]     walk the pages of pages() of tools/simtools/ui_baseline.py (--mode inst: inst_pages(), --mode ext: ext_pages()), send `uiboxes clip` on each
  ui_boxes_check.py --from-file <txt> [--check-back]                      check a saved `uiboxes clip` output (no simulator): the counter-example tests

Input lines:  APP uibox <kind> x1 y1 x2 y2 vx1 vy1 vx2 vy2 |text      (v = the visible part: the box cut by every ancestor; see console `uiboxes clip`)
Rules (buttons only; numbers are read from app/main/ui/ui_geom.h and bsp_geom.h, not retyped):
  size    the whole box is at least UI_TAP_MIN wide and high
  gap     two visible buttons are at least UI_TAP_GAP apart (along the axis they are separated on)
  back    a page has its one Back button, UI_BACK_W x UI_BACK_H centred at UI_BACK_Y (UI_HUB_BACK_Y on the pages named in HUB_BACK), unless the page is named in NO_BACK
  text    no label (other than Back's own) is drawn over the Back button: their visible boxes do not overlap (board batch A F3)
  glass   the UI_TAP_MIN square around the button's centre (the button itself when it is smaller) lies inside the circle of radius BSP_LCD_H_RES / 2
          (narrowed from spec section 7's "everything inside the glass": a rounded corner that just touches the circle is for the picture to catch, not for this check)
Named exceptions (page, what, why) are in EXCEPTIONS below; each says what it lets through.
Output: `BOX ok <page>` or `BOX fail <page>: <rule> <box>`; at the end `BOXES ok <n> pages`, or `BOXES FAIL <k> of <n> pages` and exit 1. Exit 2: could not run."""
import argparse, os, re, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "simtools"))   # tools/simtools: sibling tools live in other folders

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
GEOM = os.path.join(ROOT, "app", "main", "ui", "ui_geom.h")
BSP = os.path.join(ROOT, "components", "jc3636f118_bsp", "include", "bsp", "bsp_geom.h")
LINE = re.compile(r"^APP uibox (label|button|qr) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) \|(.*)$")
BACK_WORDS = ("Back", "返回")


def define(path, name):
    """the first `#define NAME <integer>` of a header (the round-panel branch comes first)"""
    m = re.search(r"^[ \t]*#[ \t]*define[ \t]+" + name + r"[ \t]+\(?(-?\d+)\)?", open(path, encoding="utf-8").read(), re.M)
    if not m: sys.exit(f"ui_boxes_check: {name} not found in {path}")
    return int(m.group(1))


class Geo:
    def __init__(self):
        self.tap_min = define(GEOM, "UI_TAP_MIN")
        self.tap_gap = define(GEOM, "UI_TAP_GAP")
        self.row_gap = define(GEOM, "UI_ROW_GAP")
        self.row_w = define(GEOM, "UI_ROW_W")
        self.back_w = define(GEOM, "UI_BACK_W")
        self.back_h = define(GEOM, "UI_BACK_H")
        self.back_y = define(GEOM, "UI_BACK_Y")
        self.hub_back_y = define(GEOM, "UI_HUB_BACK_Y")
        self.w = define(BSP, "BSP_LCD_H_RES")
        self.h = define(BSP, "BSP_LCD_V_RES")


class Box:
    def __init__(self, m):
        self.kind, self.text = m.group(1), m.group(10)
        self.x1, self.y1, self.x2, self.y2 = (int(m.group(i)) for i in (2, 3, 4, 5))
        self.vx1, self.vy1, self.vx2, self.vy2 = (int(m.group(i)) for i in (6, 7, 8, 9))

    w = property(lambda s: s.x2 - s.x1 + 1)
    h = property(lambda s: s.y2 - s.y1 + 1)
    is_back = property(lambda s: s.text in BACK_WORDS)
    full = property(lambda s: f"[{s.x1},{s.y1}..{s.x2},{s.y2}|{s.text}]")


def parse(text, kind="button"):
    return [Box(m) for m in (LINE.match(l.rstrip("\r")) for l in text.splitlines()) if m and m.group(1) == kind]


def separation(a, b):
    """free pixels between two visible boxes along the axis they are apart on (negative: they overlap)"""
    gx = max(b.vx1 - a.vx2, a.vx1 - b.vx2) - 1
    gy = max(b.vy1 - a.vy2, a.vy1 - b.vy2) - 1
    return max(gx, gy)


# (page or None = every page, what it is, why). A pair is excused by `pair_ok`, a single button by `button_ok`.
EXCEPTIONS = [
    ("list_rows", "two buttons of one list: both UI_ROW_W wide, same x range, UI_ROW_GAP apart",
     "the rows of a ui_list sit UI_ROW_GAP (4) apart by design (spec section 3); the list scrolls by one row, so a finger aims at a row, not at the gap"),
    ("fortune_dice", "the faces of the dice (buttons side by side); not the Back button",
     "content, not controls: the dice are the page's one tap target drawn as three pictures; a tap on any of them rolls"),
]


# Pages that have no Back button on purpose (name: why). Every other page of the shot set must have exactly one, in the one place.
NO_BACK = {
    "menu": "the main menu is the bottom of the stack",
    "lang_cn_menu": "the main menu in 簡中 (i18n A): the bottom of the stack",
    "lang_en_menu": "the main menu in English (i18n A): the bottom of the stack",
    "message": "a message page leaves by a tap anywhere (test msg_no_back_button)",
    "pair": "select-then-confirm ring with 2 cells; the pairing sheet is answered, not left (spec 4.3 B9)",
    "show_pomo": "pomodoro: Back is a cell of the tap-revealed ring",
    "clock_drink": "F1a2 pilot: content first, the Back pill is part of the page frame's overlay (hidden until the bubble, a knob turn or a tap shows it)",
    "clock_habits": "F1a2 pilot: content first, the Back pill is part of the page frame's overlay (hidden until the bubble, a knob turn or a tap shows it)",
    "pomo_ring": "pomodoro: Back is a cell of the tap-revealed ring",
    "ring": "the alarm is ringing: a tap anywhere stops it, the knob snoozes (no way out needed, nothing to go back to)",
    "cardfmt_run": "cardfmt: the card is being formatted; f_mkfs cannot be stopped half-way, so there is no way out until the result page (which has Back)",
    "speaker_meta": "the playing page (the phone's track): a tap opens its ring, whose Back cell pops the page",
}
# the instrument (--mode inst): its play page is the bottom of the stack (a tap opens its ring; the ring's cell is Back); the ring effects are shot over it
for _n in ["inst_play", "inst_play_ring"] + [f"inst_fx_{_f}" for _f in ("pitch", "rainbow", "ripple", "comet", "spectrum", "off")]:
    NO_BACK[_n] = "the instrument's play page: the bottom of its stack, Back is a cell of its tap-revealed ring"
# the instrument's pads, metronome and looper pages (UI unify T11): no permanent Back; the way out is the Back cell of the page's tap-revealed ring
NO_BACK["inst_pads"] = "pads: the hub opens the pads ring, whose Back cell pops the page"
NO_BACK["inst_metro"] = "metronome: a tap on the page opens its ring, whose Back cell pops the page"
NO_BACK["inst_loop"] = "looper: a tap beside the controls opens its ring, whose Back cell pops the page"
for _n in ("inst_pads_ring", "inst_metro_ring", "inst_loop_ring"):
    NO_BACK[_n] = "the page's tap-revealed ring is open: Back is its cell"
# the external mode (--mode ext, 外接模式 T2): its one page is a menu ring at the bottom of its stack (no Back); the 設定 page has the shared Back button
for _n in ("ext_media", "ext_armed", "ext_layers", "ext_ble_ask", "ext_ble_conn", "ext_sound_cell", "ext_sound_status"):
    NO_BACK[_n] = "the external mode's page: a menu ring at the bottom of its stack, no Back"
# pages that are menu rings (or a ring over a page): Back is the ring's own cell, there is no button
for _n in ("bt", "colors", "tools", "fortune", "fortune_gacha", "vhome", "clock", "clockface_classic", "clockface_minimal", "clockface_digital",
           "clockface_info", "clockface_sun", "clockface_rings", "clockface_moon", "show_clockmenu", "now_paused", "now_menu", "now_menu_p2", "menu_p2",
           "album_ring", "bt_p2", "games_hub"):
    NO_BACK[_n] = "a menu ring or a tap-revealed ring: Back is its cell"
# Pages whose Back sits in the centre hub (the rim is all wheel / cards): the one named exception to UI_BACK_Y
HUB_BACK = {"fortune_wheel": "the wheel fills the rim; Back sits in its hub (spec 2.3 ui_back_button_hub)",
            "fortune_ten": "the ten cards fill the rim; Back sits in the centre (spec 2.3 ui_back_button_hub)"}


# G4b: the full-screen field pages (the bubble sheet and every game picture, game_*) have no Back button: their exit is the shared ui_exit (a 返回 pill after a knob turn,
# the round ... bubble, the pause overlay). The pause overlays (exit_pause_*) have exactly one 返回 of their own, in the middle, not at UI_BACK_Y.
NO_BACK["tools_bubbles"] = "the bubble sheet fills the whole circle; its exit is the shared 返回 pill after a knob turn (ui_exit, mode A)"
for _n in ("noise_breath_in", "noise_breath_out"):
    NO_BACK[_n] = "the breathing coach fills the page like the ambient scenes; its exit is the shared 返回 pill after a knob turn (ui_exit, mode A)"
for _n in ("tool_ambient_lava", "tool_ambient_fish", "tool_ambient_fire", "tool_ambient_night"):
    NO_BACK[_n] = "the ambient scene fills the page; its exit is the shared 返回 pill after a knob turn (ui_exit, mode A)"

# CF2: the content pages (ui_screen_t .content) rest with no Back pill: it is the popup's (ui_frame.h); a long press anywhere is the escape hatch
for _n in ("tools_dice", "tools_slot", "tools_ideas", "tools_lunch", "tools_people", "vquiz", "tool_counter", "tool_counter8", "fortune_dice"):
    NO_BACK[_n] = "a content page: content-first, the Back pill is in the popup (CF2)"

NO_BACK["lang_cn_now"] = "the playing page (cover, no Back button): same page as the now pictures, in 簡中"
NO_BACK["lang_cn_colors"] = "a menu ring: Back is its cell"
for _n in ("inst_lang_cn_play", "inst_lang_cn_pads"):
    NO_BACK[_n] = "the instrument page in 簡中: same page as inst_play / inst_pads (no Back button)"
NO_BACK["lang_pick"] = "the first-boot language picker: three rows, nothing to go back to (one tap chooses and continues)"
NO_BACK["lang_cn_game"] = "a game field page: its exit is the shared ui_exit, in 簡中"


def check_back(geo, page, boxes):
    """[(rule, text)] for the Back button rule of one page"""
    backs = [b for b in boxes if b.is_back]
    if page.startswith("exit_"): return []                # the shared exit's own 返回 pill (a popup chrome: it shows after a knob turn / a tap) is allowed
    if page.startswith("exit_pause"):   # the pause overlay's own 返回 (120 wide, in the middle of the dimmed page): exactly one
        return [] if len(backs) == 1 else [("back", f"{len(backs)} Back buttons on the pause overlay")]
    if page.startswith("game_"):
        return [("back", f"{page} has a Back button, a game page has the shared exit instead")] if backs else []
    if backs: return [("back", f"{len(backs)} visible Back button(s): GS (2026-11-16) removed every Back, the up flick goes back")]   # a hidden Back (ui_back_set) is not in the dump
    return []


def check_text(boxes, labels):
    """[(rule, text)]: a label whose visible box overlaps the visible box of a Back button (text under Back cannot be read)"""
    return [("text", f"{l.full} under {b.full}") for b in boxes if b.is_back for l in labels
            if not l.is_back and l.text.strip() and separation(l, b) < 0]


def check_page(geo, page, boxes, back=True, labels=()):
    """the failures of one page: [(rule, text)]; back=False skips the Back rule (a saved fragment is not a whole page); labels: the page's label boxes"""
    bad = (check_back(geo, page, boxes) if back else []) + check_text(boxes, labels)
    for b in boxes:
        small = b.w < geo.tap_min or b.h < geo.tap_min
        if small and page == "fortune_dice" and not b.is_back:
            small = False                                              # exception fortune_dice
        if small: bad.append(("size", b.full))
        sz = min(geo.tap_min, b.w, b.h)                                 # the square around the centre; the button when it is smaller
        cx, cy = (b.x1 + b.x2) / 2, (b.y1 + b.y2) / 2
        r = geo.w / 2
        corners = [(cx + dx * sz / 2, cy + dy * sz / 2) for dx in (-1, 1) for dy in (-1, 1)]
        if any((x - geo.w / 2) ** 2 + (y - geo.h / 2) ** 2 > r * r for x, y in corners): bad.append(("glass", b.full))
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            if separation(a, b) >= geo.tap_gap: continue
            if a.x1 == b.x1 and a.x2 == b.x2 and a.w == geo.row_w and b.w == geo.row_w and separation(a, b) >= geo.row_gap:
                continue                                                # exception list_rows
            if page == "fortune_dice" and not (a.is_back or b.is_back): continue   # exception fortune_dice
            bad.append(("gap", f"{a.full} {b.full}"))
    return bad


def report(geo, pages, back=True):
    """pages: [(name, [Box] buttons, [Box] labels)]; prints the lines, returns the number of failing pages"""
    nfail = 0
    for name, boxes, labels in pages:
        bad = check_page(geo, name, boxes, back, labels)
        if not bad: print(f"BOX ok {name}")
        else:
            nfail += 1
            for rule, what in bad: print(f"BOX fail {name}: {rule} {what}")
    n = len(pages)
    print(f"BOXES ok {n} pages" if not nfail else f"BOXES FAIL {nfail} of {n} pages", flush=True)
    return nfail


def run_sim(a, geo):
    from sim_con import SimCon, SimCrash
    import ui_baseline
    only = set(a.only.split(",")) if a.only else None
    got, c = [], None
    out = tempfile.mkdtemp(prefix="boxes_", dir=os.path.join(ROOT, ".local") if os.path.isdir(os.path.join(ROOT, ".local")) else None)
    try:
        c = SimCon(extra_args=["--mode", a.mode] if a.mode else [], strict=True)
        c.cmd("mem", 1.0)

        def on_page(name):
            o = c.cmd("uiboxes clip", 0.0).decode("utf-8", "replace")
            got.append((name, parse(o), parse(o, "label")))
        if a.mode == "inst": ui_baseline.inst_pages(c, out, on_page)   # the instrument app's picture pages (only is not used there)
        elif a.mode == "ext": ui_baseline.ext_pages(c, out, on_page)  # the USB drive mode's picture pages (only is not used there)
        else: ui_baseline.pages(c, ui_baseline.find_root(c), out, only, on_page)
        c.close()
        if c.settings_changed:                                         # the page steps must put back what they moved (board batch A F6)
            print("ui_boxes_check: settings changed by the run: " + "; ".join(c.settings_changed), flush=True)
            return 2
    except (SimCrash, OSError) as e:
        print(f"ui_boxes_check: {e}", flush=True)
        return 2
    finally:
        if c: c.close()
        import shutil
        shutil.rmtree(out, ignore_errors=True)
    if only and {g[0] for g in got} != only:
        print(f"ui_boxes_check: pages not seen: {sorted(only - {g[0] for g in got})}", flush=True)
        return 2
    return 1 if report(geo, got) else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", nargs="?", choices=["sim"])
    ap.add_argument("--only"); ap.add_argument("--mode", choices=["inst", "ext"]); ap.add_argument("--from-file"); ap.add_argument("--check-back", action="store_true", help="with --from-file: also apply the Back rule")
    a = ap.parse_args()
    geo = Geo()
    if a.from_file:
        name = os.path.splitext(os.path.basename(a.from_file))[0]
        txt = open(a.from_file, encoding="utf-8").read()
        sys.exit(1 if report(geo, [(name, parse(txt), parse(txt, "label"))], a.check_back) else 0)
    if a.what != "sim": ap.error("sim or --from-file <txt>")
    sys.exit(run_sim(a, geo))


if __name__ == "__main__":
    main()
