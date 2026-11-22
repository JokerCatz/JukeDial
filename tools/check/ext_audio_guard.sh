#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# 外接模式 T5 (audio safety): source pins for the sound card's way into audio_svc. Run by make lint-app and sim/test.sh (t5_ext_source_pins).
#   1. ext_audio.c starts audio_svc, sets the device volume from pc_extstream_start_volume (min(saved, 20)), installs the fixed limiter hook (audio_svc_set_post) and only THEN
#      calls audio_svc_play_mixer (line order of the first occurrences).
#   2. audio_svc_play_mixer / audio_svc_set_post / audio_svc_set_sink / audio_svc_set_remote_volume are called from no other file of the external mode.
#   3. The external mode's files write no I2S, codec or volume register (no i2s_, bsp_audio_, es8311, drv_ names), and nothing but the mixer's render hands samples on.
#   4. The render side: both gains go through clamp_gain (<= 1, NaN = 0); the path gates on the limiter and clamps after the chain.
#   5. audio_svc.c's output stage is the one of the normal mode: the single pc_rv_block call, the single sink snapshot and the toy sound's two mix points, byte for byte.
# A hit prints "ext_audio_guard: <what>" and exits 1; otherwise "ext_audio_guard: ok".
#   tools/check/ext_audio_guard.sh [root]       root defaults to the repository
#   tools/check/ext_audio_guard.sh --selftest   a copy of the files with planted violations (each must fail) and the clean copy (must pass)
set -u
FILES="app/main/ext_audio.c app/main/ext_uac.c app/main/ext_mode.c app/main/usb_mode.c app/main/audio_svc.c components/player_core/src/pc_extstream.c components/player_core/src/pc_extpath.c"

line() { grep -n -m1 -F -- "$2" "$1" | cut -d: -f1; }

check() {
    local root=$1 bad=0 f
    cd "$root" 2>/dev/null || { echo "ext_audio_guard: no root $root"; return 1; }
    for f in $FILES; do [ -f "$f" ] || { echo "ext_audio_guard: missing $f"; bad=1; }; done
    [ $bad = 0 ] || return 1
    local A=app/main/ext_audio.c l1 l2 l3 l4 l5
    l1=$(line $A 'audio_svc_start();'); l2=$(line $A 'pc_extstream_start_volume(app_volume())'); l3=$(line $A 'audio_svc_set_volume(vol);')
    l4=$(line $A 'audio_svc_set_post(pc_extpath_post, s_path);'); l5=$(line $A 'audio_svc_play_mixer(pc_extpath_render, s_path')
    for v in "$l1" "$l2" "$l3" "$l4" "$l5"; do [ -n "$v" ] || { echo "ext_audio_guard: ext_audio.c lacks a step of the start (service, start volume, set_volume, post hook, mixer)"; bad=1; break; }; done
    if [ $bad = 0 ]; then
        { [ "$l1" -lt "$l2" ] && [ "$l2" -le "$l3" ] && [ "$l3" -lt "$l4" ] && [ "$l4" -lt "$l5" ]; } || { echo "ext_audio_guard: ext_audio.c: the order must be service, volume, limiter hook, mixer"; bad=1; }
    fi
    # 2: the entries into audio_svc, only in ext_audio.c (the sound card), ext_audio_start must precede the USB stack in usb_mode.c
    for f in app/main/ext_uac.c app/main/ext_mode.c app/main/usb_mode.c app/main/ext_usbdesc.c app/main/ext_hid_send.c app/main/ext_xport.c app/main/ext_ble.c; do
        [ -f "$f" ] || continue
        if grep -nE 'audio_svc_(play_mixer|set_post|set_sink|set_remote_volume|play|play_ex|queue_next)\(' "$f" >/dev/null; then echo "ext_audio_guard: $f calls audio_svc outside ext_audio.c"; bad=1; fi
    done
    local U=app/main/usb_mode.c u1 u2
    u1=$(line $U 'ext_audio_start();'); u2=$(line $U 'tinyusb_driver_install(&tc)')
    if [ -z "$u1" ] || [ -z "$u2" ] || [ "$u1" -ge "$u2" ]; then echo "ext_audio_guard: usb_mode.c: ext_audio_start() must come before tinyusb_driver_install"; bad=1; fi
    # 3: no output of the board's own
    for f in app/main/ext_audio.c app/main/ext_uac.c components/player_core/src/pc_extstream.c components/player_core/src/pc_extpath.c; do
        if grep -nEi 'i2s_|bsp_audio|es8311|cs43131|bsp_codec' "$f" >/dev/null; then echo "ext_audio_guard: $f touches the I2S / codec"; bad=1; fi
    done
    # 4: the render side
    local S=components/player_core/src/pc_extstream.c P=components/player_core/src/pc_extpath.c
    grep -q 'clamp_gain(gain_l)' $S && grep -q 'clamp_gain(gain_r)' $S || { echo "ext_audio_guard: pc_extstream_render does not clamp both gains"; bad=1; }
    grep -q 'PC_EXTPATH_GATE_BLOCKS' $P && grep -q 'pc_fxchain_busy' $P && grep -q 'PC_EXTPATH_CEIL' $P && grep -q 'pc_fxapp_find("limiter")' $P || { echo "ext_audio_guard: pc_extpath lost its limiter gate or the final clamp"; bad=1; }
    grep -q 'pc_extuac_gain(p->u, 0), pc_extuac_gain(p->u, 1)' $P || { echo "ext_audio_guard: pc_extpath_render: the stream's gain is not the feature unit's"; bad=1; }
    # 5: audio_svc.c, the pinned output stage
    local V=app/main/audio_svc.c
    [ "$(grep -c 'pc_rv_block(' $V)" = 1 ] || { echo "ext_audio_guard: audio_svc.c: pc_rv_block call count changed"; bad=1; }
    tr -d '\n' <$V | tr -s ' ' | grep -qF 'pc_rv_block_t bg = pc_rv_block(sink != NULL, __atomic_load_n(&s_remote, __ATOMIC_SEQ_CST) != NULL, S.paused, unity_prev, gain, s_target_gain);' || { echo "ext_audio_guard: audio_svc.c: the pc_rv_block call changed"; bad=1; }
    [ "$(grep -cF 'aud_sink_fn sink = s_sink;' $V)" = 1 ] || { echo "ext_audio_guard: audio_svc.c: the per-block sink snapshot changed"; bad=1; }
    grep -qF 'if (!sfx_rt.post) sfx_mix(buf, frames, brate, 1.0f);' $V && grep -qF 'if (sfx_rt.post) sfx_mix(buf, frames, brate, sfx_rt.gain);' $V || { echo "ext_audio_guard: audio_svc.c: the toy sound's mix points changed"; bad=1; }
    grep -qF 'const pc_sfx_route_t sfx_rt = pc_sfx_route(S.paused, s_target_gain, S.fading ? S.fade_gain : 1.0f);' $V || { echo "ext_audio_guard: audio_svc.c: the toy sound's route changed"; bad=1; }
    [ $bad = 0 ] && echo "ext_audio_guard: ok"
    return $bad
}

if [ "${1:-}" = "--selftest" ]; then
    REPO=$(cd "$(dirname "$0")/../.." && pwd)
    T=$(mktemp -d) || exit 2
    trap 'rm -rf "$T"' EXIT
    mk() { local d=$T/$1; mkdir -p "$d"; for f in $FILES app/main/ext_usbdesc.c app/main/ext_hid_send.c app/main/ext_xport.c app/main/ext_ble.c; do mkdir -p "$d/$(dirname "$f")"; cp "$REPO/$f" "$d/$f"; done; }
    mk good; check "$T/good" >/dev/null || { echo "ext_audio_guard selftest: the clean copy failed"; exit 1; }
    n=0
    plant() {   # name file sed-expression
        mk "$1"; sed -i "$3" "$T/$1/$2"
        if check "$T/$1" >/dev/null; then echo "ext_audio_guard selftest: planted '$1' passed"; n=1; fi
    }
    plant order app/main/ext_audio.c 's/^    audio_svc_set_post(pc_extpath_post, s_path); .*$//'
    plant nopost app/main/ext_audio.c 's/audio_svc_set_post(pc_extpath_post, s_path);/(void)0;/'
    plant startvol app/main/ext_audio.c 's/pc_extstream_start_volume(app_volume())/app_volume()/'
    plant i2s app/main/ext_uac.c 's/^static void wake_pump(void)$/static void wake_pump(void) { i2s_channel_write(0,0,0,0,0); }\nstatic void wake_pump2(void)/'
    plant play app/main/ext_uac.c 's/^static void wake_pump(void)$/static void wake_pump(void) { audio_svc_play_mixer(0,0,0,0,0); }\nstatic void wake_pump2(void)/'
    plant gain app/../components/player_core/src/pc_extstream.c 's/clamp_gain(gain_l)/(gain_l)/'
    plant rv app/main/audio_svc.c 's/pc_rv_block(sink != NULL, /pc_rv_block(1, /'
    plant sfx app/main/audio_svc.c 's/if (!sfx_rt.post) sfx_mix(buf, frames, brate, 1.0f);/if (!sfx_rt.post) sfx_mix(buf, frames, brate, 2.0f);/'
    plant usb app/main/usb_mode.c 's/ext_audio_start();/(void)0;/'
    [ $n = 0 ] && echo "ext_audio_guard selftest: ok"
    exit $n
fi
check "${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
