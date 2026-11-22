# SPDX-License-Identifier: LGPL-2.1-or-later
# --- fx apps T3 ---
# the state on the card (fx.txt), the service split (fx_state.c + the simulator's fake_fx_audio.c running the real chain on silence), the mode bits
nowrun t3_fx_boot_off "fx state; fx stat" && {
    cnt=$(grep -c '^APP fx app=' <<<"$out")
    { [ "$(grep '^APP fx state n=' <<<"$out")" = "APP fx state n=$cnt mode=0 from=" ] && [ "$cnt" -ge 13 ] && ! grep '^APP fx app=' <<<"$out" | grep -qv ' on=0 ' && grep -qx 'APP fx state missing' <<<"$out"; } || { EVID="n=$cnt $(grep '^APP fx state' <<<"$out" | tr '\n' '|')"; fail t3_fx_boot_off; }
    [ "$(grep -c '^APP fx app=[a-z_]* cls=\(light\|heavy\) ' <<<"$out")" = "$cnt" ] && [ "$(grep -c ' cls=heavy ' <<<"$out")" -ge 1 ] || fail t3_fx_boot_cls
}
NOWCFG="sound=warm.toml" nowrun t3_fx_migrate "fx state; cfg sound" && {
    ok=1
    grep -qx 'APP fx migrated from warm.toml' <<<"$out" || ok=0
    for a in equalizer tone limiter freeverb; do fxapp $a | grep -q ' on=1 ' || ok=0; done
    grep -qx 'APP cfg sound=' <<<"$out" || ok=0
    grep -qx 'state_from=warm.toml' "$T/fix/_player/config/fx.txt" || ok=0
    grep -q '^APP fx state n=[0-9]* mode=0 from=warm.toml$' <<<"$out" || ok=0
    [ $ok = 1 ] || { EVID="$(grep '^APP fx migrated\|^APP cfg\|^APP fx state' <<<"$out" | tr '\n' '|') $(grep state_from "$T/fix/_player/config/fx.txt")"; fail t3_fx_migrate; }
}
# an unreadable file named by cfg sound: the cfg key stays, everything is off, the failure is logged
NOWCFG="sound=nothere.toml" nowrun t3_fx_migrate_missing "fx state; cfg sound" && {
    { grep -qx 'APP fx migrate failed nothere.toml' <<<"$out" && grep -qx 'APP cfg sound=nothere.toml' <<<"$out" && ! grep '^APP fx app=' <<<"$out" | grep -qv ' on=0 '; } || { EVID="$(grep '^APP fx migrate\|^APP cfg' <<<"$out" | tr '\n' '|')"; fail t3_fx_migrate_missing; }
}
nowrun t3_fx_set_store "fx on compressor 1; fx set compressor threshold_db -30; @3.5; fx stat; fx state" && {
    sw=$(grep -n '^APP fx apply op=swap app=compressor' <<<"$out" | head -1 | cut -d: -f1)
    st=$(grep -n '^APP fx apply op=set app=compressor param=0 v=-30$' <<<"$out" | head -1 | cut -d: -f1)
    { [ -n "$sw" ] && [ -n "$st" ] && [ "$sw" -lt "$st" ] && grep -qx 'compressor.threshold_db=-30' "$T/fix/_player/config/fx.txt" && grep -qx 'state_from=' "$T/fix/_player/config/fx.txt" \
        && grep -q '^APP fx stat mail_free=32 ret_free=16 blocks=1 live=1 ' <<<"$out"; } || { EVID="swap@$sw set@$st $(grep '^APP fx stat ' <<<"$out") $(grep -c . "$T/fix/_player/config/fx.txt") lines"; fail t3_fx_set_store; }
    [ "$(fxval "$(fxapp compressor)" threshold_db)" = -30 ] || fail t3_fx_set_state
}
# nothing is on the card before the 3 s are over
nowrun t3_fx_store_debounce "fx on gain 1; @2.5" && { [ ! -e "$T/fix/_player/config/fx.txt" ] || fail t3_fx_store_debounce; }
nowrun t3_fx_mode_find "fx state" && {
    H=$(sed -n 's/^APP fx app=\([a-z_]*\) cls=heavy.*/\1/p' <<<"$out" | head -1)
    [ -n "$H" ] || fail t3_fx_mode_heavy
    nowrun t3_fx_mode "fx on loudness 1; fx on $H 1; @0.2; fx mode 2; @0.2; fx state; fx mode 0; @0.2; fx state" && {
        { grep -qx 'APP fx mode=2 heavy=off' <<<"$out" && grep -qx 'APP fx mode=0 heavy=run' <<<"$out"; } || { EVID="$(grep '^APP fx mode' <<<"$out" | tr '\n' '|')"; fail t3_fx_mode_lines; }
        h1=$(fxfirst "$H"); c1=$(fxfirst loudness); h2=$(fxapp "$H"); c2=$(fxapp loudness)
        { grep -q ' run=0 look=dim ' <<<"$h1" && grep -q ' run=1 ' <<<"$c1" && grep -q ' run=1 ' <<<"$h2" && grep -q ' run=1 ' <<<"$c2" && grep -q '^APP fx state n=[0-9]* mode=2 ' <<<"$out"; } || { EVID="H(mode2)=$h1 light=$c1 H(mode0)=$h2"; fail t3_fx_mode; }
    }
}
nowrun t3_fx_alloc_fail "fx on compressor 1; @0.2; fx state; fx failalloc 1; fx on freeverb 1; @0.2; fx state; fx on freeverb 0; fx on freeverb 1; @0.2; fx state" && {
    ids=$(grep '^APP fx app=' <<<"$out" | awk '{print $3}' | sort -u | sed 's/app=//')
    bad=
    for i in $ids; do
        [ "$i" = freeverb ] && continue
        a=$(grep "^APP fx app=$i " <<<"$out" | sed -n 1p); b=$(grep "^APP fx app=$i " <<<"$out" | sed -n 2p)
        [ "$a" = "$b" ] || bad="$bad $i"
    done
    fv2=$(grep '^APP fx app=freeverb ' <<<"$out" | sed -n 2p); fv3=$(grep '^APP fx app=freeverb ' <<<"$out" | sed -n 3p)
    { [ -z "$bad" ] && grep -q '^APP fx alloc fail app=freeverb bytes=[0-9]*$' <<<"$out" && grep -q ' on=1 run=0 look=dim ' <<<"$fv2" && grep -q ' on=1 run=1 look=run ' <<<"$fv3" \
        && fxapp compressor | grep -q ' run=1 '; } || { EVID="changed:$bad fv2=$fv2 fv3=$fv3"; fail t3_fx_alloc_fail; }
}
# the production dirty-bit path: a knob turn on a REBUILD value = alloc + SWAP; while the previous SWAP has not been taken, the newest value waits (no block per step)
SETS=; v=1; for k in $(seq 1 100); do SETS="$SETS fx set limiter lookahead_ms $v;"; last=$v; v=$(awk -v v="$v" 'BEGIN{v+=0.5; if (v>20) v=1; print v}'); done
SETSC=; v=-10; for k in $(seq 1 40); do SETSC="$SETSC fx set compressor threshold_db $v;"; v=$(( -21 - v )); done
nowrun t3_fx_rebuild_coalesce "fx on limiter 1; @0.2;$SETS fx stat; @0.5; fx stat; fx state" && {
    live1=$(grep '^APP fx stat ' <<<"$out" | sed -n 1p | sed 's/.* live=\([0-9]*\).*/\1/')
    swaps=$(grep -c '^APP fx apply op=swap app=limiter' <<<"$out")
    { [ "$live1" -le 2 ] && grep '^APP fx stat ' <<<"$out" | sed -n 2p | grep -q 'blocks=1 live=1 ' && grep '^APP fx stat ' <<<"$out" | sed -n 2p | grep -q 'mail_free=32 ret_free=16' && [ "$swaps" -le 3 ] \
        && [ "$(fxval "$(fxapp limiter)" lookahead_ms)" = "$last" ]; } || { EVID="live1=$live1 swaps=$swaps last=$last $(grep '^APP fx stat ' <<<"$out" | tr '\n' '|') $(fxapp limiter)"; fail t3_fx_rebuild_coalesce; }
    ! grep -q '^APP fx apply op=set app=limiter param=2 ' <<<"$out" || fail t3_fx_rebuild_no_set       # X13: a REBUILD value never travels as a SET
}
nowrun t3_fx_save_on_reboot 'fx on gain 1; fx set gain gain_db -1; {"op":"reboot","id":1}' && {
    { grep -q '^APP sysmode reboot' <<<"$out" && grep -qx 'gain.gain_db=-1' "$T/fix/_player/config/fx.txt" && grep -qx 'gain.on=1' "$T/fix/_player/config/fx.txt"; } || { EVID="$(grep -c . "$T/fix/_player/config/fx.txt" 2>&1) lines"; fail t3_fx_save_on_reboot; }
}
fresh
{ printf 'gain.on=2\nx\ncompressor.ratio=\ngain.gain_db=zz\nequalizer.count=999\ntone.mix=-4\n'; head -c 300 /dev/urandom; printf '\n'; } > "$T/fix/_player/config/fx.txt"
out=$($SIM --headless --root "$T/fix" --cmd "fx state; vol 0; track 1; @3; @250; fx stat; uitop; fx state" 2>&1); rc=$?
# the garbage lines are ignored or clamped (gain.on=2 -> on, zz and an empty value -> the default, count 999 -> 10, mix -4 -> 0), nothing is nan/inf, and the music really plays on (the track ends and the next starts)
{ [ $rc = 0 ] && ! grep -q 'Sanitizer\|runtime error\|^APP ui leak' <<<"$out" && ! grep '^APP fx app=' <<<"$out" | grep -qi 'nan\|inf' && grep -q '^APP fx stat mail_free=32 ret_free=16' <<<"$out" && grep -qx 'APP uitop now' <<<"$out" \
    && [ "$(grep -c '^APP track idx=' <<<"$out")" -ge 2 ] \
    && fxfirst gain | grep -q ' on=1 .* gain_db=0' && fxfirst compressor | grep -q ' ratio=2.5 ' && fxfirst equalizer | grep -q ' count=10 ' && fxfirst tone | grep -q ' mix=0$'; } \
    || { EVID="rc=$rc tracks=$(grep -c '^APP track idx=' <<<"$out") $(fxfirst gain) | $(fxfirst tone) | $(grep -m3 'Sanitizer\|runtime error' <<<"$out" | tr '\n' ' ')"; fail t3_fx_bad_file_keeps_playing; }
# I1 (review): an OFF that could not be posted (full mailbox) must not eat the changes of the following on
nowrun t3_fx_off_on_full_mailbox "fx on compressor 1; @0.2;$SETSC fx stat; fx set compressor threshold_db -40; fx on compressor 0; fx on compressor 1; @0.5; fx stat; fx state" && {
    lastset=$(grep '^APP fx apply op=set app=compressor param=0 ' <<<"$out" | tail -1 | sed 's/.* v=//')
    { [ "$(grep '^APP fx stat ' <<<"$out" | sed -n 1p | sed 's/.*mail_free=\([0-9]*\).*/\1/')" = 0 ] && [ "$lastset" = -40 ] && [ "$(fxval "$(fxapp compressor)" threshold_db)" = -40 ] && fxapp compressor | grep -q ' run=1 ' \
        && grep '^APP fx stat ' <<<"$out" | sed -n 2p | grep -q 'mail_free=32 ret_free=16 blocks=1 live=1 '; } || { EVID="lastset=$lastset $(grep '^APP fx stat ' <<<"$out" | tr '\n' '|') $(fxapp compressor)"; fail t3_fx_off_on_full_mailbox; }
}
# I2 (review): with nothing playing the chain still drains (the board's hook runs while busy): a look-ahead change shows as run, the mailbox ends empty,
# 20 on/off pairs of the reverb leave no block parked in the mailbox
nowrun t3_fx_stopped_drains "fx on limiter 1; fx set limiter lookahead_ms 5; fx set limiter lookahead_ms 7; @0.3; fx state; fx stat" && {
    { fxapp limiter | grep -q ' look=run ' && [ "$(fxval "$(fxapp limiter)" lookahead_ms)" = 7 ] && grep '^APP fx stat ' <<<"$out" | tail -1 | grep -q 'mail_free=32 ret_free=16 blocks=1 live=1 '; } || { EVID="$(fxapp limiter) $(grep '^APP fx stat ' <<<"$out" | tr '\n' '|')"; fail t3_fx_stopped_drains; }
}
PAIRS=; for k in $(seq 1 20); do PAIRS="$PAIRS fx on freeverb 1; fx on freeverb 0; @0.1;"; done
nowrun t3_fx_stopped_no_parking "$PAIRS @0.5; fx stat" && {
    grep '^APP fx stat ' <<<"$out" | tail -1 | grep -q 'mail_free=32 ret_free=16 blocks=0 live=0 ' || { EVID="$(grep '^APP fx stat ' <<<"$out" | tail -1)"; fail t3_fx_stopped_no_parking; }
}
# M2: a failed first write keeps cfg sound (fx.txt.tmp is a directory: the write cannot start); the next boot migrates again and removes the key
fresh; printf 'sound=warm.toml\n' >> "$T/fix/_player/config/app.txt"; mkdir "$T/fix/_player/config/fx.txt.tmp"
out=$($SIM --headless --root "$T/fix" --cmd "cfg sound; @0.2" 2>&1)
{ grep -qx 'APP fx migrated from warm.toml' <<<"$out" && grep -qx 'APP cfg sound=warm.toml' <<<"$out" && grep -q '^APP fx store n=[0-9]* rc=-1$' <<<"$out" && [ ! -e "$T/fix/_player/config/fx.txt" ]; } || { EVID="$(grep '^APP fx\|^APP cfg' <<<"$out" | tr '\n' '|')"; fail t3_fx_migrate_write_fails; }
rmdir "$T/fix/_player/config/fx.txt.tmp"
out=$($SIM --headless --root "$T/fix" --cmd "cfg sound" 2>&1)
{ grep -qx 'APP fx migrated from warm.toml' <<<"$out" && grep -qx 'APP cfg sound=' <<<"$out" && grep -qx 'state_from=warm.toml' "$T/fix/_player/config/fx.txt"; } || { EVID="$(grep '^APP fx\|^APP cfg' <<<"$out" | tr '\n' '|')"; fail t3_fx_migrate_then_ok; }
# M3: an import that leaves an app on retries an allocation that failed; M7: fx mode leaves the battery bit to the power tick
nowrun t3_fx_import_retries_alloc "fx failalloc 1; fx on freeverb 1; fx state; fx import warm.toml; fx state" && {
    { grep '^APP fx app=freeverb ' <<<"$out" | sed -n 1p | grep -q ' run=0 look=dim ' && grep '^APP fx app=freeverb ' <<<"$out" | sed -n 2p | grep -q ' on=1 run=1 look=run '; } || { EVID="$(grep '^APP fx app=freeverb ' <<<"$out" | tr '\n' '|')"; fail t3_fx_import_retries_alloc; }
}
nowrun t12_fx_ldac_heavy "fx on freeverb 1; fx on loudness 1; @0.2; fx mode 4; @0.2; fx state" && {
    { grep -qx 'APP fx mode=4 heavy=off' <<<"$out" && grep '^APP fx app=freeverb ' <<<"$out" | tail -1 | grep -q ' run=0 look=dim ' && grep '^APP fx app=loudness ' <<<"$out" | tail -1 | grep -q ' run=1 '; } || { EVID="$(grep '^APP fx mode\|app=freeverb\|app=loudness' <<<"$out" | cut -c1-80 | tr '\n' '|')"; fail t12_fx_ldac_heavy; }; }
SIM_STATE="charging=0" nowrun t3_fx_mode_keeps_batt "fx on freeverb 1; @0.2; fx state; fx mode 0; fx state; fx mode 6; fx state" && {
    { grep -q '^APP fx state n=[0-9]* mode=1 ' <<<"$out" && ! grep -q '^APP fx state n=[0-9]* mode=0 ' <<<"$out" && grep -q '^APP fx state n=[0-9]* mode=7 ' <<<"$out" && grep '^APP fx app=freeverb ' <<<"$out" | sed -n 2p | grep -q ' run=0 '; } || { EVID="$(grep '^APP fx state n=' <<<"$out" | tr '\n' '|')"; fail t3_fx_mode_keeps_batt; }
}
# the old chain is gone: nothing of it is left in the sources
{ ! grep -rq 'fx_force_reduced\|pc_fx_level\|fx_svc\.h\|pc_chain_' app/main components/player_core/src components/player_core/include sim/*.c sim/CMakeLists.txt; } || fail t3_old_chain_gone

# --- fx apps T4 ---
# the settings protocol: fx_apps / fx_export / fx_list state_from / fx_live in memory only / status fx
MIXTXT=$(jtext sim/fixture/_player/config/sound/mixed.toml)
nowrun t4_fx_apps_op 'fx state; {"op":"fx_apps","id":1}' && {
    n=$(sed -n 's/^APP fx state n=\([0-9]*\) .*/\1/p' <<<"$out"); c=$(ctlline 1)
    ids=$(sed -n 's/^APP fx app=\([a-z_]*\) .*/\1/p' <<<"$out" | tr '\n' ' ')
    jids=$(python3 -c 'import json,sys;print(" ".join(a["id"] for a in json.loads(sys.argv[1][4:])["apps"]))' "$c" 2>/dev/null)
    { [ "$(grep -o '"cls":' <<<"$c" | wc -l)" = "$n" ] && [ "$jids " = "$ids" ] && grep -Eq '"id":"ratio"[^}]*"lo":1,"hi":20' <<<"$c"; } || { EVID="n=$n ids=$ids jids=$jids"; fail t4_fx_apps_op; }
    python3 - "$c" <<'PY' || fail t4_fx_apps_shape
import json,sys
a=json.loads(sys.argv[1][4:])["apps"]
for x in a:
    for k in("id","zh","en","desc_zh","desc_en","cls","look","why","on","params"): assert k in x,k
    assert x["cls"] in("light","heavy") and x["look"] in("off","run","dim")
    for p in x["params"]:
        for k in("id","zh","en","lo","hi","def","step","unit","flags","v"): assert k in p,k
assert not [x for x in a if x["id"]=="compressor"][0]["on"]
PY
}
nowrun t4_fx_export_op 'fx on gain 1; {"op":"fx_export","id":1}' && {
    c=$(ctlline 1)
    python3 - "$c" <<'PY' || { EVID="$(ctlline 1 | cut -c1-200)"; fail t4_fx_export_op; }
import json,sys
t=json.loads(sys.argv[1][4:])["text"]
assert 'kind = "gain"' in t and "format = 1" in t
i=t.index('kind = "compressor"'); assert "enabled = false" in t[i:i+200]
PY
}
nowrun t4_fx_live_bad 'fx on gain 1; fx state; {"op":"fx_live","id":1,"text":"this is = [not toml"}; fx state' && {
    c=$(ctlline 1)
    a=$(grep '^APP fx app=' <<<"$out" | sed -n '1,/^$/p' | head -$(( $(grep -c '^APP fx app=' <<<"$out") / 2 )) | md5sum); b=$(grep '^APP fx app=' <<<"$out" | tail -$(( $(grep -c '^APP fx app=' <<<"$out") / 2 )) | md5sum)
    { grep -q '"ok":false' <<<"$c" && [ "$a" = "$b" ]; } || { EVID="$c"; fail t4_fx_live_bad; }
}
nowrun t4_fx_list_state_from 'fx import warm.toml; {"op":"fx_list","id":1}; fx set gain gain_db -1; {"op":"fx_list","id":2}' && {
    { ctlline 1 | grep -q '"state_from":"warm.toml"' && ctlline 2 | grep -q '"state_from":""' && ! ctlline 1 | grep -q '"current"'; } || { EVID="$(grep '^CTL' <<<"$out" | tr '\n' '|')"; fail t4_fx_list_state_from; }
}
nowrun t4_status_fx 'fx import warm.toml; {"op":"status","id":1}' && {
    ctlline 1 | grep -q '"fx":"4 on"' || { EVID="$(ctlline 1)"; fail t4_status_fx; }
}
nowrun t4_fx_live_unsupported "fx import warm.toml; {\"op\":\"fx_live\",\"id\":1,\"text\":\"format = 1\\n[[music]]\\nkind = \\\"nosuchthing\\\"\\n\"}" && {
    ctlline 1 | grep -q '"ok":true.*"unsupported":\["nosuchthing"\]' || { EVID="$(ctlline 1)"; fail t4_fx_live_unsupported; }
}
# the dim row carries its reason
nowrun t4_fx_heavy_find "fx state"
H4=$(sed -n 's/^APP fx app=\([a-z_]*\) cls=heavy.*/\1/p' <<<"$out" | head -1)
nowrun t4_fx_apps_why "fx on $H4 1; fx mode 2; @0.3; {\"op\":\"fx_apps\",\"id\":1}" && {
    python3 - "$(ctlline 1)" "$H4" <<'PY' || fail t4_fx_apps_why
import json,sys
a=json.loads(sys.argv[1][4:])["apps"]
d=[x for x in a if x["look"]=="dim"]
assert d and all(x["why"] for x in d), d
assert [x for x in a if x["id"]==sys.argv[2]][0]["look"]=="dim"
PY
}
nowrun t4_fx_live_then_tap_base "fx import warm.toml; @3.5" && cp "$T/fix/_player/config/fx.txt" "$T/fx_before.txt"
nowrun t4_fx_live_then_tap "fx import warm.toml; @3.5; {\"op\":\"fx_live\",\"id\":1,\"text\":$MIXTXT}; fx state; @3.5" && {
    { ctlline 1 | grep -q '"ok":true' && fxapp gain | grep -q ' on=1 ' && fxapp compressor | grep -q ' on=1 ' && fxapp equalizer | grep -q ' on=0 ' && cmp -s "$T/fix/_player/config/fx.txt" "$T/fx_before.txt" && grep -qx 'state_from=warm.toml' "$T/fix/_player/config/fx.txt"; } || { EVID="$(fxapp gain) cmp=$(cmp "$T/fix/_player/config/fx.txt" "$T/fx_before.txt" 2>&1)"; fail t4_fx_live_then_tap; }
}
nowrun t4_fx_live_then_tap2 "fx import warm.toml; @3.5; {\"op\":\"fx_live\",\"id\":1,\"text\":$MIXTXT}; @3.5; fx set gain gain_db -1; @3.5" && {
    f="$T/fix/_player/config/fx.txt"; ok=1
    for l in gain.on=1 gain.gain_db=-1 compressor.on=1 compressor.threshold_db=-20 compressor.ratio=3 crossfeed.on=1 limiter.on=1 limiter.ceiling_db=-1 equalizer.on=0 tone.on=0 freeverb.on=0 state_from=; do
        grep -qx "$l" "$f" || { ok=0; EVID="$EVID missing:$l"; }
    done
    [ $ok = 1 ] || fail t4_fx_live_then_tap2
}

# --- fx apps T5 ---
# the second accent colour per theme: the generator's own check of the committed table (rule of spec 6.6)
out=$(python3 tools/gen/gen_themes.py --check components/player_core/src/pc_theme_table.inc 2>&1); rc=$?
{ [ $rc = 0 ] && [ "$out" = "21 themes ok" ]; } || { EVID="rc=$rc $out"; fail t5_theme_on_check; }
# kit7: one list row per look (normal, sel, dim, sys, on) through ui_list_row_look; the ON row's background differs from the SEL row's
nowrun t5_kit7_looks "uipush kit7; @0.3; uiboxes" && {
    L=$(grep '^APP kit look ' <<<"$out" | sed 's/^APP kit look //' | tr '\n' '|')
    n=$(grep -c '^APP kit look ' <<<"$out")
    lk() { grep "^APP kit look $1 " <<<"$out" | sed "s/.* $2=\\([0-9a-f]*\\).*/\\1/"; }
    nb=$(lk 0 bg); sel=$(lk 1 bg); on=$(lk 4 bg); sy=$(lk 3 bg); nf=$(lk 0 fg); df=$(lk 2 fg)
    { [ "$n" = 5 ] && [ -n "$sel" ] && [ -n "$on" ] && [ "$sel" != "$on" ] && [ "$on" != "$nb" ] && [ "$sel" != "$nb" ] && [ "$sy" != "$nb" ] && [ -n "$df" ] && [ "$df" != "$nf" ] && [ "$(grep -c '^APP uibox button .*|\(normal\|sel\|dim\|sys\|on\)$' <<<"$out")" = 5 ]; } || { EVID="looks=$L"; fail t5_kit7_looks; }
}

# --- fx apps T6 ---
# the one 說明 piece: the explain page (title is 說明 with the CJK font, Help without), the settings list-end row, the popring's last cell
nowrun t6_explain_settings "uipush help; @0.3; help show; uitop" && {
    grep -Eq '^APP explain id=settings title=(說明|Help) paras=([3-9]|[1-9][0-9]+) qr=1 web=0$' <<<"$out" && [ "$(tp)" = "explain " ] || { EVID="$(grep '^APP explain\|^APP uitop' <<<"$out")"; fail t6_explain_settings; }; }
nowrun t6_help_open_by_id "help open settings; @0.3; uitop; back; help open nosuch; uitop; help show" && {
    [ "$(tp)" = "explain menu " ] && grep -qx 'APP help unknown nosuch' <<<"$out" && grep -qx 'APP explain none' <<<"$out" || { EVID="$(grep '^APP help\|^APP explain\|^APP uitop' <<<"$out" | tr '\n' '|')"; fail t6_help_open_by_id; }; }
nowrun t6_settings_help_row "setui; @0.3; help tap; @0.3; uitop; back; @0.3; uitop" && {
    [ "$(tp)" = "explain settings " ] || { EVID="top: $(tp)"; fail t6_settings_help_row; }; }
nowrun t6_help_tap_without_row "help tap" && { grep -qx 'APP help tap none' <<<"$out" || { EVID="$(grep '^APP' <<<"$out")"; fail t6_help_tap_without_row; }; }
nowrun t6_settings_rows_count "$ALLROWS" && {
    [ "$(rows_total)" = 23 ] || { EVID="$(grep '^APP settings rows' <<<"$out")"; fail t6_settings_rows_count; }
    ! grep '^APP settings row=' <<<"$out" | grep -q '說明與設定頁 QR\|Help and settings QR' || { EVID="$(grep '^APP settings row=' <<<"$out")"; fail t6_settings_no_qr_row; }; }
# `settings key <name>` taps the row of that name: every T6 key exists and an unknown one says so
nowrun t6_settings_keys "setui; @0.3; settings key sleep; settings key theme; settings key style; settings key bright; settings key haptic; settings key toysfx; settings key usb; settings key rescan; settings key nosuch" && {
    [ "$(grep -c '^APP settings key unknown' <<<"$out")" = 1 ] && grep -qx 'APP settings key unknown nosuch' <<<"$out" || { EVID="$(grep '^APP settings key' <<<"$out")"; fail t6_settings_keys; }; }
# the popring's help cell: the last cell (after the def's own four), never armed; its pick closes the ring and opens the explain page
nowrun t6_popring_help_cell "uipush poptest; popring -2; popring 4; @0.3; uitop; popring -5" && {
    grep -qx 'APP popring open=1 page=0 cells=5 armed=-1 centre=Close' <<<"$out" || { EVID="$(grep '^APP popring' <<<"$out")"; fail t6_popring_cells; }
    ! grep -q '^APP popring .* armed=4' <<<"$out" || { EVID="$(grep '^APP popring' <<<"$out")"; fail t6_popring_help_cell_armed; }
    [ "$(tp)" = "explain " ] && grep -q '^APP popring open=0' <<<"$out" || { EVID="top $(tp); $(grep '^APP popring' <<<"$out" | tr '\n' '|')"; fail t6_popring_help_cell; }; }
# the explain page scrolls with the knob, one row per detent
LSTEP=52   # UI_ROW_H 48 + UI_ROW_GAP 4 (ui_geom.h): one knob detent
nowrun t6_explain_knob_scrolls "screen on; vol 0; uipush help; @0.3; simknob -1; @0.2; uiboxes; simknob 3; @0.3; uiboxes" && {
    d1=$(sed -n '1,/^APP uiboxes end/p' <<<"$out"); d2=$(sed '1,/^APP uiboxes end/d' <<<"$out")
    y1=$(yofnote "$d1"); y2=$(yofnote "$d2"); d=$((y1 - ${y2:-9999}))
    [ -n "$y1" ] && [ "$d" -ge $((3 * LSTEP - 1)) ] && [ "$d" -le $((3 * LSTEP + 1)) ] || { EVID="first note y $y1 -> $y2 (want $((3 * LSTEP)))"; fail t6_explain_knob_scrolls; }; }
# fix round 1 (review of T6)
# the explain page's last visible note ends above the Back button after the knob has gone to the end (the old help page's "text above Back" check). The check is a function so the test can also
# prove it fails: the same dump with Back moved up must be refused.
nowrun t6_explain_back_below "screen on; vol 0; uipush help; @0.3; simknob -1; @0.5; simknob 99; @0.3; uiboxes clip" && {   # the first knob turn may wake the screen: the next 400 ms of turns are dropped (pc_wake.c), hence the wait
    last_above_back "$out" || { EVID="$(grep '^APP uibox' <<<"$out" | tail -3 | tr '\n' '|')"; fail t6_explain_back_below; }
    ! last_above_back "$out" -200 || { EVID="the check accepted Back moved 200 px up"; fail t6_explain_back_below_can_fail; }; }
# the same on the long fixture: one wrapped label far taller than the list. The knob must bring its REAL end into view (raw bottom above Back and inside the list)
# and turning back must bring its raw top to the list top again. Fields of a label line: 5 6 7 = raw y1 x2 y2, 9 11 = clipped y1 y2 (the list's top and bottom for a clipped label).
nowrun t6_explain_back_below_long "screen on; vol 0; help open longtest; @0.3; simknob -1; @0.5; uiboxes clip; simknob 99; @0.3; uiboxes clip; simknob -99; @0.3; uiboxes clip" && {
    d1=$(sed -n '1,/^APP uiboxes end/p' <<<"$out"); r=$(sed '1,/^APP uiboxes end/d' <<<"$out"); d2=$(sed -n '1,/^APP uiboxes end/p' <<<"$r"); d3=$(sed '1,/^APP uiboxes end/d' <<<"$r")
    tl() { grep '^APP uibox label ' <<<"$1" | tail -1; }            # the last label (the long body)
    bt() { echo 294; }                                                # GS: no Back button: where it was
    set -- $(tl "$d1" | cut -d" " -f1-11); [ "$7" -gt "${11}" ] || { EVID="start: $(tl "$d1" | cut -c1-60)"; fail t6_long_taller_than_list; }
    set -- $(tl "$d2" | cut -d" " -f1-11); [ "$7" -le "${11}" ] && [ "$7" -lt "$(bt "$d2")" ] || { EVID="end: $(tl "$d2" | cut -c1-60) back=$(bt "$d2")"; fail t6_explain_back_below_long; }
    set -- $(tl "$d3" | cut -d" " -f1-11); [ "$5" = "$9" ] && [ "$7" -gt "${11}" ] || { EVID="back at top: $(tl "$d3" | cut -c1-60)"; fail t6_long_scrolls_back; }; }
# a body longer than the buffer ends on a whole character, with one visible note; the text is valid UTF-8 and the note is the last label
nowrun t6_help_cut_whole_char "help open longtest; @0.3; help show; uiboxes" && {
    grep -qx 'APP explain id=longtest title=Long paras=1 qr=0 web=0' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail t6_help_cut_paras; }
    L=$(grep '^APP uibox label ' <<<"$out")
    [ "$(tail -1 <<<"$L" | sed 's/^[^|]*|//')" = "(text too long)" ] || { EVID="last label: $(tail -1 <<<"$L" | cut -c1-80)"; fail t6_help_cut_note; }
    sed -n '2,$p' <<<"$(sed 's/^[^|]*|//' <<<"$L" | sed '$d')" | grep -q 'a' && printf '%s' "$(sed 's/^[^|]*|//' <<<"$L" | sed '$d' | tail -1)" | iconv -f UTF-8 -t UTF-8 >/dev/null 2>&1 || { EVID="not valid UTF-8 or no body label"; fail t6_help_cut_utf8; }; }
# opening 說明 while a settings row holds the knob ends that borrow: back on the settings page the knob scrolls, it does not step brightness
nowrun t6_help_ends_borrow "screen on; seti brightness 60; setui; @0.3; settings key bright; settings key bright; cfg brightness; help tap; @0.3; back; @0.3; simknob 2; cfg brightness" && {
    [ "$(cv brightness)" = "60|65|65|" ] || { EVID="brightness $(cv brightness)"; fail t6_help_ends_borrow; }; }
# a full def (14 own cells) keeps its help cell: it gives up its last own cell, says so, and the help cell opens the explain page
nowrun t6_popring_full_def_help "uipush poptest; poptest4; popring -5; popring 14; @0.3; uitop" && {
    grep -q '^APP popring open=1 page=0 cells=15 ' <<<"$out" && [ "$(tp)" = "explain " ] || { EVID="$(grep '^APP popring\|^APP uitop' <<<"$out" | tr '\n' '|')"; fail t6_popring_full_def_help; }; }

# --- fx apps T7 ---
# the 音效 menu cell, the filter list (three looks, family headers) and the filter page generated from the parameter table. No CJK font in the simulator's test root: the English texts.
nowrun t7_fx_state_ids "fx state"; H7=$(sed -n 's/^APP fx app=\([a-z_]*\) cls=heavy.*/\1/p' <<<"$out" | head -1); APPS7=$(sed -n 's/^APP fx app=\([a-z_]*\) .*/\1/p' <<<"$out" | sort -u)
nowrun t7_menu_fx_cell "screen on; @0.5; uipush menu; ring; simknob 1; @0.5; simtap 40 180; @0.2; simtap 180 180; @0.3; uitop" && {
    grep -q '^APP ring .* n=12$' <<<"$out" && grep -qx 'APP menu pick=9' <<<"$out" && [ "$(tp)" = "fx " ] || { EVID="$(grep '^APP ring\|^APP menu\|^APP uitop' <<<"$out" | tr '\n' '|')"; fail t7_menu_fx_cell; }; }
nowrun t7_fx_list_looks "fx on loudness 1; fx on $H7 1; uipush fx; @0.3; fxui rows; fx mode 2; @0.3; fxui rows" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); d2=$(sed '1,/^APP fxui end/d' <<<"$out")
    r() { grep "^APP fxui row=[0-9]* look=[a-z]* text=$2" <<<"$1" | head -1; }
    { [ "$(grep -c '^APP fxui header text=' <<<"$d1")" -ge 5 ] && grep -qx 'APP fxui header text=Gain' <<<"$d1" \
      && r "$d1" 'Loudness  on$' | grep -q ' look=on ' && r "$d1" 'Gain  off$' | grep -q ' look=normal ' \
      && [ "$(grep ' text=' <<<"$d1" | tail -1 | grep -c 'look=normal text=Help$')" = 1 ] \
      && grep -q ' look=dim text=.*  on, paused$' <<<"$d2" && r "$d2" 'Gain  off$' | grep -q ' look=normal ' && r "$d2" 'Loudness  on$' | grep -q ' look=on '; } || { EVID="$(sed 's/^APP fxui //' <<<"$d1" | tr '\n' '|' | cut -c1-500) || $(grep dim <<<"$d2")"; fail t7_fx_list_looks; }; }
nowrun t7_fx_alloc_fail_row "fx failalloc 1; fx on freeverb 1; @0.3; fxpage freeverb; @0.3; fxui rows; back; @0.2; uipush fx; fxui rows" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); d2=$(sed '1,/^APP fxui end/d' <<<"$out")
    { grep -q '^APP fxui row=0 look=dim text=On, paused: Out of memory$' <<<"$d1" && grep -q '^APP fxui row=[0-9]* look=dim text=.*  on, paused$' <<<"$d2" && [ "$(grep -c ' look=dim ' <<<"$d2")" = 1 ]; } || { EVID="$(sed 's/^APP fxui //' <<<"$out" | grep 'dim\|row=0 ' | tr '\n' '|')"; fail t7_fx_alloc_fail_row; }; }
nowrun t7_fxapp_toggle "fxpage compressor; @0.3; fxui tap 0; fx state; fxui rows; fxui tap 0; fx state" && {
    s1=$(grep '^APP fx app=compressor ' <<<"$out" | sed -n 1p); s2=$(grep '^APP fx app=compressor ' <<<"$out" | sed -n 2p)
    { [ "$(fxval "$s1" on)" = 1 ] && [ "$(fxval "$s2" on)" = 0 ] && grep -q 'row=0 look=on text=On$' <<<"$(fxrows)"; } || { EVID="$(grep '^APP fx app=compressor' <<<"$out" | cut -c1-60 | tr '\n' '|') $(fxrows | head -3 | tr '\n' '|')"; fail t7_fxapp_toggle; }; }
nowrun t7_fxapp_axis "screen on; @0.5; fxpage compressor; @0.3; fxui tap 2; fxui tap 2; fx state; simknob -3; fx state; @4.5; uiboxes; simknob 1; @0.2; uiboxes; fx state; fxui rows" && {
    v=$(grep '^APP fx app=compressor ' <<<"$out" | sed 's/.* threshold_db=\([^ ]*\).*/\1/' | tr '\n' ' ')
    [ "$v" = "-23 -26 -26 " ] || { EVID="threshold_db $v"; fail t7_fxapp_axis; }
    b1=$(sed -n '1,/^APP uiboxes end/p' <<<"$out"); b2=$(sed '1,/^APP uiboxes end/d' <<<"$out" | sed -n '1,/^APP uiboxes end/p')
    { [ -n "$(tyof "$b1")" ] && [ -n "$(tyof "$b2")" ] && [ "$(tyof "$b2")" -lt "$(tyof "$b1")" ]; } || { EVID="threshold row y $(tyof "$b1") -> $(tyof "$b2") (the knob must scroll the page after the borrow ran out)"; fail t7_fxapp_axis_scrolls; }
    fxrows | grep -q 'row=2 look=normal text=Threshold: -26 dB$' || { EVID="$(fxrows | sed -n 1,4p | tr '\n' '|')"; fail t7_fxapp_axis_row; }; }
nowrun t7_fxapp_axis_sel "fxpage compressor; @0.3; fxui tap 2; fxui tap 2; fxui rows; @4.5; fxui rows" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); d2=$(sed '1,/^APP fxui end/d' <<<"$out")
    { grep -q 'row=2 look=sel text=Threshold: -23 dB$' <<<"$d1" && grep -q 'row=2 look=normal text=Threshold: -23 dB$' <<<"$d2"; } || { EVID="$(grep 'row=2 ' <<<"$out" | tr '\n' '|')"; fail t7_fxapp_axis_sel; }; }
nowrun t7_fxapp_tap_clamps "fx set compressor threshold_db 0; fxpage compressor; @0.3; fxui tap 2; fxui tap 2; fx state" && {
    [ "$(fxval "$(fxapp compressor)" threshold_db)" = 0 ] || { EVID="$(fxapp compressor)"; fail t7_fxapp_tap_clamps; }; }
nowrun t7_fxapp_bool_tap "screen on; @0.5; fxpage compressor; @0.3; fxui tap 8; fx state; fxui tap 8; fx state; uiboxes; simknob 1; @0.2; uiboxes; fx state" && {
    v=$(grep '^APP fx app=compressor ' <<<"$out" | sed 's/.* auto_makeup=\([^ ]*\).*/\1/' | tr '\n' ' ')
    [ "$v" = "0 1 1 " ] || { EVID="auto_makeup $v"; fail t7_fxapp_bool_tap; }
    b1=$(sed -n '1,/^APP uiboxes end/p' <<<"$out"); b2=$(sed '1,/^APP uiboxes end/d' <<<"$out" | sed -n '1,/^APP uiboxes end/p')
    { [ -n "$(tyof "$b1")" ] && [ -n "$(tyof "$b2")" ] && [ "$(tyof "$b2")" -lt "$(tyof "$b1")" ]; } || { EVID="threshold row y $(tyof "$b1") -> $(tyof "$b2") (a plain tap row does not borrow: the knob scrolls)"; fail t7_fxapp_bool_tap_scrolls; }; }
nowrun t7_fxapp_reset "fx on compressor 1; fx set compressor threshold_db -40; fxpage compressor; @0.3; fxui tap 9; fxui rows; fx state; fxui tap 9; fx state; fxui rows" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); d2=$(sed '1,/^APP fxui end/d' <<<"$out")
    s1=$(grep '^APP fx app=compressor ' <<<"$out" | sed -n 1p); s2=$(grep '^APP fx app=compressor ' <<<"$out" | sed -n 2p)
    { grep -q 'row=9 look=[a-z]* text=Tap again: back to defaults$' <<<"$d1" && [ "$(fxval "$s1" threshold_db)" = -40 ] && [ "$(fxval "$s2" threshold_db)" = -24 ] && [ "$(fxval "$s2" on)" = 1 ] && grep -q 'row=9 look=[a-z]* text=Reset$' <<<"$d2"; } || { EVID="$(grep 'row=9 ' <<<"$out" | tr '\n' '|') $(fxval "$s1" threshold_db) $(fxval "$s2" threshold_db)"; fail t7_fxapp_reset; }; }
nowrun t7_fxapp_reset_times_out "fx set compressor threshold_db -40; fxpage compressor; @0.3; fxui tap 9; @4.5; fxui tap 9; fx state; fxui rows" && {
    { [ "$(fxval "$(fxapp compressor)" threshold_db)" = -40 ] && grep -q 'row=9 look=[a-z]* text=Tap again: back to defaults$' <<<"$(fxrows)"; } || { EVID="$(fxapp compressor | cut -c1-80)"; fail t7_fxapp_reset_times_out; }; }
nowrun t7_fx_eq_rows "fx import warm.toml; fxpage equalizer; @0.3; fxui rows" && {
    d=$(fxrows)
    { [ "$(grep -c '^APP fxui header text=' <<<"$d")" = 2 ] && grep -qx 'APP fxui header text=Band 1' <<<"$d" && grep -qx 'APP fxui header text=Band 2' <<<"$d" && [ "$(grep -c 'text=\(type\|freq\|gain\|Q\): ' <<<"$d")" = 8 ]; } || { EVID="$(sed 's/^APP fxui //' <<<"$d" | tr '\n' '|')"; fail t7_fx_eq_rows; }; }
nowrun t7_fx_eq_count_rebuild "screen on; @0.5; fx import warm.toml; fxpage equalizer; @0.3; fxui rows; fxui tap 3; fxui tap 3; fxui rows; @4.5; fxui rows; uitop" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); r=$(sed '1,/^APP fxui end/d' <<<"$out"); d2=$(sed -n '1,/^APP fxui end/p' <<<"$r"); d3=$(sed '1,/^APP fxui end/d' <<<"$r")
    c3=$(grep -c '^APP fxui header text=' <<<"$d3")
    { [ "$(grep -c '^APP fxui header' <<<"$d1")" = 2 ] && [ "$(grep -c '^APP fxui header' <<<"$d2")" = 2 ] && [ "$c3" = 3 ] && grep -qx 'APP fxui header text=Band 3' <<<"$d3" && [ "$(tp)" = "fxapp " ]; } || { EVID="headers $(grep -c header <<<"$d1") $(grep -c header <<<"$d2") $c3 top $(tp)"; fail t7_fx_eq_count_rebuild; }; }
nowrun t7_fxapp_leave_back "screen on; @0.5; uipush menu; fxpage compressor; @0.3; fxui tap 2; fxui tap 2; back; @0.5; simknob 1; @0.2; ring; fx state" && {
    { [ "$(fxval "$(fxapp compressor)" threshold_db)" = -23 ] && grep -q '^APP ring page=1/2 ' <<<"$out"; } || { EVID="$(fxapp compressor | cut -c1-90) $(grep '^APP ring' <<<"$out")"; fail t7_fxapp_leave_back; }; }   # the turn reached the menu (page 2), not a left-over borrow
nowrun t7_fxapp_leave_home "screen on; @0.5; uipush menu; fxpage compressor; @0.3; fxui tap 2; fxui tap 2; uihome; @0.5; simknob 1; @0.2; ring; fx state; uitop" && {
    { [ "$(fxval "$(fxapp compressor)" threshold_db)" = -23 ] && [ "$(tp)" = "menu " ] && grep -q '^APP ring page=1/2 ' <<<"$out"; } || { EVID="$(fxapp compressor | cut -c1-90) top $(tp) $(grep '^APP ring' <<<"$out")"; fail t7_fxapp_leave_home; }; }
nowrun t7_fxapp_leave_screen_off "screen on; @0.5; fxpage compressor; @0.3; fxui tap 2; fxui tap 2; screen off; @0.3; screen on; @0.8; uiboxes; simknob 1; @0.2; uiboxes; fx state" && {
    b1=$(sed -n '1,/^APP uiboxes end/p' <<<"$out"); b2=$(sed '1,/^APP uiboxes end/d' <<<"$out" | sed -n '1,/^APP uiboxes end/p')
    { [ "$(fxval "$(fxapp compressor)" threshold_db)" = -23 ] && [ -n "$(tyof "$b1")" ] && [ "$(tyof "$b2")" -lt "$(tyof "$b1")" ]; } || { EVID="$(fxapp compressor | cut -c1-90) y $(tyof "$b1") -> $(tyof "$b2")"; fail t7_fxapp_leave_screen_off; }; }
nowrun t7_fxapp_theme_rebuild "screen on; @0.5; fxpage compressor; @0.3; fxui tap 2; fxui tap 2; theme next; @0.5; fx state; uitop; fxui rows" && {
    { [ "$(fxval "$(fxapp compressor)" threshold_db)" = -23 ] && [ "$(tp)" = "fxapp " ] && fxrows | grep -q 'row=2 .*text=Threshold: -23 dB$'; } || { EVID="$(fxapp compressor | cut -c1-90) top $(tp)"; fail t7_fxapp_theme_rebuild; }; }
# the equalizer rows follow the band count: the borrow ends while 說明 covers the page (tap 頻段數, 說明 within 4 s, Back): the page rebuilds when it is top again, and a later unrelated borrow does not rebuild it
nowrun t7_fx_eq_covered_rebuild "screen on; @0.5; fxpage equalizer; @0.3; fxui rows; fxui tap 3; fxui tap 3; fxui tap -1; @0.5; back; @0.8; fxui rows; fxui tap 1; @4.5; fxui rows; uitop" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); r=$(sed '1,/^APP fxui end/d' <<<"$out"); d2=$(sed -n '1,/^APP fxui end/p' <<<"$r"); d3=$(sed '1,/^APP fxui end/d' <<<"$r")
    pushes=$(grep -c '^APP screen push=fxapp ' <<<"$out")
    { [ "$(grep -c '^APP fxui header' <<<"$d1")" = 0 ] && [ "$(grep -c '^APP fxui header' <<<"$d2")" = 1 ] && grep -q 'text=Bands: 1$' <<<"$d2" && [ "$(grep -c 'text=\(type\|freq\|gain\|Q\): ' <<<"$d2")" = 4 ] && [ "$pushes" = 2 ] && [ "$(grep -c '^APP fxui header' <<<"$d3")" = 1 ] && [ "$(tp)" = "fxapp " ]; } || { EVID="headers $(grep -c header <<<"$d1") $(grep -c header <<<"$d2") $(grep -c header <<<"$d3") fxapp pushes $pushes (want 2: the page and its one rebuild) top $(tp)"; fail t7_fx_eq_covered_rebuild; }; }
# reset on the equalizer (count back to 0) and a count changed from outside (import) rebuild through the same place
nowrun t7_fx_eq_reset_rebuild "screen on; @0.5; fx import warm.toml; fxpage equalizer; @0.3; fxui rows; fxui tap -2; fxui tap -2; @0.8; fxui rows" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); d2=$(sed '1,/^APP fxui end/d' <<<"$out")
    { [ "$(grep -c '^APP fxui header' <<<"$d1")" = 2 ] && [ "$(grep -c '^APP fxui header' <<<"$d2")" = 0 ] && ! grep -q 'text=\(type\|freq\|gain\|Q\): ' <<<"$d2" && grep -q 'text=Bands: 0$' <<<"$d2"; } || { EVID="headers $(grep -c header <<<"$d1") -> $(grep -c header <<<"$d2") $(grep 'Bands' <<<"$d2")"; fail t7_fx_eq_reset_rebuild; }; }
nowrun t7_fx_eq_import_rebuild "screen on; @0.5; fxpage equalizer; @0.3; fxui rows; fx import warm.toml; @0.8; fxui rows" && {
    d1=$(sed -n '1,/^APP fxui end/p' <<<"$out"); d2=$(sed '1,/^APP fxui end/d' <<<"$out")
    { [ "$(grep -c '^APP fxui header' <<<"$d1")" = 0 ] && [ "$(grep -c '^APP fxui header' <<<"$d2")" = 2 ]; } || { EVID="headers $(grep -c header <<<"$d1") -> $(grep -c header <<<"$d2")"; fail t7_fx_eq_import_rebuild; }; }
# a console value change refreshes an open filter page (fx_set notifies)
nowrun t7_fxapp_set_refreshes "fxpage compressor; @0.3; fx set compressor threshold_db -40; @0.3; fxui rows" && {
    fxrows | grep -q 'row=2 look=normal text=Threshold: -40 dB$' || { EVID="$(fxrows | sed -n 3p)"; fail t7_fxapp_set_refreshes; }; }
# (the toy sound effects' on/off row 小工具音效 / Toy sounds, sfx task, is not the old sound-preset row: left out of the search)
nowrun t7_settings_no_sound_row "$ALLROWS; settings key sound" && {
    { [ "$(rows_total)" = 23 ] && ! grep '^APP settings row=' <<<"$out" | grep -v 'Toy sounds\|小工具音效' | grep -qi 'sound\|音效' && grep -qx 'APP settings key unknown sound' <<<"$out"; } || { EVID="$(grep '^APP settings row' <<<"$out" | head -3 | tr '\n' '|')"; fail t7_settings_no_sound_row; }; }
nowrun t7_fx_help_body "fxpage compressor; @0.3; help tap; @0.3; help show" && {
    grep -qx 'APP explain id=fxapp title=Compressor paras=9 qr=0 web=0' <<<"$out" || { EVID="$(grep '^APP explain' <<<"$out")"; fail t7_fx_help_body; }; }
nowrun t7_fx_help_list "uipush fx; @0.3; fxui tap -1; @0.3; help show; uitop" && {
    { grep -q '^APP explain id=fx title=\(Sound FX\|Help\) paras=[1-9]' <<<"$out" && [ "$(tp)" = "explain " ]; } || { EVID="$(grep '^APP explain\|^APP uitop' <<<"$out")"; fail t7_fx_help_list; }; }
# every app's generated help: one paragraph for the description + one per row; no device word (旋鈕 / knob), nothing cut
CMDS7=; for a in $APPS7; do CMDS7="${CMDS7}fxpage $a; @0.2; help tap; @0.2; help show; uiboxes; back; back; "; done
nowrun t7_fx_help_all "$CMDS7" && {
    n=$(grep -c '^APP explain id=fxapp ' <<<"$out")
    { [ "$n" -ge 13 ] && ! grep '^APP uibox label ' <<<"$out" | grep -qi 'knob\|旋鈕\|text too long' && ! grep -q '^APP explain .* paras=[01] ' <<<"$out"; } || { EVID="apps $n; $(grep '^APP uibox label .*[Kk]nob' <<<"$out" | head -2)"; fail t7_fx_help_all; }; }
# the glyph check with player_core scanned: known-missing lines allowed
python3 tools/check/check_font_glyphs.py >"$T/gl.out" 2>&1 || { EVID="$(tail -3 "$T/gl.out" | tr '\n' '|')"; fail t7_font_glyphs; }

