# 桌面模擬器（sim/）

把真的韌體 UI（`app/main/ui/*.c`）、`components/player_core` 與可攜的主程式檔連到 Linux 上，LVGL 用 `app/sdkconfig` 的設定編，
時鐘是虛擬的。

## 編與跑
- `make sim`：編（預設帶 ASan／UBSan／LSan）。需要先跑過一次 `make build`（`app/sdkconfig` 與 `app/managed_components/` 才有）。
  會建 `.local/sim/play/`。改一個 `ui/*.c` 後重編約 1 秒。
- `make sim-test`：`sim --selftest` 與 `sim/test.sh`（檢查在 `sim/tests/` 的片段檔；每個片段各自一個行程、預設同時跑 min(CPU 數, 8) 個，`SIM_TEST_JOBS=1` 回到單一行程依序跑；`make sim-test ONLY=viz` 只跑一部分；含關閉時畫面還開著的各條結束路徑、`simtime`、`sim_fixture.py`／`sim_root.py` 的防呆）。
- `make sim-base`：把模擬器自己的基準（fixture 資料、固定時鐘與種子，和 `sim-shots` 同一組 46 張）存到 `.local/sim/base/`，重構前先做一次。
- `make sim-diff [BASE=目錄]`：模擬器的整組畫面對基準逐頁比。預設基準是板上的 `.local/ui_kit/base/`；資料用 `.local/sim_root/` 的新複本（有的話，才會和板上基準同一個主題與曲庫），沒有就印 NOTICE 改用 fixture。
  每頁印 `<名> diff_px=<n> bbox=<x,y,w,h>`，差異圖（基準調暗、不同的像素紅色）在 `.local/sim/diff/`；`sim/known_diffs.txt` 列的頁只報不算失敗，其餘頁 `diff_px>0` 結束碼 1。
  只有一邊有的頁（模擬器多出來、或基準有而模擬器沒有）和「基準裡一張 PNG 都沒有」也算失敗。
  時鐘類頁（基準是在 15:02–15:05 台北時間拍的）用 `sim/diff_times.txt` 把虛擬時鐘設成同一刻。`known_diffs.txt` 與 `diff_times.txt` 只在比板上基準時讀。
  `.local/sim/base/` 存在時，`make sim-diff` 接著再比一次自己的基準（資料固定用 `sim/fixture`，差異圖在 `.local/sim/diff_own/`）：每頁都必須相同，包括 `known_diffs.txt` 豁免的 13 頁——那 13 頁對板上基準本來就不同，這一次比對才是它們唯一的回歸檢查。
  直接呼叫時：`python3 tools/simtools/sim_diff.py --base DIR`，基準不是板上那份時資料根目錄預設是 fixture（`--root` 可改）。
- `make sim-leak`：`tools/simtools/leak_cycles.py … --port sim`，`screens`、`tools`、`vocab`、`theme` 各 20 圈（先暖機 70 圈，讓快取長滿），單一模擬器行程跑完。通過＝結束碼 0、stderr 沒有 `ERROR: AddressSanitizer`／`LeakSanitizer`／`runtime error`、
  沒有 `APP ui leak`、每圈取樣的 `lv_bytes` 都相同、`simres` 印的活著的 esp_timer 數每圈都相同（`lv_bytes` 缺漏或為 0 也算失敗）。
  檢查器自己的紅燈測試在 `sim/test.sh`（`uileak`、`simleaktimer` 兩個動作故意洩漏，`leak_cycles.py` 必須失敗）。其他動作也能跑：`python3 tools/simtools/leak_cycles.py next:100 --port sim --every 1 --warm 5`（`--root DIR` 指定資料根目錄）。
  無視窗模式不碰 SDL，所以沒有抑制檔（`sim/lsan.supp` 只給開視窗用：`LSAN_OPTIONS=suppressions=sim/lsan.supp`）。
- `make sim-root`：見下「資料與狀態」。
- `make sim-run [ARGS=...]`：開視窗，資料根目錄永遠是 `.local/sim/play`（會留著；repo 的 `fonts/` 以符號連結放進它的 `_player/fonts/`）。第一次由 `.local/sim_root`（有的話）複製、否則由 `sim/fixture` 複製；
  `sim-run` 不寫 `.local/sim_root`，所以在視窗裡改主題、分數都不會動到 `sim-diff` 的參考資料。要重新播種：刪掉 `.local/sim/play`。
- 無視窗：`sim/build/sim --headless --root DIR --cmd "uipush menu; @1; shot out.png"`；沒給 `--cmd` 就從標準輸入逐行讀。
  `@秒` 讓虛擬時間前進；每個指令做完印 `SIM ok`。指令和板子的序列主控台相同（`console_exec`），另有 `simknob <n>`、`simtap <x> <y>`、`simpress <x> <y> <毫秒>`（按住再放開；超過 400 ms 是長按）、
  `simvoices off|nes|gb|spc|one|max`（假的聲部音量表，聲部種類用；`simspec` 印頻譜被要求的次數，`simallocfail <bytes>` 讓不小於該大小的 PSRAM 配置失敗）、
  `shot <檔.png>`、`simtime <YYYY-MM-DD HH:MM:SS[.mmm]>`（UTC；過 1.1 秒虛擬時間後牆鐘讀到該刻，`sim_diff.py` 用）、`quit`。板子才有的指令（`bt…`、`wifi`、`heap…`、`tasks`…）印 `SIM n/a <指令>`。
- 整組畫面：`make sim-shots`＝`python3 tools/simtools/ui_baseline.py sim .local/sim/shots`（用 `tools/simtools/sim_con.py`；先清掉輸出目錄裡的舊 PNG；`APP cmd unknown`、`APP uipush unknown`、`APP ui leak` 任何一行、或 `home()` 回不到根畫面都讓整個流程失敗（結束碼非 0）；每次從 `sim/fixture/` 的新複本開始，沒有該目錄就用空目錄；約 1.5 秒，連跑兩次 46 張逐位元相同）。
  整個模擬器流程（含最前面的 `mem`／`back`／`top`）中途當掉都印 `FAILED: …第幾個指令…`、清掉暫存根目錄、結束碼非 0。
  子行程在 `SIM ok` 之前結束會丟 `SimCrash`，訊息含第幾個指令、指令文字、結束碼與 stderr 最後 30 行；每個指令真實時間 20 秒逾時。
  `SIM_NOFONT=1`：不放字型連結（測退回 Montserrat 與 `SIM warn no CJK font`）。`SIM_FONTS=<dir>`：字型連結改指這個資料夾（例如 `make fonts-build` 的輸出 `.local/fontsets/out`；`make sim-run` 也認），不寫進 `fonts/`。字型重做與安裝見 docs/dev/fonts.md。主控台 `uimsg <文字>`（只有模擬器）開一頁自訂文字的訊息，`fontmiss` 印缺字紀錄。`SIM_CRASH_ON=<指令>`：該指令讓 sim `abort()`（測當掉的回報）。
  `SIM_TRACE=1`：背光、觸覺的最後一次設定印成 `SIM backlight N`、`SIM haptic N`。
- 視窗操作：滑鼠＝觸控、滾輪一格＝旋鈕一格（往下＝順時針）、`S` 截圖到 `.local/sim/shots/manual_<序號>.png`、`T` 換配色、`Esc`／關窗離開。
  外圈有半透明黑框標出直徑 360 的圓外，截圖不含它。
- 視窗輸入的自動測試（`sim/test.sh`，約 9 秒）：用 SDL 的 `dummy` 視訊驅動（`env -u DISPLAY SDL_VIDEODRIVER=dummy`，只有測試設，預設仍是真視窗）跑真正的 `window_loop`，
  再用只給測試用的指令 `simsdl click <x> <y>`（視窗像素，含 zoom）、`simsdl wheel <n>`（n<0＝滾輪往下＝順時針）、`simsdl key <s|t|esc>`、`simsdl close`（`SDL_WINDOWEVENT_CLOSE`，視窗的 X 鈕）、`simsdl quit`（`SDL_QUIT`） 造出 `SDL_MOUSEBUTTONDOWN／UP`、`SDL_MOUSEWHEEL`、`SDL_KEYDOWN`、`SDL_QUIT`，
  用 `SDL_PushEvent` 送進去：走的是同一條 event filter → `SDL_PollEvent` → LVGL 滑鼠處理 → `sim_input_poll`，沒有另一條假路徑。無視窗模式印 `SIM n/a simsdl …`。
  測的是：點擊（zoom 2 與 1，和無視窗 `simtap` 到同一頁）、滾輪（音量 ±2，用 `simvol` 讀回：印 `SIM vol <n>`）、`T`（配色重建、畫面不同）、`S`（`manual_1.png` 是 360×360 PNG，且和無視窗同頁截圖逐位元相同＝沒有遮罩）、`Esc` 與關窗（結束碼 0、stderr 空）。
  用 `dummy` 不用 `offscreen`：`offscreen` 在建視窗時（`SDL_CreateRenderer` 內走 EGL／libdrm）漏 2.9 KB，LeakSanitizer 會判失敗；`dummy` 不漏，所以測試不需要抑制檔。
  限制：注入的滑鼠事件不經過 SDL 內部的滑鼠狀態（`SDL_GetMouseState` 不會變），LVGL 不用它，所以不影響；但這一塊沒被測到。
  視窗模式的 stdout 導到檔案時是全緩衝，輸出要等指令做完 flush 才看得到（測試的 `wlast` 每次都送一個指令來等）。
  順帶修的兩個視窗路徑問題：(1) 真滑鼠點擊原本完全無效——`sim_input_start` 把 `lv_sdl_mouse_create()` 的讀取函式換成自己的 `ptr_read`，而 LVGL 的 `lv_sdl_mouse_handler` 靠讀取函式找 indev，換掉後找不到，事件全被丟掉；
  現在視窗模式不動它，`simtap` 改成推真的 SDL 滑鼠事件（視窗像素 = LVGL 座標 × zoom）。(2) `window_loop` 每讀一個字元就 `continue`，一次送來的多行指令會在任何輸入／計時處理之前全部跑完；現在每執行完一行就讓迴圈本體跑一次。

## 真／假／墊片
| | 檔案 |
|---|---|
| 原樣編 | `app/main/ui/*.c`（含 `font.c`）、`components/player_core/src/*.c`（除 `pc_backend_xmp.c`；靜態庫，用到才連）、`cJSON`、`psram_alloc`、`cfg`、`lowmem`、`memlog`、`stats_svc`、`sleep_timer`、`fortune_text`、`lists_ctl`、`mp3_ctl`、`player_ctl`、`chip_ctl`、`color_noise`、`library_svc`、`ctl`、`clock_svc`、`console`（`#ifndef SIM` 之外） |
| 假的（`sim/fake_*.c`，每檔開頭三行註解說明） | `audio_svc`、`bt_svc`、`wifi_svc`、`radio_svc`、`power_svc`（固定 USB 供電）、`usb_mode`、`sysmode`、`instrument`、`album_svc`、`thumb_svc`、`noise_engine`、`fx_audio`、`fontpak`、`cpu_load`、`fake_backend.c`（GME／sdec／xmp 三組解碼器表：開檔一律失敗） |
| 墊片（`sim/shim/`、`sim/shim_*.c`） | `esp_err`、`esp_log`、`esp_attr`、`esp_heap_caps`（`heap_caps_*` → libc）、`esp_random`（xorshift32，種子 `sim_seed()`）、`esp_timer`（虛擬時鐘）、`esp_lcd_types`、`sdkconfig`、`esp_lv_adapter`（只有 FreeType 三個函式，`shim_font.c` 照 adapter 的 LVGL 9 路徑呼叫 `lv_freetype_font_create_with_info`）、`esp_sntp`／`esp_netif_sntp`、`freertos/*`（單執行緒空殼）。`bsp/*.h` 用真的標頭（簽名對不上就編不過），實作在 `shim_bsp.c`：顯示鎖＝空、背光與觸覺記最後一次、旋鈕 hook 給 `sim_knob` |
| 模擬器自己的 | `main.c`（參數、顯示、主迴圈、唯一收尾）、`sim_boot.c`（照 `app_main.c` 的啟動順序）、`sim_clock.c`、`sim_fs.c`、`sim_cmd.c`、`sim_input.c`、`sim_na.c`、`sim_shot.c`、`sim_state.c`（`SIM_STATE`）、`sim_cover.c`（測試封面）、`embed.S`（7 個嵌入檔）、`selftest.c` |

FreeRTOS 墊片的一個取捨：`xTaskCreatePinnedToCore` 只把任務函式排進佇列；`sim_advance_ms` 每走完一步（`lv_timer_handler` 之後）在最上層把佇列裡的函式一個個跑到結束
（只有原樣編的 `library_svc` 用它建掃描任務，其函式以 `vTaskDelete(NULL)` 結尾，墊片中是空操作）。所以任務不會在 `lv_timer_handler` 或 esp_timer 回呼裡面跑，也不會有巢狀的計時器執行；
任務函式執行期間 `vTaskDelay` 是空操作（虛擬時間不動）。`scan` 指令的流程：建畫面 → 同一個指令的 `sim_advance_ms(0)` 跑掃描 → `ui_async` 的換庫在下一步。
不模擬排程；要真的長駐任務的檔案都是假的。`simtaskhold 1` 讓排隊的任務先不跑（`0` 放行；測「掃描在別頁蓋住時才結束」）。任務外的 `vTaskDelay` 仍是 `sim_advance_ms`。佇列（`xQueueCreate/Send/Receive`）是真的環形緩衝（單執行緒，滿了就回失敗、空了不等）。

## 來源改動（韌體）
| 檔案 | 位置 | 原因 |
|---|---|---|
| `app/main/console.h` | 新增 `console_exec(const char *line)` | 一行指令的分派入口（`{` 給 `ctl_run`，其餘給 `run`）；序列埠那條路改呼叫它，行為不變 |
| `app/main/console.c` | 新增 `console_exec`；序列埠那條路（`task()` 尾端）改呼叫它 | 同上 |
| `app/main/console.c` | 10 段 `#ifndef SIM`：驅動／加密標頭 2 段；`tasks_top`、`wget`、`enc_bench`、`heap_walk`、`trace_cmd` 一整段；`get_file`；`console_app_cmd`（樂器主控台）分支；`hapticlen`；`btscan`…`inst` 一組；`sysmode`…`tasks` 一組；`heapwalk`…`heaptrace` 一組；序列埠那一半（`put`/`pput`/`task`/`console_start`） | 只有硬體才有的指令與序列埠收發；被包掉的指令名列在 `sim/sim_na.c`，`sim` 印 `SIM n/a` |

`uintptr_t` 改動：不需要（`-Werror=int-to-pointer-cast` 全部通過）。

## 資料與狀態
- `sim/fixture/`（進 git，只有合成資料）：`python3 tools/simtools/sim_fixture.py` 生成，輸出逐位元固定（腳本重跑再 `diff -r` 相同，`sim/test.sh` 有檢查）。
  12 首假歌＝3 張專輯（夜航星圖、紙飛機工廠、山間小火車）×4 首：含一首超長中文歌名（100 位元組）、一首日文假名歌名、一首完全沒有 tag 的（歌名＝檔名、歌手＝資料夾、沒有封面）；
  每首是約 1.7 KB 的小 MP3（ID3v2.4 標籤、一個宣告長度的 Xing 影格、三個靜音影格；真的 `pc_backend_mp3` 讀得出長度，150–260 秒）。
  `.index/library.bin` 與 `scan` 掃出來的逐位元相同（`sim/test.sh` 比對）；`config/app.txt`（只有 `volume=40`；韌體沒有語言設定鍵，語言由有沒有 CJK 字型決定）、
  `config/radio.txt`（3 個電台）、`config/stats.txt`（2 個收藏）。沒有 `lists/`：glyph 包要真的 zip，單字包沿用嵌入的範例。
- 測試與截圖每次從 fixture 複製一份新的再跑（資料根目錄會被寫入）。`make sim-run` 只用 `.local/sim/play`（第一次由 `.local/sim_root`，沒有就由 fixture 複製；重新播種＝刪掉它）。
  `.local/sim_root`（`make sim-root` 做的板上快照）只當 `sim-diff` 的參考資料與播種來源，任何指令都不寫它。fixture 目錄裡的 `.sim_fixture` 是產生器的標記：`sim_fixture.py` 只重生有這個標記（或空）的目錄。
- `make sim-root`（要序列埠，用主板的 by-id 路徑；**不抓 `wifi.txt`**）：抓 `config/app.txt`、`stats.txt`、`radio.txt`、`.index/library.bin`、`lists/*`、`fonts/*`；
  音樂本身（`export/`）不抓（幾 GB），改為依索引為每首歌建一個空檔（`tools/simtools/sim_root.py --placeholders ROOT`，不需板子），曲目頁看得到真的曲庫，播放時合成 180 秒。
- `SIM_STATE`（環境變數，分號分隔 `key=value`，測試用）：`batt=<0-100>`、`charging=<0|1>`、`bt=<off|on|connected>`、`sd=<0|1>`。電源預設是 USB 供電（沒給 `batt`、`charging` 時；`charging=1` 也是 USB，只給 `batt=` 且未充電才是電池）、藍牙關、卡在；`batt` 的百分比只留給測試。

## 假服務各自的行為
| 假服務 | 有行為 | 仍是空的 |
|---|---|---|
| `audio_svc` | 播放狀態機走虛擬時鐘：位置（`start_off` 依來源大小按比例換成起點）、暫停／繼續、`NEAR`／`NEXT`／`ENDED`／`FADED` 事件（真的環形佇列，`player_ctl` 照常換下一首）、gapless 佇列（`queue_next` 在 `NEAR` 之後，結尾接手並發 `NEXT`）、淡出停止（10 秒）、來源在結束／換歌／停止時 `release` 一次；長度與取樣率問真的後端（`open`→`length_ms`→`close`），開不起來或不知道就 180 秒、44.1 kHz；混音器（白噪音）只有狀態；頻譜＝三個慢速掃頻正弦＋種子決定的雜訊，寫進 1024 點環形緩衝再過真的 `pc_spectrum_bars`（時間與種子相同則值相同；暫停、停止、結束全 0）；`simaudiofail <n>`：接下來 n 個解碼來源在下一個 tick 以 `AUD_ERROR` 結束（板上解不了的檔）；`simaudiorate <hz> <n>`：接下來 n 個解碼來源回報這個取樣率，接受與管線規劃照韌體（`pc_src_rate`、`pc_pipe_plan`，超過 `PC_SRC_MAX_RATE` 印 `APP play refused`，否則規劃改變時印一次 `APP pipe`）；`simsink 1|0`：假的 sink 旗標（規劃改成給 sink 的 48k，立刻重新規劃）；`audio_svc_info` 回報規劃（含排隊下一首的 `next_rate`）；sink 開著時跑藍牙重取樣表的模型（hires T10b：`hp_sink` 與建表 task 的 `pc_rsq_swap` 規則，印 `APP rsq wait／init … task=sim／swap … dropped=`） | 解碼與輸出（`render` 不會被呼叫）、音量、sink、post hook、增益、子曲目 |
| `thumb_svc` | 下一步（LVGL 環境）以曲庫裡該路徑那首的專輯名畫 240×240 測試封面（`sim_cover.c`）；沒有封面（`apic_len` 0）或不在曲庫的回 NULL；最新請求優先；`thumb_svc_request_jpeg`（手機的封面）以位元組的雜湊畫測試封面；`simthumblow <n>`：接下來 n 張手機封面當作內部 RAM 不足拒絕；手機封面不受高取樣率暫停、不寫進封面包 | JPEG 解碼、bench、JPEG 鎖 |
| `album_svc` | 3 張「照片」，每張 360×360 測試圖（key＝`photo <idx>`），下一步送達 | 掃描、建檔、等待原因 |
| `radio_svc` | 清單讀 `/sdcard/config/radio.txt`（真的 `pc_radio_parse`；沒有檔案＝空清單，不寫預設；radio2：也認 `radio_<名稱>.txt`，使用中的清單 = cfg `radio_list`，沒設就依語言（簡中 `radio_cn.txt`），清單選擇列和板子一樣）；`radio_play` 接手播放（和板子一樣的 `now_ops`：標題、停止、清單、下一台；假音訊是一個混音來源＝「在播」、沒聲音；藍牙廣播模式拒絕，同板子）；`simradio giveup`＝板子連三次失敗後的放棄（先停、再推訊息頁） | 串流、重連、probe、預設清單 |
| `bt_svc` | 模式、依模式的狀態行；初始模式由 `SIM_STATE bt=`（`on`＝喇叭模式等待中、`connected`＝喇叭模式已連線）；耳機模式掃描得 2 個假裝置、`connect` 選一個；`simbt stream`／`simbt stop`＝手機開始／停止 A2DP 串流（和 `bt_svc.c` 同樣的 now_ops：標題、停止、在播；不推頁）；`simbtmeta title|artist|album <文字>`、`simbtmeta art <檔>|-`、`simbtmeta clear`＝假手機換歌（送 TRACK_CHANGED；第一次會連上 AVRCP），`simbtmeta link 0|1`、`simbtmeta hold 1|0`（封面 GET 先扣著、之後晚到）；假手機回答和板子同一個核心 `pc_bma_*`（`bt_meta.c` 驅動） | 配對（永不出現）、忘記配對、LDAC、真的串流 |
| `power_svc` | USB 或電池：`charging=1` 或兩個鍵都沒給＝USB（板子基準圖在 USB 下拍的），否則電池。韌體沒有電量顯示，百分比只留給測試 | ADC、任務 |
| `noise_engine` | 只有狀態：4 個固定場景×3 層（真的 `pc_layerset`，最多 3 層同時開）、層音量、目前場景、6 個音效／2 個分類；`noise_scene(i)` 超出範圍回 NULL；`simnoise <場景數> <場景 0 的層數> [custom]` 換一組（最多 40 個、16 層；`custom`＝場景 0 是自訂、可刪），`simnoise ?` 印目前場景；存成新情境（插在自訂的後面，同真的；`SIM noise saved <i>`）、加聲音（以音效名新增一層；同名回 `-EEXIST`）、刪自訂情境（`SIM noise delete <i>`，真的移除、後面的編號往前）；`simnoise own`：之後開始的場景接手播放器（真引擎的 now_ops：標題、收藏 id、停止、清單＝情境頁），假音訊「在播」 | 所有聲音、library.muk、寫卡 |
| `clock_svc`（真的） | 鬧鐘：`alarm 21 31; @61; uitop` → `ring`（`sim/test.sh` 驗證） | — |

## 已知和板子不同
- 沒有聲音：播放位置走、事件有，但沒有 PCM；頻譜是合成的三個掃頻音。電池只有「USB／電池」兩態（會影響封面旋轉、特效、熄屏逾時），沒有電量數字。
- 封面與相簿是程式畫的測試圖；電台、白噪音是狀態；音效（`fx_state.c` 是真的，`fake_fx_audio.c` 在 480 格靜音上跑真的濾波鏈）：狀態檔、搬家、匯入匯出、模式位元都真的跑；音是靜音，所以只看 `APP fx apply …` 與 `fx state`。
- 資料根目錄是整個 `/sdcard`；空目錄＝全新的卡（`g_sd_ok` 為真、曲庫空、設定預設值）。`/flashfont/` 沒有（`fontpak` 假的回「沒有」）。
- 時間：無視窗預設 2026-11-11 13:30:00 UTC（台北 21:30），亂數種子 1。
- **字型（Task 4 結果）**：系統 FreeType 2.13.3（板子 2.14.3）、同一份字型檔（`fonts/ui*.ttf` 與板子 TF 卡的 `sha256` 相同），用板上基準比，中文與英文文字**逐像素相同**——沒有字形邊緣的差異，
  所以沒有做 `SIM_FT_BUNDLED`（靜態編 `espressif__freetype`），也不需要模擬器自己的基準。LVGL 設定也與 `app/build/config/sdkconfig.h` 一致（只差生成時蓋掉的 OS＝無、SDL）。
- **對板上基準（`make sim-diff`，2026-11-11，資料用 `.local/sim_root`）**：46 頁中 33 頁 `diff_px=0`，13 頁列在 `sim/known_diffs.txt`（每頁寫了原因）：
  `chip`、`fortune_gacha`、`noise`、`scene`、`now_menu`、`now_paused`、`vdecks`（板上 TF 卡的資料沒抓：`/chiptune`、`fortune/cards.txt`、噪音場景庫、單字庫，或播的是板上真的曲子與封面）、
  `fortune_wheel`、`tools_ideas`、`tools_lunch`、`tools_people`、`tools_slot`、`vquiz`（亂數：板上 `esp_random`，模擬器固定種子）。其餘頁，含 6 張時鐘錶面、`clock`、`tools_moon`、`settings`、`bt`，在對齊時間與設定後逐像素相同。
- 時鐘頁的對齊：時鐘錶面的月相只在建頁時畫一次，所以時間要在開頁之前就設（`sim_diff.py` 的 `page_start`）；秒數是用整個頁面流程搜尋出來的（見 `sim/diff_times.txt`），指針、弧線、點的位置與板上基準一致。
  「下一個節氣 N 天後」是對秒數取上整，會隨一天中的時間變（15:02 是 12 天、21:30 是 11 天）。
- 找到的韌體問題（沒有改，不在本 Task 範圍）：`vocab stat`／`vocab reset` 在單字頁關掉之後執行（`deck_close` 把 `V.prog.cards` 設成 NULL，`ncards` 沒歸零）會在 `pc_vocab_stats` 讀 NULL（板子上讀位址 0 大概不會當機，沒有實測；模擬器的 ASan 會）。
  `leak_cycles.py` 的 `vocab` 收尾因此改成先開著牌組再 `vocab reset`。

## 保真度陷阱（模擬器看不到、或和板子不一樣的地方）
- 顯示鎖是空的、全部跑在同一條執行緒（`sim/shim_bsp.c`）：少拿鎖、競態在這裡看不到。
- 配置永遠成功、堆積數字固定（`sim/shim/esp_heap_caps.h`）：低記憶體（`lowmem`）與 OOM 的路徑走不到。
- 到期時間相同的 esp_timer，模擬器是「最後建立的先跑」；ESP-IDF 是依啟動順序。
- 無視窗模式的 `lv_timer` 週期會進位到 33 ms 的步長（50 ms 變 66 ms，`sim/sim_clock.c`）：以 tick 計數的動畫相位和板子、和視窗模式都不同。
- 一個指令之內時間是凍結的；迴圈或阻塞的任務本體會靜靜地卡住（`sim/shim_esp.c` 的任務是排到步長邊界、跑到結束才回來）。
- 主機是 LP64、板子是 ILP32，而且 `-Wno-format` 蓋掉格式字串與參數寬度不符的警告；`char` 已用 `-funsigned-char` 對齊板子（RISC-V 的 `char` 無號）。
- 路徑裡有 `..` 的 `/sdcard/...` 一律當成檔案不存在（`ENOENT`），韌體本來就不會寫出這種路徑。
- LSan 看不到漏掉的 LVGL 物件與 esp_timer（`lv_deinit` 會全部釋放、計時器串列到結束都還連著）：只有 `APP ui leak` 絆線、`lv_bytes`、與 `simres`（`SIM res esp_timers=<n>`，活著的 esp_timer 數）看得到。
  `make sim-leak` 要求這兩個數字每圈相同（畫面整個生命週期都留著一個計時器是可以的，數字不變就好）。

## 什麼仍然要上板
幀率與繪圖時間、內部 RAM／PSRAM 用量與 `lv_bytes` 的絕對值（模擬器的 `lv_bytes` 只看「回不回得到同一個值」）、音訊（underrun、解碼、混音）、觸覺、藍牙／Wi-Fi／USB 模式、真的封面 JPEG 解碼、睡眠與電源。
每個 Task 結尾仍然上板驗一次。
