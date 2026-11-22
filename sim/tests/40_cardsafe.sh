# SPDX-License-Identifier: LGPL-2.1-or-later
# --- cardsafe (2026-11-20): every failed-mount reason has a "what to do" line on the card page; "stuck" = the card answered but cannot be set up (not
# "nocard", never formatted); the USB drive mode releases the card at once when the computer goes away without an eject. Fakes: "simsd fail
# stuck|nocard|err|gpt|nofs", "simusb lost" (= unplug: the board releases the card unless it was ejected first), "ext state" ends with link= released=.
# Texts are English here (the fixture has no CJK font); the three languages live in tools/gen/i18n/strings.tsv (lint-app checks them).
mk_t12root
card_page() { $SIM --headless --root "$T/fix" --cmd "simsd fail $1; setui; @0.3; settings rows; settings key card; @0.3; uiboxes; cardfmt" 2>&1; }
cg() { grep -c "$1" <<<"$out"; }
for r in stuck nocard err; do
    fresh; out=$(card_page $r)
    case $r in
      stuck)  want1='Card not responding properly'; want2='Switch the device off and on with the power switch'; guide=repower; row='Card cannot be used (not responding)';;
      nocard) want1='No card'; want2='Insert a memory card'; guide=insert; row='Card cannot be used (no card)';;
      err)    want1='The card cannot be read'; want2='Re-insert the card, or try another card'; guide=reinsert; row='Card cannot be used (read error)';;
    esac
    { grep -q "$want1" <<<"$out" && grep -q "$want2" <<<"$out" && grep -qx "APP card page reason=$r guide=$guide" <<<"$out" && ! grep -q 'Format the card' <<<"$out" \
      && [ "$(sdf 1 | grep -o 'offered=[0-9] reason=[^ ]*')" = "offered=0 reason=$r" ] && ! grep -q 'Sanitizer\|runtime error' <<<"$out"; } || { EVID="$(grep 'APP card page\|APP sd format state\|uibox' <<<"$out" | cut -c1-120 | tr '\n' '|')"; fail "cardsafe_guide_$r"; }
done
# the settings row says the reason in words
fresh; out=$($SIM --headless --root "$T/fix" --cmd "simsd fail stuck; setui; @0.3; settings rows" 2>&1)
grep -qx 'APP settings card=Card cannot be used (not responding)' <<<"$out" || { EVID="$(grep 'settings card' <<<"$out")"; fail cardsafe_row_stuck; }
# gpt / nofs: the guide is the format offer, still there
fresh; out=$(card_page nofs)
{ grep -qx 'APP card page reason=nofs guide=format' <<<"$out" && grep -q 'Format the card' <<<"$out"; } || { EVID="$(grep 'APP card page\|uibox' <<<"$out" | cut -c1-100 | tr '\n' '|')"; fail cardsafe_guide_nofs; }
# the Status page's source line for a card that answered
nowrun cardsafe_status_stuck "simsd fail stuck; status" && {
    { grep -qx 'APP status Source: card not responding (power off and on, or re-insert)' <<<"$out" && ! grep -q 'no card (' <<<"$out"; } || { EVID="$(grep '^APP status' <<<"$out" | head -3 | tr '\n' '|')"; fail cardsafe_status_stuck; }; }

# --- the USB mode: lost = released at once, eject first = clean, nothing before the first mount
out=$(t12 "simusb mount; @0.3; ext state; simusb lost; @0.3; ext state; ext ask no; @0.5; ring; simusb mount; @0.3; ext state")
{ [ "$(grep '^SIM ext layer' <<<"$out" | sed 's/.*where=\([-0-9]*\) .*link=\([0-9]\) released=\([0-9]\)/\1 \2 \3/' | tr '\n' '|')" = "1 1 0|-1 3 1|-1 3 1|" ] \
  && grep -qx 'APP usb card released why=lost storage=ESP_OK drain_ms=0' <<<"$out" && [ "$(cg '^APP usb card released')" = 1 ] && grep -q 'USB dropped, card released' <<<"$out" \
  && grep -q 'Try another cable, then tap Reconnect' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM ext layer\|^APP usb card\|^APP ring' <<<"$out" | cut -c1-200 | tr '\n' '|')"; fail cardsafe_usb_lost; }
# a bus suspend (the computer sleeps): no I/O in flight = a harmless release with its own words (why=suspend, link 4); I/O in flight = a fault (why=lost, the cable words); an eject first = left alone
out=$(t12 "simusb mount; @0.3; ext state; simusb sleep; @0.3; ext state; ext ask no; @0.5; ring")
{ [ "$(grep '^SIM ext layer' <<<"$out" | sed 's/.*where=\([-0-9]*\) .*link=\([0-9]\) released=\([0-9]\)/\1 \2 \3/' | tr '\n' '|')" = "1 1 0|-1 4 1|" ] \
  && grep -qx 'APP usb card released why=suspend storage=ESP_OK drain_ms=0' <<<"$out" && grep -qx 'APP usb lost n=1 why=suspend' <<<"$out" && grep -q 'Computer asleep or suspended, card released' <<<"$out" \
  && grep -q 'When the computer is awake, tap Rec' <<<"$out" && ! grep -q 'USB dropped\|Try another cable' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM ext layer\|^APP usb \|^APP ring' <<<"$out" | cut -c1-200 | tr '\n' '|')"; fail cardsafe_usb_suspend_clean; }
out=$(t12 "simusb mount; @0.3; ext state; simusb sleepio; @0.3; ext state; ext ask no; @0.5; ring")
{ [ "$(grep '^SIM ext layer' <<<"$out" | sed 's/.*where=\([-0-9]*\) .*link=\([0-9]\) released=\([0-9]\)/\1 \2 \3/' | tr '\n' '|')" = "1 1 0|-1 3 1|" ] \
  && grep -qx 'APP usb card released why=lost storage=ESP_OK drain_ms=0' <<<"$out" && grep -qx 'APP usb lost n=1 why=lost' <<<"$out" && grep -q 'USB dropped, card released' <<<"$out" \
  && grep -q 'Try another cable, then tap Reconnect' <<<"$out" && ! grep -q 'Computer asleep' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM ext layer\|^APP usb \|^APP ring' <<<"$out" | cut -c1-200 | tr '\n' '|')"; fail cardsafe_usb_suspend_io; }
out=$(t12 "simusb mount; @0.3; simusb eject; @0.3; simusb sleep; @0.3; ext state")
{ [ "$(grep '^SIM ext layer' <<<"$out" | sed 's/.*link=\([0-9]\) released=\([0-9]\)/\1 \2/')" = "2 0" ] && ! grep -q '^APP usb card released' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM ext layer\|^APP usb card' <<<"$out" | tr '\n' '|')"; fail cardsafe_usb_suspend_after_eject; }
out=$(t12 "simusb mount; @0.3; simusb eject; @0.3; simusb unplug; @0.3; ext state")
{ [ "$(grep '^SIM ext layer' <<<"$out" | sed 's/.*link=\([0-9]\) released=\([0-9]\)/\1 \2/')" = "2 0" ] && ! grep -q '^APP usb card released' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM ext layer\|^APP usb card' <<<"$out" | tr '\n' '|')"; fail cardsafe_usb_clean_eject; }
out=$(t12 "simusb unplug; @0.3; ext state")
{ [ "$(grep '^SIM ext layer' <<<"$out" | sed 's/.*link=\([0-9]\) released=\([0-9]\)/\1 \2/')" = "0 0" ] && ! grep -q '^APP usb card released' <<<"$out" && ! t12bad; } || { EVID="$(grep '^SIM ext layer' <<<"$out")"; fail cardsafe_usb_never_mounted; }
# a forced exit (the computer still has the card) releases the card first
out=$(t12 "simusb mount; @0.3; ext arm; ext arm; @0.3")
grep -qx 'APP usb card released why=exit storage=ESP_OK drain_ms=0' <<<"$out" || { EVID="$(grep '^APP usb' <<<"$out" | tr '\n' '|')"; fail cardsafe_usb_exit_release; }
# the released state: the Reconnect cell and the status; a tap with no host = a message; with the host back = attached again (by the user, never by itself); exit works from it
out=$(t12 "simusb mount; @0.3; simusb lost; @0.3; ext ask no; @0.6; ring; ext reconnect; @0.3; ext state; simusb mount; @0.3; ext ask no; @0.3; ext reconnect; @0.6; ext state; ring")
{ grep -qx 'APP usb lost n=1 why=lost' <<<"$out" && [ "$(grep -c '^APP ring .*Reconnect' <<<"$out")" -ge 0 ] && grep -qx 'APP usb reconnect refused: no host' <<<"$out" \
  && [ "$(grep -c '^APP usb card attached by=user' <<<"$out")" = 1 ] && [ "$(grep '^SIM ext layer' <<<"$out" | sed 's/.*link=\([0-9]\) released=\([0-9]\)/\1\2/' | tr '\n' '|')" = "31|10|" ] && ! t12bad; } || { EVID="$(grep '^APP usb\|^APP ring\|^SIM ext layer' <<<"$out" | cut -c1-200 | tr '\n' '|')"; fail cardsafe_usb_reconnect; }
# the cell exists only while released (a normal run has none); the released page names it
out=$(t12 "simusb mount; @0.3; ext ask no; @0.3; ext reconnect; simusb lost; @0.3; ext ask no; @0.6; uiboxes; ring")
grep -q 'Reconnect' <<<"$out" || { EVID="$(grep '^APP ring\|^APP uibox' <<<"$out" | cut -c1-200 | tr '\n' '|')"; fail cardsafe_usb_reconnect_cell; }
! grep -q '^APP usb card attached' <<<"$out" || { fail cardsafe_usb_reconnect_not_when_in_use; }
# leaving from the released state works and releases nothing twice
out=$(t12 "simusb mount; @0.3; simusb lost; @0.3; ext ask no; @0.3; ext arm; ext arm; @0.3")
{ grep -qx 'APP usb mode exit in_use=0' <<<"$out" && [ "$(grep -c '^APP usb card released' <<<"$out")" = 1 ] && ! t12bad; } || { EVID="$(grep '^APP usb' <<<"$out" | tr '\n' '|')"; fail cardsafe_usb_exit_from_released; }
