# Changelog

## 2026.11.22 — first public release

JukeDial is a pocket music player and desk gadget for the ESP32-S31 (ESP-IDF v6.1, LVGL 9.5). Versions are dates (`version.txt`).

### Boards
- **Round board JC3636F118** (360 x 360 touch screen, knob, haptics, LED ring): supported. This is the board the release is tested on.
- **Fake Pod Nano Pro** (called FakePod below; 460 x 460 square screen, two keys, AXP2101, CS43131 DAC): experimental. Flashed by hand with esptool or by the web tool once published; several of its functions have been written without the hardware at hand and are not all verified (see below).

### Music
- Local music from a TF card (FAT32 or exFAT, MBR) with covers, lyrics, speed 0.75x to 2x with pitch kept, crossfade, gapless playback, ReplayGain, spectrum pages.
- Formats: MP3, FLAC, Ogg Vorbis / Opus, M4A (AAC, ALAC), Musepack, TTA, WAV, AIFF; chip, tracker and game music (SID, AHX / HVL, AdLib families, tracker modules, NSF / VGM / GBS and more, PICO-8 carts).
- Internet radio over http and https; a broadcast-radio mode that sends a station out over Auracast.
- Effect apps (filters) with a fixed chain order and a limiter at the end.

### Audio output
- Wired DAC up to 384 kHz (the ceiling is a setting). Round board: software volume (the PCM5102A has no volume control). FakePod: CS43131 hardware volume, passthrough from 32 kHz, 16 to 32 kHz upsampled.
- Bluetooth headphones: SBC, AAC, aptX, aptX HD, LDAC (with a bit-rate setting and automatic fallback); speaker mode (phone to player); Auracast broadcast and listen.
- A volume per output (wired, each pair of headphones by address, speaker mode). A Details page says what was done to the sound and why.

### Apps and interface
- Clock, alarm, pomodoro, drink reminder, habits, manual time setting; white noise, ambient scenes, breathing.
- Fifteen games, toys, a tuner, an instrument app (pads, loops, chip and FM voices, kits).
- Touch gestures; knob on the round board, two keys on FakePod; three languages (Traditional Chinese, Simplified Chinese, English); ten colour themes; icons on every menu cell; a boot picture per start mode.
- Memory-card diagnostics and a card format helper that only works after a failed mount, behind several confirmations.

### External mode
- The player becomes a USB drive, a USB sound card, or a USB / Bluetooth media-key pad (a clean reboot into each mode).

### Web tool
- A browser tool (Chrome or Edge, Web Serial) for settings, station lists, library fonts and flashing. It is not published yet; `tools/web/serve.sh` runs it locally.

### Safety
- Boot-loop protection with a safe mode; crash lines with the build identifier; flash targets refuse unnamed ports and a missing flash backup.

### Known limitations
- Most of the human test checklist has not been done by a person yet: listening, phones and headphones, USB hosts, long runs, crashes and power.
- **USB drive mode:** the protections for the card (release on every exit path, release when the host disappears, the reconnect cell, honest mount-failure reasons) have not been verified on a board. Compatibility of the combined USB device with different operating systems is untested.
- **No seek:** there is no fast-forward or jump inside a track; the progress bar only shows the position. Long podcasts or audiobooks start from the beginning or from the resume position.
- **FakePod:** the headphone-detect line is not wired on the board (reads "plugged" in both states), so headphone status is unreliable; a `power pmu read failed` line appears at boot; screen start-up, battery fusion, auto power-off, low-battery and charging screens, OLED blanking, two-key download mode and square-screen spectrum frame rate are written but not verified; the `lcd_mhz` setting and the 1.73 V output level are experimental; above 96 kHz a pause can drop up to 250 ms; toy sounds are silent while paused; a 384 kHz FLAC with 32768-sample blocks is halved to 96 kHz; the boot screen was garbled in some boots before a fix (0 of 18 after).
- **Wired inline remote** (buttons on the headphone cable) is not supported.
- **Round board:** no battery chip (voltage by ADC only), no real-time clock (the time comes from the network or is set by hand, and is lost at power-off), no hardware volume.
- **Crossfade** works between tracks of the same sample rate only; a manual skip or a rate change (44.1 / 48 kHz) switches directly.
- **LDAC** only in headphones mode (not for Auracast); LDAC and aptX only at 44.1 / 48 kHz. LDAC and aptX are for personal, non-commercial use and are not certified by Sony or Qualcomm; aptX involves patents (`LICENSING.md`).
- **Auracast:** Wi-Fi is off while the full player broadcasts.
- **Radio:** https relies on TLS in external RAM and a few old Shoutcast stations do not connect.
- **Files:** files of 2 GiB or more are skipped; GPT-partitioned cards do not mount; albums do not show progressive JPEG, progressive PNG or HEIC.
- **Not included:** playlists, `.eq` import, WavPack, APE, CUE, DSD, AirPlay.
- **Experimental:** the CPU clock switching setting has only been tried on a bench board; it has not been tested with a live A2DP link, native USB or heavy load.
- **Web tool:** needs desktop Chrome or Edge; the hosted address is not decided.
- **Fonts:** the font files are not in the repository; the firmware's font partition needs the font pack from a release (without it, glyphs are missing or fall back).
- **Memory:** the default image has no spare internal RAM; new features must use external RAM.
