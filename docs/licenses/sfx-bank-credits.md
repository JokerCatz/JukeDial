# Sound bank credits (app/main/sfx_bank.def, sfx task 2, 2026-11-14)

Every entry of the toy sound bank names its source on its `@entry` line (the last word: a `@credit` key). The sounds are parameters,
played by this project's own chip synth (`components/player_core/src/pc_sfx.c`); no sound file and no third-party code is in the firmware.

| Credit key | Source | Licence | Entries |
|---|---|---|---|
| `original` | written for this project | the project's own | tap, back, error, jackpot, jiao_sheng, jiao_xiao, jiao_yin, move, lose, draw, combo, level_up, extra_life, ready, warp, sparkle, gold (G1 fix round 1: the golden game's start, a sine-wave chime over a pulse arpeggio), bell, chime, buzz, engine, snare, step, siren, organ |
| `task1` | sfx Task 1's first sketches (this project), moved into the bank | the project's own | ok, fail, pop, pop_big, tick, stop, win, clack, dice |
| `zzfx` | ZzFX by Frank Force, README "Sample sounds" (https://github.com/KilledByAPixel/ZzFX) | MIT, `zzfx-MIT.txt` | game_over ("Game Over"), heart ("Heart") |
| `jsfxr` | jsfxr by Chris McCormick, the preset generators' mid values (https://github.com/chr15m/jsfxr); sfxr by DrPetter | public domain (Unlicense), `jsfxr-UNLICENSE.txt`; sfxr MIT | select (blipSelect), coin (pickupCoin), power_up (powerUp), jump (jump), hit (hitHurt), laser (laserShoot), boom (explosion) |

**Melodies.** The jingles (jackpot, level_up, extra_life, lose, draw, ready, the 擲筊 results, ...) are original. No commercial game melody
is copied: "extra life" is a rising A-major figure written for this bank in the spirit of a 1-up, not the Mario tune; "game over" is the
ZzFX README's own sample sound.

**Refused at import** (they need what the voices cannot do; the parser refuses them and the test checks it): ZzFX README "Piano"
(bit crush and delay) and "Drum" (phase noise).
