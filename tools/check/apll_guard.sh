#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# hires T12 (H20): the APLL has one owner. Any use of it outside components/jc3636f118_bsp/src/bsp_audio.c (the I2S, LCD, camera and EMAC clock sources, the
# APLL's register helpers, its calibration or the audio PLL clock-out) would move the clock under the DAC, so every other file of app/main, components
# and app/managed_components is scanned.
# A hit prints "apll_guard: <file:line>" (one per site) and exits 1; otherwise "apll_guard: ok".
#   tools/check/apll_guard.sh [root]    root defaults to the repository (the directory above tools/)
#   tools/check/apll_guard.sh --selftest   one tree with a planted hit (must fail) and one clean tree (must pass)
set -u
PAT='I2S_CLK_SRC_APLL|SOC_MOD_CLK_APLL|CLK_CAL_APLL|clk_ll_apll|periph_rtc_apll|LCD_CLK_SRC_APLL|CAM_CLK_SRC_APLL|EMAC_CLK_OUT_SRC_APLL|CLKOUT_SIG_AUDIO_PLL'
OWNER=components/jc3636f118_bsp/src/bsp_audio.c

scan() {
    local root=$1 hits
    hits=$(cd "$root" 2>/dev/null && grep -rnE "$PAT" app/main components app/managed_components --include='*.c' --include='*.cpp' --include='*.h' 2>/dev/null \
           | grep -v "^$OWNER:" | cut -d: -f1,2)
    if [ -n "$hits" ]; then
        while IFS= read -r h; do echo "apll_guard: $h"; done <<<"$hits"
        return 1
    fi
    echo "apll_guard: ok"
}

if [ "${1:-}" = "--selftest" ]; then
    T=$(mktemp -d) || exit 2
    trap 'rm -rf "$T"' EXIT
    mkdir -p "$T/bad/app/main" "$T/bad/components/jc3636f118_bsp/src" "$T/good/app/main" "$T/good/components/jc3636f118_bsp/src"
    echo 'c.clk_src = I2S_CLK_SRC_APLL;' > "$T/bad/components/jc3636f118_bsp/src/bsp_audio.c"     # the owner may
    echo 'x = I2S_CLK_SRC_APLL;' > "$T/bad/app/main/other.c"                                       # nobody else
    echo 'c.clk_src = I2S_CLK_SRC_APLL;' > "$T/good/components/jc3636f118_bsp/src/bsp_audio.c"
    echo 'int ok;' > "$T/good/app/main/other.c"
    mkdir -p "$T/bad2/app/main" "$T/bad2/app/managed_components/x" "$T/bad2/components"
    echo 'x = CLKOUT_SIG_AUDIO_PLL;' > "$T/bad2/app/managed_components/x/y.c"                    # a managed component too
    scan "$T/bad2" >/dev/null; rb2=$?
    scan "$T/bad" >/dev/null; rb=$?
    scan "$T/good" >/dev/null; rg=$?
    if [ "$rb" = 1 ] && [ "$rb2" = 1 ] && [ "$rg" = 0 ]; then echo "apll_guard selftest: ok"; exit 0; fi
    echo "apll_guard selftest: FAILED (planted=$rb managed=$rb2 clean=$rg)"; exit 1
fi
scan "${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
