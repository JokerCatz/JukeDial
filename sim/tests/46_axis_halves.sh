# SPDX-License-Identifier: LGPL-2.1-or-later
# --- Selected axis rows: the first tap only selects; then the LEFT half of the row is -1 and the RIGHT half +1, both stopping at the ends (real taps; the Brightness row is the display page's first: x 55..294, y 59..106) ---
BR="setui; @0.3; settings group display; @0.3"
nowrun axis_first_tap_selects "seti brightness 60; $BR; simtap 250 82; @0.2; cfg brightness; settings rows" && {
    [ "$(cv brightness)" = "60|60|" ] || { EVID="brightness $(cv brightness)"; fail axis_first_tap_selects; }; }
nowrun axis_halves "seti brightness 60; $BR; simtap 250 82; @0.2; simtap 250 82; @0.2; cfg brightness; simtap 100 82; @0.2; cfg brightness; simtap 100 82; @0.2; cfg brightness; simtap 174 82; @0.2; cfg brightness" && {
    [ "$(cv brightness)" = "60|65|60|55|60|" ] || { EVID="brightness $(cv brightness)"; fail axis_halves; }; }   # right +5, left -5, left -5, the exact middle is the right half
nowrun axis_clamp_low "seti brightness 10; $BR; simtap 100 82; @0.2; simtap 100 82; @0.2; simtap 100 82; @0.2; cfg brightness" && {
    [ "$(cv brightness)" = "10|10|" ] || { EVID="brightness $(cv brightness)"; fail axis_clamp_low; }; }   # the low end stops: no wrap to 100
nowrun axis_clamp_high "seti brightness 100; $BR; simtap 250 82; @0.2; simtap 250 82; @0.2; cfg brightness" && {
    [ "$(cv brightness)" = "100|100|" ] || { EVID="brightness $(cv brightness)"; fail axis_clamp_high; }; }   # the high end stops: no wrap to 10
nowrun axis_chips_selected "$BR; simtap 250 82; @0.2; uiboxes" && {
    grep -q '^APP uibox button 55 59 294 106 |Brightness' <<<"$out" || { EVID="$(grep Brightness <<<"$out")"; fail axis_chips_selected; }; }
