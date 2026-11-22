# SPDX-License-Identifier: LGPL-2.1-or-later
# --- FakePod battery clock (PCF85063, bsp_rtc.h) against the simulator's fake chip (sim/fake_rtc.c: SIM_STATE rtc=absent|invalid|ISO, console simrtc; wall_s=0 boots with an unset clock) ---
# boot from a valid RTC: the system clock was unset (epoch 0), the chip holds 2026-11-22 08:30:00 UTC: time(NULL) follows it, alarms and faces see a valid clock, no SNTP involved
SIM_STATE='wall_s=0;rtc=2026-11-22T08:30:00' nowrun rtc_boot_sets_clock "timenow" && {
    grep -q '^APP rtc present=1 valid=1 os=0 time=2026-11-22T08:30:00Z$' <<<"$out" || { EVID="$(grep '^APP rtc' <<<"$out")"; fail rtc_boot_line; }
    grep -q '^APP clock from rtc utc=1795336200$' <<<"$out" || { EVID="$(grep '^APP clock' <<<"$out")"; fail rtc_boot_adopts; }
    grep -q '^APP time utc=17953362[0-9][0-9] valid=1$' <<<"$out" || { EVID="$(grep '^APP time' <<<"$out")"; fail rtc_boot_clock_valid; }; }
# an RTC that lost its power (oscillator-stop flag): ignored, the clock stays unset; the line says os=1
SIM_STATE='wall_s=0;rtc=invalid' nowrun rtc_invalid_ignored "timenow" && {
    grep -q '^APP rtc present=1 valid=0 os=1 time=-$' <<<"$out" || { EVID="$(grep '^APP rtc' <<<"$out")"; fail rtc_invalid_line; }
    ! grep -q '^APP clock from rtc' <<<"$out" && grep -q '^APP time utc=[0-9]\{1,2\} valid=0$' <<<"$out" || { EVID="$(grep '^APP time\|^APP clock' <<<"$out")"; fail rtc_invalid_not_adopted; }; }
# a chip set to a date before 2025 is not adopted either
SIM_STATE='wall_s=0;rtc=2019-05-01T00:00:00' nowrun rtc_old_ignored "timenow" && {
    grep -q '^APP rtc present=1 valid=0 .*time=-$' <<<"$out" && ! grep -q '^APP clock from rtc' <<<"$out" || { EVID="$(grep '^APP rtc\|^APP clock' <<<"$out")"; fail rtc_old_not_adopted; }; }
# a clock that is already set (the default simulator wall clock) is not overwritten by the chip
SIM_STATE='rtc=2026-11-22T08:30:00' nowrun rtc_set_clock_kept "timenow" && {
    ! grep -q '^APP clock from rtc' <<<"$out" || { EVID="$(grep '^APP clock' <<<"$out")"; fail rtc_clock_kept; }; }
# no chip (the round board): nothing, and the console says so
nowrun rtc_absent "timenow; rtc; rtc set" && {
    grep -q '^APP rtc present=0 valid=0 os=0 time=-$' <<<"$out" && grep -q '^APP rtc absent$' <<<"$out" && grep -q '^APP cmd rtc set rc=-1$' <<<"$out" || { EVID="$(grep '^APP rtc\|^APP cmd' <<<"$out")"; fail rtc_absent; }; }
# a manual set writes the chip (UTC; the one-shot task runs at the next step): 2027-12-23 16:01 local = 08:01 UTC; the chip is valid afterwards
SIM_STATE='wall_s=0;rtc=invalid' nowrun rtc_manual_set_writes "clock set 2027-12-23 16:01; @0.2; simrtc" && {
    grep -q '^SIM rtc written 2027-12-23T08:01:00Z$' <<<"$out" && grep -q '^APP rtc write 2027-12-23T08:01:00Z rc=0$' <<<"$out" && grep -q '^SIM simrtc valid 2027-12-23T08:01:0[0-9]$' <<<"$out" || { EVID="$(grep '^SIM\|^APP rtc' <<<"$out")"; fail rtc_manual_writes; }; }
# a failed manual set (out of range) writes nothing
SIM_STATE='rtc=2026-11-22T08:30:00' nowrun rtc_refused_no_write "clock set 2027-02-29 10:00; @0.2" && {
    ! grep -q '^SIM rtc written' <<<"$out" || { EVID="$(grep '^SIM rtc' <<<"$out")"; fail rtc_refused_no_write; }; }
# the console `rtc set` writes the system time; `simrtc` changes the chip at run time
SIM_STATE='rtc=absent' nowrun rtc_console "simtime 2026-11-22 07:00:00; simrtc invalid; rtc; rtc set; simrtc" && {
    grep -q '^APP cmd rtc set rc=0$' <<<"$out" && grep -q '^SIM simrtc valid 2026-11-22T07:00:0[0-9]$' <<<"$out" || { EVID="$(grep '^APP\|^SIM' <<<"$out" | tail -5 | tr '\n' '|')"; fail rtc_console_set; }; }
# the Set time page's note: lost at power-off without a valid chip, kept with one
nowrun rtc_note_lost "setui; @0.3; settings key time; @0.3; timeset note" && {
    grep -q '^APP timeset note=\(關機後時間會遺失\|Lost at power-off\)$' <<<"$out" || { EVID="$(grep '^APP timeset' <<<"$out")"; fail rtc_note_lost; }; }
SIM_STATE='rtc=2026-11-22T08:30:00' nowrun rtc_note_kept "setui; @0.3; settings key time; @0.3; timeset note" && {
    grep -q '^APP timeset note=\(時間由內建時鐘保存，關機後仍會走\|Kept by the built-in clock, keeps running when off\)$' <<<"$out" || { EVID="$(grep '^APP timeset' <<<"$out")"; fail rtc_note_kept; }; }
SIM_STATE='rtc=invalid' nowrun rtc_note_invalid "setui; @0.3; settings key time; @0.3; timeset note" && {
    grep -q '^APP timeset note=\(關機後時間會遺失\|Lost at power-off\)$' <<<"$out" || { EVID="$(grep '^APP timeset' <<<"$out")"; fail rtc_note_invalid; }; }
# an SNTP sync writes the chip: the sync callback runs on the lwIP task and only flags; the next tick starts the write (simsntp fires the callback once SNTP is up)
SIM_STATE='wifi_up=1;rtc=2026-01-01T00:00:00' nowrun rtc_sntp_writes "simtime 2026-11-22 07:00:00; wifi 1; @3; simsntp; @2; simrtc" && {
    grep -q '^SIM sntp fired=1$' <<<"$out" && grep -q '^APP rtc write 2026-11-22T07:00:0[0-9]Z rc=0$' <<<"$out" && grep -q '^SIM simrtc valid 2026-11-22T07:00:0[0-9]$' <<<"$out" || { EVID="$(grep '^SIM\|^APP rtc\|^APP clock' <<<"$out" | tr '\n' '|')"; fail rtc_sntp_writes; }; }
