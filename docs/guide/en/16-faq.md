# 16. Troubleshooting and FAQ

**No sound on wired output.** Check volume and the volume limit; check whether the output is Bluetooth. A source below 32 kHz on the wired path is refused with a message. High rates (192 / 384 kHz) depend on `dac_max_rate`; 384 kHz from the card may underrun, then the default stays at 192 kHz.

**A file is skipped.** Files of 2 GiB or more are not listed. Unreadable or unsupported files are skipped to the next song. A WAV with an unusual format (not PCM / float) is refused.

**The card is not read.** GPT partition tables fail ("reason=gpt"): re-partition as MBR. exFAT works. Do not remove the card while playing.

**Radio does not play.** Wi-Fi may not be allowed (quick settings); the network file may be missing; during Auracast broadcast from the full player Wi-Fi is off by design - use broadcast-radio mode. Some https stations need free internal RAM; the page says if it refused.

**Bluetooth drops.** aptX HD / LDAC may step down when the link is weak (about 0.7-0.9 s pause per step). Connecting Wi-Fi while on headphones makes a half-second gap. LDAC is capped near 660 kbps while Wi-Fi is on.

**Effects are paused / speed is 1.00x / crossfade is off.** Intended. aptX, LDAC, Auracast and outputs above 48 kHz (88.2 kHz and up) turn heavy features off, and the page tells you why.

**The clock is wrong after power loss.** The round board has no battery clock: connect to Wi-Fi once and it syncs, or set it by hand under Settings > System > Set time (chapter 6). The FakePod has a built-in clock that keeps the time while powered off.

**A character shows as a box.** The library font set lacks it. Build a font set with the web tool for your library (chapter 13).

**Screen is stuck.** Hold for 3 seconds to go Home. Still stuck: unplug and replug. Crashes three times in a row lead to safe mode (chapter 15).

**Honest limits.** Wi-Fi off during broadcast; no AirPlay; no Wi-Fi setup on the device screen.

**The sound is lower quality than the file.** Open Details and read the Output block: it names what was changed and who decided (the setting, the headset, the DAC...). Chapter 17.

**The card fails to mount at boot, saying "Card not responding properly".** Usually the card got stuck after USB drive mode was unplugged roughly: on the round board switch off and on with the power switch; on the FakePod re-insert the card or power fully off and on (chapter 14).

**The sound level differs after switching to speaker mode or another headset.** Each output remembers its own volume (chapters 5 and 10): wired, each headset, speaker mode.

**Crash, then Bluetooth is off.** After any crash the next boot resets the Bluetooth mode to off on purpose; switch it on again in the Bluetooth page.
