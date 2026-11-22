# SPDX-License-Identifier: LGPL-2.1-or-later
# --- THEME: a theme file on the card (cfg theme=<name>, /sdcard/_player/themes/<name>.txt): parsed at boot, a bad or missing key / file falls back to the default with one log line ---
fresh
$SIM --headless --root "$T/fix" --cmd "uipush menu; shot $T/th_def.png; tools dice; @0.3; shot $T/th_def_dice.png" >/dev/null 2>&1
printf 'theme=sunrise\n' >> "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "uipush menu; shot $T/th_sun.png; tools dice; @0.3; shot $T/th_sun_dice.png" 2>&1)
grep -qx 'APP theme sunrise: file loaded, 0 bad line(s), 4 token(s) set' <<<"$out" || { EVID="$(grep '^APP theme' <<<"$out")"; fail theme_load; }
! cmp -s "$T/th_def.png" "$T/th_sun.png" || fail theme_differs
! cmp -s "$T/th_def_dice.png" "$T/th_sun_dice.png" || fail theme_dice_differs
grep -q 'Sanitizer\|runtime error' <<<"$out" && fail theme_sanitizer
fresh
printf 'theme=bad\n' >> "$T/fix/_player/config/app.txt"; mkdir -p "$T/fix/_player/themes"
printf 'bg = #123456\nnosuch = #ffffff\ngold = nothex\ndice.9 = #ffffff\nkuji.1 = #00FF00\n' > "$T/fix/_player/themes/bad.txt"
out=$($SIM --headless --root "$T/fix" --cmd "uipush menu; shot $T/th_bad.png; @0.1; uitop" 2>&1)
{ grep -qx 'APP theme bad: file loaded, 3 bad line(s), 1 token(s) set' <<<"$out" && grep -q '^APP uitop menu' <<<"$out" && [ -s "$T/th_bad.png" ]; } || { EVID="$(grep '^APP theme\|^APP uitop' <<<"$out")"; fail theme_bad_lines; }
fresh
printf 'theme=nosuch\n' >> "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "uipush menu; shot $T/th_none.png" 2>&1)
grep -qx 'APP theme nosuch: no file, default theme' <<<"$out" || { EVID="$(grep '^APP theme' <<<"$out")"; fail theme_missing; }
cmp -s "$T/th_def.png" "$T/th_none.png" || fail theme_missing_is_default
fresh
printf 'theme=../x\n' >> "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "uipush menu" 2>&1)
grep -q '^APP theme .*: no file, default theme' <<<"$out" || fail theme_name_unsafe
fresh   # the shipped template (every token, default values) is the default look
python3 tools/gen/gen_theme_template.py "$T/fix/_player/themes/tpl.txt"
printf 'theme=tpl\n' >> "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "uipush menu; shot $T/th_tpl.png; tools dice; @0.3; shot $T/th_tpl_dice.png" 2>&1)
NTPL=$(python3 -c 'import sys; sys.path[:0] = ["tools", "tools/simtools", "tools/gen", "tools/check", "tools/board"]; import gen_theme_template as g; c, t = g.parse(); print(sum(x[1] for x in t if x[0] not in ("help", "help_text", "game_board")))')
grep -qx "APP theme tpl: file loaded, 0 bad line(s), $NTPL token(s) set" <<<"$out" || { EVID="$(grep '^APP theme' <<<"$out")"; fail theme_template_load; }
cmp -s "$T/th_def.png" "$T/th_tpl.png" && cmp -s "$T/th_def_dice.png" "$T/th_tpl_dice.png" || fail theme_template_is_default
fresh   # the ten shipped card themes (tools/gen/gen_card_themes.py): each loads with no bad line and looks different from the default
for TH in $(python3 -c 'import sys; sys.path[:0] = ["tools", "tools/simtools", "tools/gen", "tools/check", "tools/board"]; import gen_card_themes as g; print(" ".join(g.THEMES))'); do
  fresh
  mkdir -p "$T/fix/_player/themes"; python3 -c "import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; import gen_card_themes as g; sys.stdout.write(g.build_one('$TH'))" > "$T/fix/_player/themes/$TH.txt"
  printf 'theme=%s\n' "$TH" >> "$T/fix/_player/config/app.txt"
  out=$($SIM --headless --root "$T/fix" --cmd "uipush menu; shot $T/th_$TH.png; setui; @0.5; shot $T/th_${TH}_set.png" 2>&1)
  grep -q "^APP theme $TH: file loaded, 0 bad line(s)" <<<"$out" || { EVID="$(grep '^APP theme' <<<"$out")"; fail theme_card_$TH; }
  ! cmp -s "$T/th_def.png" "$T/th_$TH.png" || fail theme_card_differs_$TH
  grep -q 'Sanitizer\|runtime error' <<<"$out" && fail theme_card_sanitizer_$TH
done
fresh   # the 配色 row cycles the built-in themes AND the card themes in _player/themes/ (built-ins first, then the card themes by name; never drops the current one)
mkdir -p "$T/fix/_player/themes"; python3 -c "import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; import gen_card_themes as g; sys.stdout.write(g.build_one('aqua'))" > "$T/fix/_player/themes/aqua.txt"
python3 -c "import sys; sys.path[:0] = ['tools', 'tools/simtools', 'tools/gen', 'tools/check', 'tools/board']; import gen_card_themes as g; sys.stdout.write(g.build_one('dawn'))" > "$T/fix/_player/themes/dawn.txt"
printf 'theme=aqua\n' >> "$T/fix/_player/config/app.txt"
out=$($SIM --headless --root "$T/fix" --cmd "theme next; theme next; theme next; theme next" 2>&1)
{ [ "$(grep -o '^APP ui rebuilt theme=[^ ]*' <<<"$out" | sed 's/.*=//' | tr '\n' ' ')" = "dawn sunrise neon miku " ]; } || { EVID="$(grep '^APP ui rebuilt' <<<"$out" | tr '\n' '|')"; fail theme_row_cycles_card_themes; }
# --- end THEME ---

