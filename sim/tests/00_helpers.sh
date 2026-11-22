# SPDX-License-Identifier: LGPL-2.1-or-later
# Helper functions of the simulator tests (definitions only; sourced first, always).
# Moved here from the pieces they were written in; a name defined more than once stays where it is.
fresh() { rm -rf "$T/fix"; cp -r sim/fixture "$T/fix"; [ -f .local/faces.zip ] && [ -z "$NOFACES" ] && mkdir -p "$T/fix/_player" && ln -s "$PWD/.local/faces.zip" "$T/fix/_player/faces.zip"; true; }   # the HUD faces plug-in (make faces-zip; never in git)
ALLROWS="setui; @0.3; settings group 1; settings rows; back; settings group 2; settings rows; back; settings group 3; settings rows; back; settings group 4; settings rows; back; settings group 5; settings rows; back; settings group 6; settings rows"   # every category page in turn (settings rows after each); the flat list of 22 rows is the six pages together
rows_total() { grep '^APP settings rows=' <<<"$out" | sed 's/.*rows=//' | paste -sd+ | bc; }
row() { $SIM --headless --root "$T/fix" --cmd "uipush menu; uipush settings$1; settings rows; settings group 1; settings rows; back; settings group 2; settings rows; back; settings group 3; settings rows; back; settings group 4; settings rows; back; settings group 5; settings rows; back; settings group 6; settings rows" 2>&1 | grep '^APP settings row=' ; }
wstart() {   # <root> <zoom>: boot a window, stdin = fifo (fd 7), stdout/stderr/exit code under $T/win*; cwd $T/win so manual_<n>.png lands there
    rm -rf "$T/win" "$T/win.in" "$T/win.rc"; mkdir -p "$T/win"; mkfifo "$T/win.in"
    ( cd "$T/win" && env -u DISPLAY SDL_VIDEODRIVER=dummy timeout 30 "$ABS_SIM" --root "$1" --zoom "$2" <"$T/win.in" >"$T/win.out" 2>"$T/win.err"; echo $? >"$T/win.rc" ) &
    WP=$!
    exec 7>"$T/win.in"
    for _ in $(seq 100); do   # stdout is a file here: lines appear when a command flushes
        if ! kill -0 "$WP" 2>/dev/null; then
            echo "SIM TEST fail: windowed sim exited at start-up (SDL dummy video driver missing?)"; tail -5 "$T/win.err"
            fail win_start; echo "SIM TEST $fails failed:$FAILED"; exit 1
        fi
        [ -n "$(wlast uitop 'APP uitop' 1)" ] && break
    done
    wsend "@0.6"; wlast uitop 'APP uitop' >/dev/null   # settle in virtual time (the ring ignores touches until its first timers ran): synchronous, no sleep
}
wsend() { echo "$*" >&7 2>/dev/null; }
wlast() {   # <command> <line prefix> [seconds]: send the command, wait for one new line with that prefix, print the last such line ("" after 5 s)
    local n; n=$(grep -c "^$2" "$T/win.out")
    kill -0 "$WP" 2>/dev/null || return 0   # a dead sim must not take the script down with SIGPIPE
    wsend "$1"
    for _ in $(seq $((${3:-5} * 10))); do [ "$(grep -c "^$2" "$T/win.out")" -gt "$n" ] && { grep "^$2" "$T/win.out" | tail -1; return; }; sleep 0.1; done
}
wexpect() {   # <command> <prefix> <wanted line>: ask until the answer is the wanted one (an injected event takes a few loop turns), at most 5 s
    local got=
    for _ in $(seq 25); do got=$(wlast "$1" "$2"); [ "$got" = "$3" ] && return 0; sleep 0.1; done
    EVID="wanted '$3', last answer '$got'"; echo "  $EVID"; return 1
}
wend() {   # [no arg: close stdin and wait for the exit] sets WRC
    exec 7>&-; wait "$WP"; WRC=$(cat "$T/win.rc")
}
wclean() {   # exit code 0 and nothing at all on stderr; otherwise EVID says what was seen
    [ "$WRC" = 0 ] && [ ! -s "$T/win.err" ] && return 0
    EVID="rc=$WRC stderr: $(head -c 300 "$T/win.err" | tr '\n' ' ')"; return 1
}
wline() {   # <prefix> <count before>: wait until there are more than <count> lines with that prefix in the sim's stdout (at most 5 s)
    for _ in $(seq 50); do [ "$(grep -c "^$1" "$T/win.out")" -gt "$2" ] && return 0; sleep 0.1; done
    EVID="no new '$1' line; last: $(grep "^$1" "$T/win.out" | tail -1)"; return 1
}
nowrun() {   # <check> <commands>: fresh fixture, headless; exit code, sanitizer text and "APP ui leak" fail the check. Sets out. NOWFRESH=pfresh: the pickers root instead
    [ -n "$ONLY" ] && ! [[ $1 =~ $ONLY ]] && return 1   # ONLY=<regex>: run only the cases whose name matches (an iteration aid)
    ${NOWFRESH:-fresh}
    [ -n "$NOWCFG" ] && printf '%s\n' "$NOWCFG" >> "$T/fix/_player/config/app.txt"      # NOWCFG: extra config lines for this case
    out=$($SIM --headless --root "$T/fix" --cmd "$2" 2>&1); local rc=$?
    if [ $rc != 0 ] || grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out"; then
        EVID="rc=$rc $(grep -m3 'ERROR: AddressSanitizer\|^SUMMARY\|runtime error\|^APP ui leak' <<<"$out" | tr '\n' ' ')"; fail "$1"; return 1
    fi
}
stack_is() {   # <check> <n-th uistack line> <wanted stack>
    local got; got=$(grep '^APP uistack' <<<"$out" | sed -n "$2p")
    [ "$got" = "APP uistack $3" ] || { EVID="uistack #$2: wanted '$3', got '${got#APP uistack }'"; fail "$1"; }
}
og() { grep '^APP viz axes' <<<"$out" | sed -E 's/.*order=([a-z]+) gap=([0-9]).*/\1\/\2/' | tr '\n' ' '; }
fits() {   # <check> <root>: no clipped "..." among the ring's first lines
    out=$($SIM --headless --root "$2" --cmd "$FITCMD" 2>&1)
    n=$(grep -c '^APP viz ring hint=0 title=0 line=' <<<"$out")
    [ "$n" -ge 300 ] || { EVID="only $n ring lines"; fail "$1_count"; }
    bad=$(grep '^APP viz ring hint=0' <<<"$out" | grep -E 'line=.*(\.\.\.|…) cells=' | sed -E 's/.*line=(.*) cells=.*/\1/' | sort -u | tr '\n' '|')
    [ -z "$bad" ] || { EVID="clipped: $bad"; fail "$1"; }
}
pm() { paste -d/ <(grep '^APP viz open' <<<"$out" | sed -E 's/.* map=([a-z]+)$/\1/') <(grep '^APP viz axes' <<<"$out" | sed -E 's/.* peaks=([01])$/\1/') | tr '\n' ' '; }
pushes() { grep -c "^APP screen push=$1 " <<<"$out"; }
vs() { grep '^APP viz open' <<<"$out" | sed -E 's/^APP viz open=[01] kind=([a-z]+) var=([0-9]+) ramp=([a-z]+) ring=([01]) knob=([a-z]+) .*/\1 \2 \3 \4 \5/' | tr '\n' '|'; }
vols() { grep '^SIM vol' <<<"$out" | sed 's/SIM vol //' | tr '\n' ' '; }
noback_check_playing() {   # <name>: the playing ring was open after the tap (nowmenu -5), the tops are "viz", "viz", then "now"
    [ "$(grep '^APP now menu' <<<"$out" | head -1)" = "APP now menu=1 page=0" ] || { EVID="$(grep '^APP now menu' <<<"$out")"; fail "viz_noback_${1}_ring"; }
    [ "$(grep '^APP uitop' <<<"$out" | tail -3 | tr '\n' '|')" = "APP uitop viz|APP uitop viz|APP uitop now|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail "viz_noback_${1}_exit"; }
}
noback_check() {   # <name>: nothing is playing: the 4th stat field "ring" is 1 after the tap (the style ring), the tops are "viz" then "now"
    [ "$(vs | sed 's/|/\n/g' | grep -c ' 1 kind$')" -ge 1 ] || { EVID="$(vs)"; fail "viz_noback_${1}_ring"; }
    [ "$(grep '^APP uitop' <<<"$out" | tail -2 | tr '\n' '|')" = "APP uitop viz|APP uitop now|" ] || { EVID="$(grep '^APP uitop' <<<"$out" | tr '\n' '|')"; fail "viz_noback_${1}_exit"; }
}
rl() { grep '^APP viz ring' <<<"$out" | sed -E 's/^APP viz ring hint=([01]) title=([01]) line=/\1 \2 /; s/ cells=[0-9]+ detail=[01]$//' | tr '\n' '|'; }
saves() { awk '/^APP cmd viz menu/{f=1} f&&/^APP cfg saved/{n++} END{print n+0}' <<<"$out"; }
ax() { grep '^APP viz open' <<<"$out" | sed -E 's/^APP viz open=[01] kind=([a-z]+) var=([0-9]+) ramp=([a-z]+) ring=([01]) knob=([a-z]+) .* map=([a-z]+)$/\1:\2:\3:\4:\5:\6/' | tr '\n' ' '; }
rf() { grep '^APP viz ring' <<<"$out" | sed -E 's/^APP viz ring hint=([01]) title=([01]) line=.* cells=([0-9]+) detail=([01])$/\1\2\3\4/' | tr '\n' ' '; }
vv() { grep "^APP viz $1=" <<<"$out" | sed "s/^APP viz $1=//" | tr '\n' ' '; }
vshot() { fresh; [ -n "$1" ] && printf '%s\n' "$1" >> "$T/fix/_player/config/app.txt"; $SIM --headless --root "$T/fix" --cmd "screen on; vol 0; track 3; @3; $2; viz bars; @1; $3shot $T/$4.png" >/dev/null 2>&1; }
tr_ids() { grep '^APP track idx=' <<<"$1" | sed 's/^APP track idx=\([0-9]*\) .*/\1/' | tr '\n' ' '; }
nm() { grep '^APP now menu' <<<"$out" | sed 's/^APP now //' | tr '\n' '|'; }
pz() { grep '^APP now paused' <<<"$out" | sed 's/^APP now //' | tr '\n' '|'; }
aw() { grep '^APP viz axes' <<<"$out" | sed -E 's/^APP viz axes barw=(-?[0-9]+) floor=(-?[0-9]+)( .*)?$/\1:\2/' | tr '\n' ' '; }
rg() { grep '^APP ring' <<<"$out" | sed 's/^APP ring //; s/ n=[0-9]*$//' | tr '\n' '|'; }   # the line without its trailing " n=<cells>" (UI unify T7), so the older comparisons stay as they were
rn() { grep '^APP ring' <<<"$out" | sed -n 's/.* n=\([0-9]*\)$/\1/p' | tr '\n' ' '; }   # the n= of every "ring" line (cells of the ring, Back not counted)
tp() { grep '^APP uitop' <<<"$out" | sed 's/^APP uitop //' | tr '\n' ' '; }
us() { grep '^APP uistack' <<<"$out" | sed 's/^APP uistack //' | tr '\n' '|'; }
nrb() { grep -c '^APP sysmode reboot' <<<"$out"; }
pre() { [ "$1" = 1 ] && echo "simflick left; @0.3;"; }
USBROW() { grep '^APP settings row=' <<<"$out" | sed -n "$((USBN + 1))p" | sed 's/^APP settings row=//'; }
usb_rows() { grep '^APP settings row=' <<<"$out" | grep -c 'Tap again: reboot in'; }   # armed rows in all the "settings rows" dumps
nreq() { grep -c '^APP usb_mode request' <<<"$out"; }
bub() { awk -v i="$1" 'BEGIN{a=(130+280/6*i)*3.14159265/180; printf "%d %d", 180+124*cos(a)+0.5, 180+124*sin(a)+0.5}'; }
pic_off() {   # <png> <x0> <y0> <x1> <y1>: how many pixels of the rectangle differ from the page background
    python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
a = sys.argv[1]; x0, y0, x1, y1 = map(int, sys.argv[2:6])
w, h, rows = read_png(a); want = rows[180][6:9]
print(sum(1 for y in range(max(y0, 60), y1) for x in range(x0, x1) if rows[y][3 * x:3 * x + 3] != want))" "$@"
}
first_frame() {   # <name> <theme steps> <preface commands> <command that shows the picture> <unused> <x0 y0 x1 y1 of the picture>
    fresh
    local th=""; for _ in $(seq 1 "$2"); do th="${th}theme next; "; done
    $SIM --headless --root "$T/fix" --cmd "screen on; vol 0; @1; $th$3$4; shot $T/ff_$1.png" >/dev/null 2>&1
    local bad; bad=$(pic_off "$T/ff_$1.png" $6 $7 $8 $9)
    [ "$bad" = 0 ] || { EVID="$bad pixels of the picture differ from the background"; fail "t4_first_frame_$1"; }
}
moonrun() { nowrun "$1" "simtime $2; @1.5; clock; clockface moon; @1; clockface moon?; clockface info; clockface moon; @1; clockface moon?; uitop"; }
torch_bl() { awk '/^SIM backlight/ { b = $3 } /^APP cfg brightness=/ { printf "%s|", b }' <<<"$out"; }
torch_cfg() { grep '^APP cfg brightness=' <<<"$out" | sed 's/.*=//' | tr '\n' '|'; }
tn_title() { grep '^APP tuner shown' <<<"$out" | sed -n "$1p" | sed 's/^APP tuner shown title=//; s/ hub=.*//'; }
tn_hub() { grep '^APP tuner shown' <<<"$out" | sed -n "$1p" | sed 's/.* hub=\(-*[0-9]*\) .*/\1/'; }
tn_field() { grep '^APP tuner shown' <<<"$out" | sed -n "$1p" | sed "s/.* $2=\([^ ]*\).*/\1/"; }
tn_music() { grep '^APP tuner shown' <<<"$out" | sed -n "$1p" | sed 's/.* music=\([a-z]*\) .*/\1/'; }
tn_tone() { grep '^APP tuner tonestate' <<<"$out" | sed -n "$1p" | sed "s/.* $2=\([^ ]*\).*/\1/"; }
uibox_overlaps() {   # stdin: "APP uibox <kind> x1 y1 x2 y2 |text" lines; prints each pair of boxes that intersect
    awk '/^APP uibox /{ n++; k[n]=$3; x1[n]=$4; y1[n]=$5; x2[n]=$6; y2[n]=$7; t[n]=substr($0, index($0, "|") + 1) }
         END { for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++)
                 if (x1[i] <= x2[j] && x1[j] <= x2[i] && y1[i] <= y2[j] && y1[j] <= y2[i]) print "[" t[i] "] x [" t[j] "]" }'
}
helpfresh() { fresh; [ -d fonts ] && mkdir -p "$T/fix/_player" && ln -s "$PWD/fonts" "$T/fix/_player/fonts"; true; }   # with the real CJK fonts (git-ignored fonts/, when present): the board's Chinese wrap
ps() { grep '^APP pomo state=' <<<"$out" | sed 's/^APP pomo state=\([a-z]*\) .*/\1/' | tr '\n' ' '; }
pcell() { grep '^APP popring ' <<<"$out" | sed -n "$1p" | sed 's/.* cells=\([0-9]*\) .*/\1/'; }
pcen() { grep '^APP popring ' <<<"$out" | sed -n "$1p" | sed 's/.* centre=//'; }
bracket_fix() {   # <extra files...>: the fixture plus a folder of untagged songs whose titles are their file names
    mkdir "$T/fix/export/新歌"
    for n in "【Aimer】甲" "【Aimer】乙" "$@"; do cp "$T/fix/export/紙飛機工廠/04_untitled_demo.mp3" "$T/fix/export/新歌/$n.mp3"; done
}
vshots() {   # <name> <commands before the shots>: six shots 0.3 s apart (screen on, no sound, the page open on voices); out = the console, files $T/<name>_0..5.png
    local c="screen on; vol 0; @1; $2"; for i in 0 1 2 3 4 5; do c="$c; @0.3; shot $T/$1_$i.png"; done
    out=$($SIM --headless --root "$T/fix" --cmd "$c" 2>&1)
}
vbars() {   # <n> <png>...: "<bar groups> <slots with a glyph>" of the voices picture (n slots of 300 / n columns for the names: the names of neighbours can be 7 px apart)
    python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
bars, names = set(), set()
for f in sys.argv[2:]:
    w, h, rows = read_png(f); bg = rows[180][6:9]
    for y in range(80, 80 + 180):
        for x in range(30, 330):
            if rows[y][3 * x:3 * x + 3] != bg: bars.add(x)
    for y in range(80 + 184, 80 + 198):
        for x in range(30, 330):
            if rows[y][3 * x:3 * x + 3] != bg: names.add(x)
n = int(sys.argv[1]); g, last = 0, None
for x in sorted(bars):
    if last is None or x - last > 2: g += 1
    last = x
print(g, len(set((x - 30) * n // 300 for x in names)))" "$@"
}
vring() {   # <png>...: how many separate groups the union of the shots has on the circle of radius 70 around the ring picture's centre (179.5, 179.5)
    python3 -c "
import sys, math; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
lit = [False] * 1440
for f in sys.argv[1:]:
    w, h, rows = read_png(f); bg = rows[180][6:9]
    for i in range(1440):
        a = 2 * math.pi * i / 1440
        x, y = round(179.5 + 70 * math.cos(a)), round(179.5 + 70 * math.sin(a))
        if rows[y][3 * x:3 * x + 3] != bg: lit[i] = True
print(sum(1 for i in range(1440) if lit[i] and not lit[i - 1]))" "$@"
}
vx() { grep '^APP viz extra' <<<"$out" | tail -1; }
many_packs() {   # <decks> <lists>
    fresh; mkdir -p "$T/fix/vocab" "$T/fix/lists"
    local i; for ((i = 1; i <= $1; i++)); do cp app/main/assets/vocab/sample.zip "$T/fix/vocab/deck$(printf %02d $i).zip"; done
    for ((i = 1; i <= $2; i++)); do cp app/main/assets/lists/lunch.zip "$T/fix/lists/lunch$(printf %02d $i).zip"; done
}
list_rows() { grep '^APP uibox button' <<<"$1" | grep -v '|\(Back\|返回\)$' | awk '{print $4, $5, $6, $7}'; }
rows_disjoint() {   # <boxes output>
    local back; back=$(grep '^APP uibox button' <<<"$1" | grep '|\(Back\|返回\)$' | awk '{print $4, $5, $6, $7}')
    [ -n "$back" ] || back="9999 9999 9999 9999"   # GS: no Back button any more: only the rows against each other
    list_rows "$1" | awk -v back="$back" 'BEGIN { split(back, B, " ") }
        { x1[NR]=$1; y1[NR]=$2; x2[NR]=$3; y2[NR]=$4 }
        END { for (i=1;i<=NR;i++) {
                for (j=i+1;j<=NR;j++) if (x1[i]<=x2[j] && x2[i]>=x1[j] && y1[i]<=y2[j] && y2[i]>=y1[j]) { print "rows " i " and " j " intersect"; bad=1 }
                a=y1[i]<35?35:y1[i]; b=y2[i]>285?285:y2[i]
                if (a<=b && x1[i]<=B[3] && x2[i]>=B[1] && a<=B[4] && b>=B[2]) { print "row " i " hits Back"; bad=1 } }
              exit bad }'
}
chooser_each() {   # <check> <n> <open cmds> <choose line prefix> <fixture builder args...>
    local chk=$1 n=$2 open=$3 pre=$4; shift 4
    local i o bx cx cy got
    for ((i = 0; i < n; i++)); do
        many_packs "$@"
        o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; $open; @0.3; simknob -9; @0.2; simknob $i; @0.2; uiboxes" 2>&1)
        bx=$(list_rows "$o" | sed -n "$((i + 1))p")
        [ -n "$bx" ] || { EVID="row $i missing: $(grep -c '^APP uibox button' <<<"$o") buttons"; fail "${chk}_row$i"; continue; }
        read -r x1 y1 x2 y2 <<<"$bx"; cx=$(((x1 + x2) / 2)); cy=$(((y1 + y2) / 2))
        many_packs "$@"
        o=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; $open; @0.3; simknob -9; @0.2; simknob $i; @0.2; simtap $cx $cy; @0.3; uitop" 2>&1)
        got=$(grep -c "^APP $pre" <<<"$o")
        [ "$got" = 1 ] || { EVID="tap $cx,$cy on row $i: $got choose lines"; fail "${chk}_row$i"; }
    done
}
slot_label() {   # grid, zh, en: the result label after the spin has this text ('/' = line break)
    local o
    o=$($SIM --headless --root "$T/fix" --cmd "tools grid $1; @6; uiboxes" 2>&1)
    grep -q "^APP uibox label .*|\($2\|$3\)\$" <<<"$o" || { EVID="grid $1: $(grep '^APP uibox label' <<<"$o" | cut -d'|' -f2 | tr '\n' ' ')"; fail "slot_label_$1"; }
}
box_of() { grep "^APP uibox button .*|\($1\)\$" <<<"$2" | awk '{print $4, $5, $6, $7}'; }   # "x1 y1 x2 y2" of the button with this text
back_top() { echo 294; }   # POP: the popup card holds 返回 now (it moves); the page's old Back slot (UI_BACK_Y - UI_BACK_H / 2 + the panel centre) is the lower limit of the content, as before
cv() { grep "^APP cfg $1=" <<<"$out" | sed "s/^APP cfg $1=//" | tr '\n' '|'; }
srow() { grep "^APP settings row=$1" <<<"$out" | sed "s/^APP settings row=$1//" | tr '\n' '|'; }
sy() { awk '/^APP uibox button/ && !seen { print $5; seen = 1 } /^APP uiboxes end/ { seen = 0 }' <<<"$out" | tr '\n' ' '; }   # the first row's top: the list's scroll
key_group() { case $1 in sleep) echo power;; theme|style|bright) echo display;; haptic|toysfx) echo sound;; *) echo 1;; esac; }
settings_axis() {   # <check> <settings key> <label prefix> <after tap> <after knob 2> <after knob -1> <after two taps>
    nowrun "$1" "screen on; @0.5; vol 0; setui; @0.3; settings group $(key_group $2); uiboxes; settings key $2; settings key $2; settings rows; simknob 2; settings rows; simknob -1; settings rows; uiboxes; simvol; @4.5; simknob 1; settings rows; uiboxes; simvol" && {
        [ "$(srow "$3")" = "$4|$5|$6|$6|" ] || { EVID="$(srow "$3")"; fail "$1"; }
        set -- "$1" "$2" "$3" "$4" "$5" "$6" "$7" $(sy)
        { [ "$8" = "$9" ] && { [ "${10}" != "$9" ] || [ "$2" = sleep ]; }; } ||   # the Power page is one row (it cannot scroll): there the row only stays put { EVID="first row top: $8 $9 ${10} (unmoved while borrowed, scrolled after)"; fail "$1_scroll"; }
        [ "$(vols)" = "0 0 " ] || { EVID="volumes: $(vols)"; fail "$1_volume"; }; }   # the knob was the row's, then the list's: never the volume
    fresh; out=$($SIM --headless --root "$T/fix" --cmd "setui; @0.3; settings key $2; settings key $2; settings key $2; settings rows" 2>&1)
    [ "$(srow "$3")" = "$7|" ] || { EVID="two taps: $(srow "$3")"; fail "$1_taps"; }
}
ar() { grep '^APP uibox button .*|\(Repeat\|重複\): ' <<<"$out" | sed 's/.*|\(Repeat\|重複\): //' | tr '\n' '|'; }
pickers_fix() { fresh; sim/pickers_root.sh "$T/pick" || fail t13_pickers_root; }   # the four lists' data (sim/pickers_root.sh, shared with make sim-leak); the 30 noise scenes are "simnoise 30 3"
need_pick() { [ -d "$T/pick" ] || pickers_fix; }   # the four lists' data root, made on first use (22 makes it again and checks it)
pfresh() { need_pick; rm -rf "$T/fix"; cp -r "$T/pick" "$T/fix"; }
block_rows() { awk -v b="$2" '/^APP uiboxes end/{k++; next} k==b-1 && /^APP uibox button/ && !/\|(Back|返回)$/ {t=$0; sub(/^[^|]*\|/, "", t); print $4, $5, $6, $7 "|" t}' <<<"$1"; }
picker_each() {   # <check> <open cmds> <first> <count> <page> [<prefix cmds>] [<verify cmds>]
    local chk=$1 open=$2 first=$3 cnt=$4 page=$5 pre=${6:-@0} ver=${7:-@0} c i k r o taps=""
    c="screen on; @0.5; $pre; $open; @0.3; uiboxes"; for ((i = 0; i < first + cnt; i++)); do c="$c; simknob 1; @0.1; uiboxes"; done
    pfresh; o=$($SIM --headless --root "$T/fix" --cmd "$c" 2>&1)
    for ((r = first; r < first + cnt; r++)); do
        local hit=""
        for ((k = 0; k <= first + cnt; k++)); do
            local bx; bx=$(block_rows "$o" $((k + 1)) | sed -n "$((r + 1))p" | cut -d'|' -f1)
            [ -n "$bx" ] || continue
            read -r x1 y1 x2 y2 <<<"$bx"
            if [ "$y1" -ge 35 ] && [ "$y2" -le 285 ]; then hit="$k $(((x1 + x2) / 2)) $(((y1 + y2) / 2))"; break; fi
        done
        [ -n "$hit" ] || { EVID="row $r never inside the window"; fail "${chk}_row$r"; continue; }
        read -r k x y <<<"$hit"
        pfresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; @0.5; $pre; $open; @0.3; simknob $k; @0.1; simtap $x $y; @0.3; $ver" 2>&1)
        taps="$taps$(grep "^APP pick $page " <<<"$out" | awk '{print $4}' | tr '\n' ' ')"
        LASTOUT=$out
    done
    PICKS=$taps
    BOXES=$o
}
row_colours() {   # <png> <boxes output, one dump>
    block_rows "$2" 1 | python3 -c "
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; from png_diff import read_png
w, h, rows = read_png(sys.argv[1])
for line in sys.stdin:
    b, t = line.rstrip('\n').split('|', 1); x1, y1, x2, y2 = map(int, b.split())
    y = (y1 + y2) // 2; x = x1 + 4
    print(('%02x%02x%02x' % tuple(rows[y][3 * x:3 * x + 3])) if y1 >= 35 and y2 <= 285 else '------', t, sep='|')" "$1"
}
pa() { grep '^APP now paused' <<<"$out" | sed 's/.*audio=//' | tr '\n' ' '; }
stopidx() { grep '^APP now cells' <<<"$out" | head -1 | sed 's/.* items=//' | tr '|,' '\n\n' | grep -n '^stop$' | cut -d: -f1; }
treefresh() { fresh; local m="$T/fix/export/紙飛機工廠/04_untitled_demo.mp3" d="$T/fix/export" i; mkdir -p "$d/many/sub" "$d/deep/a" "$d/deep/b"
    for ((i = 1; i <= 100; i++)); do cp "$m" "$d/many/s$(printf %03d $i).mp3"; done; for i in 1 2 3; do cp "$m" "$d/many/sub/t$i.mp3"; done
    cp "$m" "$d/deep/a/x.mp3"; cp "$m" "$d/deep/b/y.mp3"; cp "$m" "$d/root1.mp3"; cp "$m" "$d/root2.mp3"; }
corruptfresh() { fresh; python3 - "$T/fix/_player/index/library.bin" <<'PY' || return 1
import struct, sys
p = sys.argv[1]; b = bytearray(open(p, "rb").read())
n, nf = struct.unpack_from("<II", b, 8)[0:2]   # header: magic, version, n, nfolders, ...
off = 32 + n * 40 + 24
assert struct.unpack_from("<i", b, off)[0] == 1, "nsub of folder 0 is not 1 (layout changed?)"
struct.pack_into("<i", b, off, 2); open(p, "wb").write(b)
PY
}
tm() { grep '^SIM timers' <<<"$out" | sed -n "$1p"; }
r40() {   # <check> <commands> <stack 1> <stack 2> <stack 3>: the three uistack dumps; timer census #1 (before) = #2 (after)
    nowrun "$1" "$2" && { stack_is "$1_before" 1 "$3"; stack_is "$1_message" 2 "$4"; stack_is "$1_after" 3 "$5"
        [ -n "$(tm 1)" ] && [ "$(tm 1)" = "$(tm 2)" ] || { EVID="$(tm 1) / $(tm 2)"; fail "$1_timers"; }; }
}
lay() { grep '^APP uibox button .*|[●・] Rain ' <<<"$(awk -v b="$1" '/^APP uiboxes end/{k++; next} k==b-1' <<<"$out")" | sed 's/^APP uibox button [-0-9]* \([-0-9]*\) .*|[●・] Rain /\1 /'; }
leak_knob_proof() {   # <action> <probe> <root or ""> -> prints "<action> knobs=<n> unchanged=<m>"
    python3 - "$1" "$2" "$3" <<'PY'
import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']
import leak_cycles as L
act, probe, root = sys.argv[1], sys.argv[2], sys.argv[3] or None
b = L.SimBoard(root)
orig = b.cmd
stats = [0, 0]
def cmd(c, wait=0.4, until=None):
    if not c.startswith("knob "): return orig(c, wait, until)
    before = [l for l in orig(probe, 0) if l.startswith(b"APP ")]
    out = orig(c, wait, until)
    after = [l for l in orig(probe, 0) if l.startswith(b"APP ")]
    stats[0] += 1; stats[1] += before == after
    return out
b.cmd = cmd
setup, fn, td, doc = L.ACTIONS[act]
for c in setup: L.do(b, c)
b.cmd("screen on", 0.1); fn(b, 0)
print(f"{act} knobs={stats[0]} unchanged={stats[1]}")
b.c.close()
PY
}
uis() { grep '^APP uistack' <<<"$out" | sed 's/^APP uistack //' | tr '\n' '|'; }   # every uistack dump of the run, "|" between
nomenu() { grep '^APP now menu=' <<<"$out" | tail -1 | sed 's/^APP now menu=\([01]\).*/\1/'; }   # the playing ring open (1) or not, last "nowmenu -5"
scxy() { box_of "$1" "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}'; }
scrows() { block_rows "$out" "$1" | cut -d'|' -f2 | tr '\n' ','; }   # the row texts of the n-th uiboxes dump
ran() { grep "^APP poptest ran $2 " <<<"$1" | wc -l; }   # <out> <cell>: how many times the cell ran
pst() { grep '^APP popring ' <<<"$out" | sed 's/^APP popring //'; }
pstn() { pst | sed -n "$1p"; }
bx() { set -- $1; printf 'APP uibox button %s %s %s %s %s %s %s %s |x\n' "$1" "$2" "$3" "$4" "$1" "$2" "$3" "$4"; }
hy() { grep '^APP uibox button .*|\(時: \|Hour: \)' <<<"$1" | awk '{print $5}'; }
rows6() { grep '^APP uibox button ' <<<"$(awk -v b="$1" '/^APP uiboxes end/{k++; next} k==b-1' <<<"$out")" | sed 's/^[^|]*|//' | tr '\n' ','; }   # row texts of the n-th uiboxes dump
vrow() { grep "^APP uibox button .*|[●・] Rain" <<<"$(awk -v b="$1" '/^APP uiboxes end/{k++; next} k==b-1' <<<"$out")" | sed 's/^[^|]*|//'; }
sn() { grep '^APP tools stat dice' <<<"$out" | sed "s/.*$1=\([0-9]*\).*/\1/" | tr '\n' ' '; }   # <rolls|dice_n> of every "tools stat" of the run
tv() { grep '^APP timers sw=' <<<"$out" | tail -1 | sed "s/.* $1=\([^ ]*\).*/\1/"; }
tf() { grep '^APP tface ' <<<"$out" | tail -1 | sed "s/.* $1=\([^ ]*\).*/\1/"; }
mkzip() {   # mkzip <out> <store|deflate> : the smallest face pack, one face (rd_sw: a frame layer and the slot "main")
    python3 - "$1" "$2" <<'PYEOF'
import sys, zipfile
out, mode = sys.argv[1], sys.argv[2]
ct = zipfile.ZIP_STORED if mode == "store" else zipfile.ZIP_DEFLATED
g = "0123456789:.-_"
sheet = b"P5\n%d 10\n255\n" % (8 * len(g)) + b"\xff" * (80 * len(g))
frame = b"P5\n20 10\n255\n" + b"\xff" * 200
with zipfile.ZipFile(out, "w", ct) as z:
    z.writestr("sheets/sheets.txt", "sheet seg32 seg.pgm glyph 8 10 0 %s %s\n" % (g, ",".join(["8"] * len(g))))
    z.writestr("sheets/seg.pgm", sheet)
    z.writestr("rd_sw/face.txt", "face rd_sw 360 360 round\nlayer frame frame.pgm 100 100\nslot main 120 150 seg32 digit ghost=1 chars=88:88\n")
    z.writestr("rd_sw/frame.pgm", frame)
PYEOF
}
bkbox() { box_of '返回\|Back' "$1"; }
efresh() { rm -rf "$T/fix"; mkdir "$T/fix"; }
t9() { $SIM --headless --mode inst --root "$T/t9root" --cmd "$1" 2>&1; }
t10() { rm -rf "$T/t10r"; cp -r "$T/t10root" "$T/t10r"; $SIM --headless --mode inst --root "$T/t10r" --cmd "$1" 2>&1; }
t10bad() { grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out"; }
ph() { awk -v k="$1" '/^APP uitop/{n++; next} n==k' <<<"$out"; }          # the lines between the (k)th and (k+1)th "uitop"
cur_of() { grep '^INST uistat ' <<<"$1" | sed -n 's/.* cur=\(-\?[0-9]*\).*/\1/p' | head -1; }
t11() { rm -rf "$T/t11r"; cp -r "$T/t11root" "$T/t11r"; $SIM --headless --mode inst --root "$T/t11r" --cmd "$1" 2>&1; }
t11bad() { grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out"; }
bpms() { grep -o '^SIM inst metro .*bpm=[0-9]*' <<<"$out" | sed 's/.*bpm=//' | tr '\n' ' '; }
cnt() { grep '^INST loop state=' <<<"$out" | sed 's/.* cnt=\([0-9/]*\) .*/\1/' | tr '\n' ' '; }
kitof() { grep '^INST cfg ' <<<"$out" | sed -n 's/.* kit=\([^ ]*\).*/\1/p' | tr '\n' ' '; }
notes() { grep '^APP uibox label ' <<<"$out" | cut -d'|' -f2 | tr '\n' ','; }
lrows() { grep '^APP uibox button ' <<<"$out" | cut -d'|' -f2 | grep -c '^loop_'; }
t12() { rm -rf "$T/t12r"; cp -r "$T/t12root" "$T/t12r"; $SIM --headless --mode ext --root "$T/t12r" --cmd "$1" 2>&1; }
t12bad() { grep -q 'Sanitizer\|runtime error\|APP ui leak' <<<"$out"; }
hid() { grep '^SIM ext hid' <<<"$out" | tr '\n' '|'; }
xp() { grep '^SIM ext xport' <<<"$out" | tr '\n' '|'; }
dump() { grep '^SIM ext usb dump' <<<"$out" | sed 's/^SIM ext usb dump //' | tr '\n' '|'; }
uacd() { grep '^SIM ext uac done' <<<"$out"; }
uacf() { uacd | sed -n "$1p" | tr ' ' '\n' | grep "^$2=" | cut -d= -f2; }   # field $2 of the $1-th "done" line
t13() { rm -rf "$T/t13r"; cp -r "$T/t12root" "$T/t13r"; "$@" 2>&1; }
tcf() { rm -rf "$T/tcfr2"; cp -r "$T/tcfr" "$T/tcfr2"; $SIM --headless --mode inst --root "$T/tcfr2" --cmd "$1" 2>&1; }
tcp() { rm -rf "$T/tcfr2"; cp -r "$T/tcfr" "$T/tcfr2"; $SIM --headless --root "$T/tcfr2" --cmd "$1" 2>&1; }   # the player's console (poptest)
fxapp() { grep "^APP fx app=$1 " <<<"$out" | tail -1; }          # the last state line of an app
fxfirst() { grep "^APP fx app=$1 " <<<"$out" | head -1; }
fxval() { sed -n "s/.* $2=\([^ ]*\).*/\1/p" <<<"$1"; }          # one field of a state line
jtext() { python3 -c 'import json,sys;print(json.dumps(open(sys.argv[1]).read()))' "$1"; }
ctlline() { grep "^CTL " <<<"$out" | sed -n "$1p"; }
yofnote() { grep '^APP uibox label .*|Settings: ' <<<"$1" | awk '{print $5}'; }
last_above_back() { awk -v off="${2:-0}" 'BEGIN { bt = 294 + off } /^APP uibox label / { y = $7 } /^APP uibox button .*\|(Back|返回)$/ { bt = $5 + off } END { exit !(y != "" && y < bt) }' <<<"$1"; }   # GS: no Back button: the line where it was (y 294) is the bound
tyof() { grep '^APP uibox button .*|Threshold: ' <<<"$1" | awk '{print $5}'; }   # the y of the Threshold row in one uiboxes dump: the page's scroll
fxrows() { grep '^APP fxui ' <<<"$out"; }                       # every dump line of "fxui rows" (header, row, end)
t14_radio() { pfresh; out=$($SIM --headless --root "$T/fix" --cmd "$1" 2>&1); }
t14_last() { grep '^APP uibox button' <<<"$1" | grep -v '|\(Back\|返回\)$' | tail -1 | sed 's/.*|//'; }   # the label of the page's last row (Back excluded)
t14_lists_fix() { fresh; mkdir -p "$T/fix/lists"; local i; for i in 1 2 3 4 5 6 7; do cp app/main/assets/lists/lunch.zip "$T/fix/lists/l$i.zip"; done; }
rt() { grep '^APP ratetest ' <<<"$out" | tr '\n' '|'; }
bb() { grep '^APP bubbles n=' <<<"$out" | sed "$1q;d" | sed 's/^APP bubbles //'; }          # the $1-th dump header
bbv() { bb "$1" | tr ' ' '\n' | sed -n "s/^$2=//p"; }                                             # one value of it
bbs() { awk -v k="$1" '/^APP bubbles n=/{c++} c==k && /^APP bubbles row /{printf "%s", $5} END{print ""}' <<<"$out"; }   # the k-th dump's state characters, all rows in bubble order
vrs() { grep '^APP fx app=velvet_room ' <<<"$out"; }
cfv() { cf "$1" | tr ' ' '\n' | grep "^$2=" | cut -d= -f2-; }                                  # a field of it (row= is the rest of the line: no spaces in the fields before)
cfrow() { cf "$1" | sed 's/.* row=//'; }
sdf() { grep '^APP sd format state=' <<<"$out" | sed "$1q;d" | sed 's/^APP sd format //'; }
nfmt() { grep -c '^SIM sd format fs=' <<<"$out"; }
cpd() { grep '^APP codepad len=' <<<"$out" | sed "$1q;d" | sed 's/^APP codepad //'; }
c444fresh() {
    fresh; mkdir -p "$T/fix/export/00_c444"
    ffmpeg -loglevel error -y -f lavfi -i "testsrc=size=320x320:rate=1" -frames:v 1 -pix_fmt yuvj444p "$T/c444.jpg" \
      && python3 -I -c "from PIL import Image; Image.open('$T/c444.jpg').save('$T/prog.jpg', progressive=True)" \
      && python3 -I - "sim/fixture/export/山間小火車/01_first_train.mp3" "$T/fix/export/00_c444" "$T/c444.jpg" "$T/prog.jpg" <<'EOF'
import sys
src, out, *pics = sys.argv[1:]
b = open(src, 'rb').read()
n = 0
for c in b[6:10]: n = n << 7 | c
audio = b[10 + n:]
def ss(v): return bytes([(v >> 21) & 127, (v >> 14) & 127, (v >> 7) & 127, v & 127])
def frame(fid, body): return fid + ss(len(body)) + b'\0\0' + body
for k, pic in enumerate(pics):
    body = frame(b'TIT2', b'\x03c%d' % k) + frame(b'TALB', b'\x03zz cover') + frame(b'APIC', b'\x00image/jpeg\x00\x03\x00' + open(pic, 'rb').read())
    open('%s/%02d_c.mp3' % (out, k + 1), 'wb').write(b'ID3\x04\x00\x00' + ss(len(body)) + body + audio)
EOF
}
cvl() { grep '^APP covers state=' <<<"$out" | sed -n "$1p" | sed 's/ unchecked=.*//'; }   # the k-th `covers` line without unchecked/tidy/row and its texts
cvp() { grep '^APP covers page ' <<<"$out" | sed -n "$1p"; }                          # the k-th page dump
bml() { grep '^APP btmeta ' <<<"$out" | sed -n "$1p"; }                           # the k-th btmeta line
bmband() { grep '^APP uibox label 95 11 264 32 |' <<<"$out" | sed -n "$1p" | cut -d'|' -f2-; }   # the k-th dump's title band text
t50() { grep '^SIM timers' <<<"$out" | sed -n "$1p" | sed 's/.*periods=//' | tr ',' '\n' | grep -cx 50; }   # running 50 ms timers in the k-th dump
t50p() { grep '^SIM timers' <<<"$out" | sed -n "$1p" | sed 's/.*periods=//' | tr ',' '\n' | grep -cx 50p; }
sx() { grep '^APP sfx on=' <<<"$out" | tail -1 | sed 's/^APP sfx //'; }
sxv() { sx | tr ' ' '\n' | sed -n "s/^$1=//p"; }
pick_top() {   # $1 = the config file's text ("" = no file), $2 extra SIM_STATE; prints the top page at boot
    rm -rf "$T/lp"; mkdir -p "$T/lp/_player/config"; [ -n "$1" ] && printf '%s\n' "$1" > "$T/lp/_player/config/app.txt"
    ( [ -n "$2" ] && export SIM_STATE="$2"; $SIM --headless --root "$T/lp" --cmd "@0.5; uitop" 2>&1 | grep '^APP uitop' )
}
rl_labels() { grep '^APP uibox button' <<<"$1" | grep -v '|\(Back\|返回\)$' | sed 's/.*|//'; }
rot_pt() { python3 -c 'import sys; k, n, x, y = map(int, sys.argv[1:]); print(*[(x, y), (n-1-x, n-1-y)][k])' "$@"; }   # pc_rotate_screen

# --- what the pieces used to borrow from each other: now every piece has it from here (or prepares it itself), so a piece runs on its own ---
# constants (plain assignments)
NOWMENU="simtap 180 180; @0.3"; PAUSE="simtap 180 180; @0.3"      # the Now page: open its menu / pause by a tap on the middle
PM="screen on; vol 0; @0.5; clock; show pomo; @0.5"               # the pomodoro page (19, 25, 31)
NB="uitop; simtap 180 318; @0.3; viz stat; uitop; viz pick -3; uitop"   # the viz page without a Back cell (15, 16)
DELRE='刪除情境\|Delete scene'   # the delete row is the last one: the knob scrolls it into view (UI unify T6: a row, armed 4 s by ui_arm_tap)
T6P="screen on; @0.5; vol 0; simnoise 30 3 custom; simnoise own; noiseui; sceneui 0; @0.3"
BTSLOT=("81 279" "40 180" "81 81" "180 40" "279 81" "320 180" "279 279" "180 320")   # GS: eight cells a page (no 下一頁 cell, no Back cell)
AS=app/main/audio_svc.c; BA=components/jc3636f118_bsp/src/bsp_audio.c   # sources the source checks read (28, 30, 31, 34); (RS, the radio source, is set in 30: 22 uses the name for something else)
# prepared directories (idempotent or re-made fresh)
mk_root() { rm -rf "$T/root"; mkdir "$T/root"; mkdir -p "$T/root/_player/config"; echo lang=0 > "$T/root/_player/config/app.txt"; }   # the plain data root (10, 11, 13, 27)
mk_t10root() { rm -rf "$T/t10root"; cp -r sim/fixture "$T/t10root"; }   # the instrument-mode root t10 copies (26, 91: games/gs.sh)
mk_t12root() { rm -rf "$T/t12root"; cp -r sim/fixture "$T/t12root"; mkdir -p "$T/t12root/_player/config"; }   # the external-mode root t12/t13 copy (27, 28)
mk_t11root() { rm -rf "$T/t11root"; cp -r sim/fixture "$T/t11root"; }   # the instrument-mode root t11 copies (26, 28)
need_base() { [ -e "$T/base.ok" ] || { python3 tools/simtools/ui_baseline.py sim "$T/base" >"$T/base.log" 2>&1 && touch "$T/base.ok"; }; }   # the whole baseline set: 10 checks it, 12 compares against it
# values read off a simulator run: each is computed on first use by the same command line the piece that checks it runs (10 simulator runs at most, no checks here)
need_d6() { [ -n "${D6:-}" ] && return 0; fresh; out=$($SIM --headless --root "$T/fix" --cmd "$T6P; simknob 9; @0.2; uiboxes" 2>&1); D6=$(box_of "$DELRE" "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}'); }   # the delete-scene row (25: t6_delete_row)
need_dc() { [ -n "${DC:-}" ] && return 0; fresh; printf '%s\n' dice_n=1 >> "$T/fix/_player/config/app.txt"; out=$($SIM --headless --root "$T/fix" --cmd "tools dice; @0.5; frame hold; uiboxes" 2>&1); DC=$(box_of '顆數：1\|Dice: 1' "$out" | awk '{print int(($1 + $3) / 2), int(($2 + $4) / 2)}'); }   # the dice-count button (25: t7_dice_boxes)
need_btn() { [ -n "${BTN:-}" ] && return 0; fresh; out=$($SIM --headless --root "$T/fix" --cmd "btui; @0.3; ring" 2>&1); BTN=$(rn | tr -d ' '); BTN=${BTN:-8}; }   # the cells of the Bluetooth ring (17: t15_bt_ring_n)
need_bbs() { [ -n "${BB_S:-}" ] && return 0; fresh; out=$($SIM --headless --root "$T/fix" --cmd "screen on; tools bubbles; @0.3; bubbles; uitop; uistack" 2>&1); BB_S=$(bbv 1 special); }   # the special bubble (31: bubbles_open)
need_hv8() { [ -n "${HV8:-}" ] && return 0; fresh; out=$($SIM --headless --root "$T/fix" --cmd "fx state" 2>&1); HV8=$(sed -n 's/^APP fx app=\([a-z_]*\) cls=heavy.*/\1/p' <<<"$out" | head -1); LT8=$(sed -n 's/^APP fx app=\([a-z_]*\) cls=light.*/\1/p' <<<"$out" | head -1); }   # the first heavy / light fx app (30: hr8_fx_state_ids)
