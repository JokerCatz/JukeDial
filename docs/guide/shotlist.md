# Guide shot list

One row per image id used by `docs/guide/{en,zh-TW,zh-CN}/*.md` and the READMEs. Files go to `docs/guide/<lang>/img/<id>.png`; after every run `tools/simtools/guide_share.py` moves each image that is byte-identical in all three languages to `docs/guide/img/` (decided by content, not by the Lang column below) and rewrites the links to `../img/<id>.png`. An image identical in only two languages stays per language.

How to shoot in the simulator: round board `make sim` (360 x 360), FakePod `make sim-fp` (460 x 460, `SIM_FP=1` for `tools/simtools/ui_baseline.py`). Headless: `sim/build/sim --headless --root DIR --cmd "<cmds>; shot out.png"` (see `sim/README.md`). Set language with `lang tw|cn|en`. "needs a sim command" = no existing command reaches that state; add one (or shoot on the board).
Columns: Boards = R round, F FakePod. Lang = L language-dependent (one per language), N same for all.

| id | Boards | Lang | Page / scene | EXACT state wanted | How to reach it |
|---|---|---|---|---|---|
| hero-boards | R+F | N | photo | both devices side by side on a desk, home menu lit, same album art | real photo (not simulator) |
| hero-round | R | N | photo/render | round board, now-playing with a colourful cover | photo, or sim `vol 0`, `track 1`, `uipush now`, `shot` |
| hero-fakepod | F | N | photo/render | FakePod now-playing, same cover | photo, or `make sim-fp` with the same commands |
| langpick | R | N | language picker | three rows 繁體中文 / 简体中文 / English, none selected | `uipush langpick` (`lang tw` first) |
| tour-step | R+F | L | gesture tour step 1 | big flick-up arrow picture and its one-line text | needs a sim command (`uipush tour` or similar; `ui_tour.c` exists) |
| home-round | R | L | home menu | ring page 1, no selection, clock-friendly time | `uipush menu` |
| home-fakepod | F | L | home menu | 2 x 4 grid page 1 | `make sim-fp`, `uipush menu` |
| gesture-flick-up | R | N | drawn arrows | finger path up over a blurred content page, short motion streak | annotate `uipush now` shot; sim `simflick up` for the state after |
| gesture-flick-down | R | N | drawn arrows | same, downward | annotate shot; `simflick down` opens the quick panel |
| gesture-flick-lr | R | N | drawn arrows | two arrows left and right over a ring page | annotate `uipush menu` shot; `simflick left|right` |
| gesture-drag | R | N | drawn arrows | slow finger path on the help page, scroll bar visible | annotate `uipush help` shot; `simdrag x1 y1 x2 y2 ms` |
| gesture-hold | R | N | drawn arrows | finger with a 3-second ring filling | annotate any page; no sim visual of the hold ring (needs a sim command if a ring is shown) |
| page-bubble | R+F | N | ring/list with page bubble | bubble "2/3" visible at the ring's bottom gap (R) / the 2x2 junction (F) | `uipush menu`, `screen on`, `knob 1` or `simflick left` |
| now-playing | R+F | L | now playing | MP3 mid-song, cover shown, progress about 40 %, title + artist | `vol 0`, `track 1`, `@1`, `uipush now` (see ui_baseline `lang_cn_now`) |
| library | R+F | L | track list | first folder, 5 rows with the favourite star | `uipush tracks0` (or `list`) |
| radio-list | R+F | L | radio list | 5 stations, one playing, last row "edit on a computer" | `uipush radio` |
| bt-devices | R+F | L | Bluetooth page | list with two paired devices, one connected, codec line (AAC) | `btui`, `simbt` commands (see `sim/fake_bt_svc.c`) |
| cast-broadcast | R | L | Auracast page | broadcasting, 48 kHz stereo line, Wi-Fi greyed with the reason | needs a sim command (`fake_cast_mode.c` exists; page name TODO) |
| cast-radio-mode | R | L | broadcast-radio boot mode | a station playing, broadcast status, armed "back to player" cell | `sim --mode cast` if present, otherwise needs a sim command |
| clock-face | R+F | L | clock | analog face, 10:08:35 (use `simtime 2026-11-16 10:08:35`) | `simtime ...`, then the clock page (needs the page name; `scr_clock.c`) |
| alarm | R | L | alarm page | one alarm on, 07:30, tone "built-in chime" | needs the page command (`scr_clock.c` alarm) |
| pomodoro | R | L | pomodoro ring | 25-minute ring 40 % used | needs the page command |
| drink-habits | R+F | L | drink / habits | habit list with 3 of 5 ticked | needs the page commands (`scr_drink.c`, `scr_habits.c`) |
| toys-menu | R+F | L | tools menu | the list of toys, first page | `tools` (ui_baseline "tools") |
| game-breakout | R+F | L | Ring breakout, in play | mid-play, ball in flight, score 1200, 3 lives | `games mode 0`, `games style 0`, `games rule 1`, then breakout page (see `BR` list in ui_baseline.py); needs a mid-play command if not reachable |
| game-safe | R+F | L | Safe cracker | dial between two digits, two of three locks open | needs a mid-play command (`scr_game_safe.c`) |
| game-tower | R+F | L | Stack tower | 8 blocks stacked, a wobble, score visible | needs a mid-play command |
| game-spiro | R+F | L | Spirograph | a finished rosette, 3 colours | needs a mid-play command |
| instrument-play | R+F | L | instrument app play page | drum pads page, one pad lit | `sim --mode inst` (see ui_baseline `inst_*`) |
| ambient-lava | R+F | N | ambient lava lamp | blobs in 3 colours, no text | needs the ambient command (`scr_ambient.c`) |
| noise-colors | R+F | L | white-noise colours | colour list, one selected, volume bar | `noiseui`, `uipush colors` |
| breath | R+F | L | breathing | circle at full inhale, caption | `uipush breath` |
| quick-settings | R+F | L | quick-settings panel over now-playing | brightness 60, volume 12, Bluetooth on, Wi-Fi allowed, airplane off | `uipush now`, `simflick down` |
| settings | R+F | L | settings | first page of the settings ring | `setui` |
| card-layout | - | L | diagram | folder tree: top-level user folders and `_player/` with config, fonts, sleep, index | to be drawn (diagram), text from chapter 12 |
| web-settings | - | L | web tool, Device tab | connected, device status, a few wired settings | real browser against the peer board (or the mock serial); tab 裝置/Device |
| web-stations | - | L | web tool, Radio tab | list of 6 stations with a preview button | browser, Radio tab |
| web-fonts | - | L | web tool, Fonts tab | charset loaded, size bar under the 4 MB limit, three output files | browser, Fonts tab (needs the library charset) |
| web-flash | - | L | web tool, Flash tab | board chosen, checks green, backup done, progress 60 % | browser, Flash tab with the mock port or the peer board; needs the board in hand |
| ext-mode | R+F | L | external mode page | mode name, USB/BLE state, the armed exit cell | `sim --mode ext` |
| safe-mode | R | N | safe mode screen | the static three-line black screen | console `safeshow` on the board (a photo of the real screen) |
| btaudio-page | R+F | L | Bluetooth audio page (chapter 17) | fake AAC/SBC headset connected, live line, codec/rate rows | `tools/simtools/guide_shots.py` (`simbt mode 2`, `simbt caps aac,sbc`, `simbt connect`, `btaudio open`) |
| btaudio-info | R+F | L | same page scrolled to its info lines | "Headset max" and "Actual" lines | same plus `knob 8` |
| wired-page | R+F | L | Wired output page | a 96 kHz file playing, live line, wired rows | `mp3ui`, `simaudiorate 96000 1`, `mp3 1`, `wired open` |
| details-out-wired | R+F | L | Details page, Output block, wired | halved / passthrough line with its reason | `uipush meta`, `knob 8` after the above |
| details-out-bt | R+F | L | Details page, Output block, Bluetooth | same over the fake headset (`simsink 1`) | see `S` in `tools/simtools/guide_shots.py` |
| popup-card | R+F | L | popup card over a game | Ring breakout paused-style popup, solid rounded card | `games open breakout`, `simflick up` |
| theme-gallery | R+F | L | ten card themes + default on one image | home page per theme, names per language | `guide_shots.py --compose` (`theme_sheets.run`); tiles show the zh-TW home labels |
| icon-sheet | - | N | menu icon map | every menu icon, round and square cell | `make icons-sheet ICON_SHEETS=dir`, then `ICON_SHEET_DIR=dir guide_shots.py --compose` |
| now-info | F | L | FakePod now playing, style cover plus info | cfg `now_fp 0`, MP3 mid-song | `cfgset now_fp 0`, then the now-playing commands |
| now-disc | F | L | FakePod now playing, style menu grid (disc) | cfg `now_fp 2` | `cfgset now_fp 2`, then the now-playing commands |
| ninegrid-cover | F | L | nine-grid labels shown over the full cover | first tap done | now-playing commands, `simtap 230 230` |
| ninegrid-viz | F | L | nine-grid labels shown over the spectrum page | first tap done | `viz bars`, `simtap 230 230` |
| volume-pill | R+F | L | volume indicator after a knob turn | arc (R) / pill (F) visible | `SIM_SHOT_TOP=1`, now-playing commands, `knob 8` |
| settings-sound | R+F | L | settings, Sound category page | six category rows are on `settings` | `setui`, `settings group sound` |
| settings-value | R+F | L | a selected value row with - and + | brightness row selected | `setui`, `settings key bright` |
| timeset | R+F | L | Set time page | five rows, Apply, note | `setui`, `settings key time` |
| cardtest | R+F | L | card diagnostics after a raw read and a file test | two scores | `simsd speed 350 800 400 200`, `settings key cardtest`, `cardtest raw`, `cardtest tap file` x2, `knob 4` |
| cardtest-ladder | R+F | L | the same page scrolled to the format ladder | verdict icons | as above, `knob 14` |
| game-maze-gravity | R | L | Knob toys, gravity maze | marble in play | `games open knobtoy`, `games rule 2` |
| game-maze-tilt | F | L | Knob toys, tilt maze | marble in play | `maze grav 60 20`, `games rule 5` |
| usb-lost | R | L | drive mode after the computer vanished | "USB dropped, card released" | `sim --mode ext`, `simusb mount`, `simusb lost`, `ext ask no` |
| clock-default | R+F | N | the default clock face (HUD) | needs the card pack's faces.zip | `cfgdel clockface`, `clock` |

Count: 63 ids (49 + the 14 above; `drink` and `hero-now` were dropped, the guide no longer shows them). Round+square ids are written as `<id>_round.png` / `<id>_square.png`, and the chapters embed both. The chapters now reference every id by those names; `hero-boards` (a real photo) is replaced in the guide by the simulator render `hero-screens`.
