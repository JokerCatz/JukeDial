// SPDX-License-Identifier: LGPL-2.1-or-later
/* Every help text of the player (fx apps T6): one ui_help_t per help entry, nothing else. Explanations live here, never on a page (spec
 * 2026-11-13-ui-shared-area.md); tools/check/check_help.py keeps the entries well formed. Paragraphs are split by a blank line ("\n\n").
 * Bodies say "turn", never the device (knob): there are boards without one. Later tasks add their entries below and to UI_HELPS. */
#include "pc/pc_cardtest.h"
#include "pc/pc_i18n.h"
#include "ui_help.h"
#include "esp_attr.h"
#include <stdio.h>
#include <string.h>
#include "audio_svc.h"
#include "bt_meta.h"
#include "bt_svc.h"
#include "cfg.h"
#include "cpu_clock.h"
#include "fx_state.h"
#include "pc/pc_castq.h"
#include "pc/pc_nowinfo.h"
#include "sd_format.h"
#include "thumb_svc.h"
#include "pack_stamp.h"
#include "font.h"
#include "screens.h"
#ifndef SIM
#include "bsp/bsp_sdcard.h"
#endif
#include "games.h"
#include "ui_fxparam.h"

const ui_help_t HELP_SETTINGS = {
    .id = "settings",
    .title_zh = "說明", .title_en = "Help",
    .qr = true,
    .body_zh = "設定頁：USB 線插設定面，電腦用 Chrome 開上面網址\n\n"
               "FakePod 的螢幕是 OLED，久亮會烙印，所以閒置一段時間後一定會關掉，沒有「常亮」選項；音樂照常播放，按鍵或碰一下螢幕就亮\n\n"
               "上面每個 QR 是一個網址：第一個是主要網址，其餘是備用（GitHub、Gitee）。主要網址打不開就掃下一個；網址清單在卡上 app.txt 的 setup_url，用空白隔開，最多 4 個\n\n"
               "隨身碟：插頭翻面插\n\n"
               "震動：本機只有開或關，強度與長度在網頁\n\n"
               "網頁上還有：音量上限、ReplayGain、封面、相簿、廣播品質等細項，以及進階選項\n\n"
               "有線最高取樣率：耳機孔輸出最高到這個值，更高要看 DAC 吃不吃得下；超過的曲目會降到這個值，下一首起生效\n\n"
               "取樣率測試：用耳朵找出你的 DAC 吃得下哪一級，結果會寫進有線最高取樣率\n\n"
               "重開機：點兩下，正在播的歌會在重開後接著播\n\n"
               "實驗性格式：部分 DOS 音樂與少數模組格式的播放器較不成熟，損壞的檔案可能讓播放器重開機。預設關閉：掃描資料庫時不會讀這些檔案（只列檔名），打開會說明原因。開啟要點兩下，改完請重新掃描；任何格式若在播放時當機，那一首下次開機會被標示並略過\n\n"
               "實驗性處理器時脈：預設 320 MHz（我們只在這個速度測試）。降到 240 或 160 MHz 可省一點電，但高解析、LDAC、重效果可能斷音，我們不會自動擋下或拉回。開啟要點兩下，改了馬上生效；開機與當機後會回到 320 MHz。目前實際的時脈在「現況」\n\n"
               "Amiga 音色：只對 Amiga 系的 MOD 模組，用 Paula 模擬器重現硬體的聲音。關＝原本的聲音；A500＝帶固定濾波，LED 濾波照模組指令；A500 + LED＝LED 濾波一直開；A1200＝沒有類比濾波。耗處理器較多：最重的 4 聲道模組約佔音訊核心 55 %（螢幕開著量的）；六、八聲道的模組不套用，聲音照舊；改了播放中的模組馬上換\n\n"
               "播放頁樣式（FakePod）：封面＋資訊＝封面在左上、右邊三個狀態圖示（播放／暫停、隨機、循環，只顯示不能按），下方是歌名、歌手、格式與進度；滿版封面＝封面鋪滿，沒有任何按鈕和字；兩種都是點一下螢幕出現九宮格（見播放選單的說明）；選單格＝舊的格狀選單。進度條只顯示位置，不能點著跳",
    .body_en = "Settings: plug in on the settings side, open the address above in Chrome\n\n"
               "The FakePod screen is OLED, which burns in if it stays lit, so it always turns off after a while idle and has no always-on option. Music keeps playing; a key press or a touch lights it again\n\n"
               "Each QR above is one address: the first is the main one, the others are mirrors (GitHub, Gitee). If the main one does not open, scan the next. The list is setup_url in app.txt on the card, split by spaces, up to 4\n\n"
               "USB drive: flip the plug\n\n"
               "Haptics: on or off here; the strength and the length are on the web page\n\n"
               "More on the web page: the volume limit, ReplayGain, covers, the album wall, the cast quality and an advanced block\n\n"
               "Wired max rate: the headphone jack plays up to this rate, and higher values depend on what the DAC can take. A track above it is halved to it, from the next track on\n\n"
               "Rate test: finds by ear which tier your DAC takes, and writes the result into the wired max rate\n\n"
               "Reboot: tap twice, and the song that is playing carries on after the restart\n\n"
               "Experimental formats: the readers for some DOS music and a few module formats are less mature, and a damaged file may reboot the player. Off by default: the library scan does not read these files (the name only) and opening one says why. Turning it on takes two taps; rescan afterwards. If any format crashes the player while it plays, that song is marked and skipped from the next start on\n\n"
               "Experimental CPU clock: 320 MHz by default (the only speed we test). 240 or 160 MHz saves a little power, but hi-res, LDAC and heavy effects may break up; nothing stops it or raises it back by itself. Turning it on takes two taps and applies at once; a boot or a crash goes back to 320 MHz. The real clock is on Status\n\n"
               "Amiga tone: for Amiga-family MOD modules only, the Paula simulator recreates the hardware sound. Off = the sound as it was; A500 = the fixed filter, the LED filter following the module; A500 + LED = the LED filter always on; A1200 = no analogue filter. It costs more CPU: the heaviest 4-channel module takes about 55 % of the audio core (measured with the screen on); 6- and 8-channel modules are not touched and sound as before; a playing module switches at once\n\n"
               "Now playing style (FakePod): Cover + info = the cover top left, three status icons at its right (playing / paused, shuffle, repeat: display only, nothing to press), then the title, artist, format and progress below; Full cover = the cover fills the screen with no button and no text; with both a tap anywhere shows the nine-grid (see the help of the playing menu); Menu grid = the older grid menu. The bar shows the position only: it cannot be tapped to seek",
};

const ui_help_t HELP_RATETEST = {
    .id = "ratetest",
    .title_zh = "取樣率測試", .title_en = "Rate test",
    .body_zh = "每一級會播兩聲，用不同的取樣率\n\n"
               "兩聲都聽得到，就點一下，換到下一級；沒聽到兩聲或聲音不順，不要點，等 8 秒就結束\n\n"
               "結果是最後一個兩聲都聽到的那一級，會寫進有線最高取樣率；一個都沒確認，設定不變\n\n"
               "中途離開不會改設定，測完會接著播原本的歌；藍牙輸出時不能測",
    .body_en = "Every tier plays two tones, each at a different sample rate\n\n"
               "If you hear both, tap once to go on to the next tier. If you miss one or the sound breaks up, do not tap: the test ends after 8 seconds\n\n"
               "The result is the last tier where you heard both tones. It goes into the wired max rate; if no tier was confirmed, the setting stays\n\n"
               "Leaving half-way changes nothing, and the song that was playing carries on afterwards. The test cannot run over Bluetooth",
};

/* Card diagnostics: a body_fn, so the numbers in "how it is worked out" are the pure function's own constants (pc_cardtest.h), not typed here */
static int cardtest_body(char *b, size_t n, bool zh, int arg)
{
    (void)arg;
    const unsigned sf = PC_CT_SAFETY_X100, fr = PC_CT_FLAC_RATIO_PCT, mg = PC_CT_MARGINAL_PCT;
    if (zh) return snprintf(b, n,
        "直接讀取：不經過檔案，直接讀卡的三個位置（開頭、中間、接近尾端），每處用 16 KB（解碼器實際的讀法）和 64 KB（卡的極限）。只讀，不會寫入，卡滿了也能測\n\n"
        "檔案測試：在卡上寫入 4 MB 暫存檔（_player/cardtest.tmp），再讀回、比對、刪除；要有 8 MB 以上剩餘空間，點兩次才開始。不會動別的檔案\n\n"
        "最近一次的結果會存在卡上（_player/cardtest.txt），下次打開標示「上次」；換了卡就不會顯示\n\n"
        "下方是天梯：各格式依低標由重到輕排列，這張卡的分數是夾在其中的標記列。raw 和檔案讀回是兩個分開的分數，各有一列；各格式的判定看檔案讀回的標記，沒有檔案測試時看 raw 的標記\n\n"
        "怎麼算：低標＝每秒資料量 × %u.%u（%u %%），FLAC 以未壓縮的 %u %% 估算，實際檔案通常更小。勉強＝達到低標的 %u %%：可能能播，也可能卡頓\n\n"
        "限制：直接讀取只量卡本身，實際播放經過檔案系統，碎片多的卡會更慢，檔案測試較接近。門檻只用一張卡校準（2026-11-20），是估計，不是保證。只讀三個區域、各幾秒，卡的其他地方仍可能有慢處\n\n"
        "測試時音樂會停止；離開本頁就中止，暫存檔也會刪掉", sf / 100, sf % 100 / 10, sf, fr, mg);
    return snprintf(b, n,
        "Direct read: no files, the card is read at three places (the start, the middle, near the end), each with 16 KB reads (how a decoder asks) and 64 KB reads (the card's maximum). Read only, nothing is written, and it works on a full card\n\n"
        "File test: writes a 4 MB temp file on the card (_player/cardtest.tmp), reads it back, checks it and deletes it. It needs 8 MB free and two taps to start. No other file is touched\n\n"
        "The latest result of each mode is saved on the card (_player/cardtest.txt) and shown as \"last run\" next time; another card's file is ignored\n\n"
        "Below is the ladder: the formats from the heaviest to the lightest minimum, this card's scores as marker rows among them. Raw and file read-back are two separate scores, each with its own marker; the marks follow the file read-back marker, or the raw one when there is no file test\n\n"
        "How it is worked out: minimum = the format's data per second x %u.%u (%u %%). FLAC is counted at %u %% of uncompressed; real files are usually smaller. Marginal = at least %u %% of the minimum: may play, may stutter\n\n"
        "Limits: the raw read measures the card itself; real playback goes through the file system and a fragmented card is slower, so the file test is closer. The limits were calibrated on one card only (2026-11-20): estimates, not a guarantee. Only three regions are read, a few seconds each; the card can still have slow spots elsewhere\n\n"
        "Music stops while a test runs. Leaving this page cancels it and removes the temp file", sf / 100, sf % 100 / 10, sf, fr, mg);
}

const ui_help_t HELP_CARDTEST = { .id = "cardtest", .title_zh = "記憶卡診斷", .title_en = "Card diagnostics", .body_fn = cardtest_body };

const ui_help_t HELP_FX = {
    .id = "fx",
    .title_zh = "音效", .title_en = "Sound FX",
    .body_zh = "每個音效是一個小工具：點進去可以開關、調整數值\n\n"
               "順序固定：增益、立體聲、音色、真空管、動態、殘響，最後是限幅\n\n"
               "開的音效亮色，關的是一般色，暗色是暫停：電池供電、藍牙編碼、廣播或高取樣率時，吃 CPU 的音效會先暫停，音樂照常播放",
    .body_en = "Every effect is a small app: open it to switch it on or off and set its values\n\n"
               "The order is fixed: gain, stereo, tone, tube, dynamics, reverb, and the limiter last\n\n"
               "On is the bright colour, off is plain, dim means paused: on battery, with some Bluetooth encoders, when broadcasting or at high sample rates, the effects that need much CPU pause first and the music keeps playing",
};

/* Status (hires T7): the pipeline as it is now, from the audio service, the effects, the Bluetooth mode and the card; the console `status` prints the same text.
 * LVGL task (fx_state_running). The page runs this once a second while it is open (refresh_ms). */
EXT_RAM_BSS_ATTR static pc_tbg_prog_t s_status_covers;   /* the cover pass as the last Status body saw it (PSRAM) */
static bool ui_status_covers_row(void) { return pc_tbg_status_row(&s_status_covers); }   /* asked by the help piece right after the body */
int ui_status_body(char *b, size_t n, bool zh, int arg)
{
    (void)arg;
    aud_info_t a;
    audio_svc_info(&a);
    pc_nowinfo_t ni = { .fmt = a.fmt, .src_rate = a.src_rate, .src_bits = a.src_bits, .kbps = a.kbps, .pipe_rate = a.pipe_rate, .pipe_why = a.pipe_why,
                        .out_rate = a.out_rate, .out_bits = a.out_bits, .underruns = a.underruns_src, .released = a.released && !a.sink,
                        .hw_cap = a.hw_cap, .cap = a.cap, .covers_paused = thumb_svc_paused() };
    pc_tbg_prog_t cp;
    thumb_svc_progress_want();
    thumb_svc_progress(&cp);
    s_status_covers = cp;                               /* hires T8e: the pass with its numbers; review M3: the row (ui_status_covers_row) uses this same snapshot */
    ni.covers_state = cp.state; ni.covers_why = cp.why; ni.covers_done = cp.done; ni.covers_total = cp.total; ni.covers_tidy = cp.tidy;
    fx_state_running(&ni.fx_run, &ni.fx_paused, &ni.fx_why);
    bt_mode_t m = bt_svc_mode();
    if (a.sink && m == BT_MODE_HEADPHONES) { ni.out = PC_NI_A2DP; ni.link = bt_svc_codec(); }
    else if (a.sink && m == BT_MODE_BROADCAST) { ni.out = PC_NI_CAST; ni.link = pc_cast_q_label(pc_cast_quality(cfg_int("cast_q", PC_CAST_Q_DEFAULT)), zh); }
    else ni.out = m == BT_MODE_SPEAKER ? PC_NI_SPEAKER : PC_NI_WIRED;
    if (ni.out == PC_NI_SPEAKER && bt_meta_get()) ni.peer_title = bt_meta_get()->title;   /* AVRCP: source: Bluetooth (phone)・<title> */
    if (!g_sd_ok) ni.nocard = sd_format_reason();       /* "no card" only after the SD layer reported a failed mount (no reason yet = still mounting, or the card is the computer's: no line); the simulator's fake_sd.c says "nocard" unless simsd set another */
    pc_nowinfo_text(&ni, zh, b, n);
    {   /* the real CPU clock (the experimental setting; 320 normally) */
        char t[48];
        snprintf(t, sizeof t, T(S_STATUS_CPU_CLOCK) /* CPU clock: %d MHz */, cpu_clock_mhz());
        size_t l = strlen(b);
        if (l + 8 < n) snprintf(b + l, n - l, "\n%s", t);
    }
    if (g_sd_ok) {   /* the card pack's stamp (pack_stamp.h): the version, and an honest line when the pack is missing or older than this firmware needs */
        size_t l = strlen(b);
        const char *v = pack_stamp_version();
        pc_pack_state_t ps = pack_stamp_state();
        if (v[0] && l + 8 < n) snprintf(b + l, n - l, "\n%s%s", T(S_PACK_VERSION) /* Card pack: */, v);
        l = strlen(b);
        if (ps != PC_PACK_OK && l + 8 < n) snprintf(b + l, n - l, "\n%s", ps == PC_PACK_OLD ? T(S_PACK_OLD) /* older than needed */ : T(S_PACK_MISSING) /* none */);
    }
    return (int)strlen(b);
}

const ui_help_t HELP_STATUS = {
    .id = "status",
    .title_zh = "現況", .title_en = "Status",
    .body_fn = ui_status_body,
    .refresh_ms = 1000,
    .link = &scr_covers, .link_zh = "封面快取", .link_en = "Cover cache",   /* hires T8e: the cover pack's page, one tap */
    .link_shown = ui_status_covers_row,                  /* fix 0: only while there is something to see (pc_tbg_status_row); always reachable from Settings */
};

/* Cover cache (hires T8e): the background pass of the cover pack */
const ui_help_t HELP_COVERS = {
    .id = "covers",
    .title_zh = "封面快取", .title_en = "Cover cache",
    .body_zh = "封面先做好存在記憶卡的一個檔裡，翻歌時馬上就有，不必每次重新解圖\n\n"
               "自動：停止時，以及播放 48 kHz 以下的歌時，在背景一張一張建好；播放時每張之間多休息，讓音樂先讀卡。音樂永遠不會因為封面停下來\n\n"
               "暫停的原因會寫在上面：高取樣率的歌在播、播放不順（先等 30 秒，不順三次之後只在停止時建）、記憶體不足、曲庫掃描中、記憶卡空間不足，或你按了暫停\n\n"
               "手動（模式在網頁上設定）：只有按「立即建置」才建。立即建置也會重試之前做不出來的圖\n\n"
               "數字是已建好的封面和有封面的歌；同一張專輯的封面只存一次",
    .body_en = "Covers are made once and kept in one file on the card, so they show at once when the song changes\n\n"
               "Automatic: they are built one by one in the background while nothing plays and while a song of 48 kHz or less plays; while playing there is a longer rest between pictures so the music reads the card first. The music never stops for covers\n\n"
               "Why it pauses is written above: a hi-res song plays, playback stuttered (it waits 30 s; after three times it builds only while stopped), low memory, a library scan, the card is full, or you paused it\n\n"
               "Manual (the mode is set on the web page): covers are built only when you tap Build now. Build now also retries pictures that failed before\n\n"
               "The numbers are the covers built and the songs that have a cover; an album's picture is kept once",
};

/* the edit-on-the-computer entries (fx apps T14): the QR + address of the settings page and one line of what the computer can do; never a Wi-Fi name or password */
const ui_help_t HELP_WIFI = {   /* fx T14; fonts rebuilt at batch close */
    .id = "wifi",
    .title_zh = "Wi-Fi", .title_en = "Wi-Fi",
    .qr = true,
    .web_zh = "在電腦上可以：輸入 Wi-Fi 名稱和密碼", .web_en = "On the computer you can: type the Wi-Fi name and password",
    .body_zh = "Wi-Fi 的名稱和密碼在電腦上輸入，這裡不顯示也不能輸入。USB 線插設定面，電腦用 Chrome 開上面網址",
    .body_en = "The Wi-Fi name and password are typed on the computer; they are neither shown nor typed here. Plug in on the settings side and open the address above in Chrome",
};

const ui_help_t HELP_RADIO = {   /* fx T14; fonts rebuilt at batch close */
    .id = "radio",
    .title_zh = "電台", .title_en = "Radio",
    .web_zh = "在電腦上可以：新增、刪除、排序電台", .web_en = "On the computer you can: add, delete and sort stations",
    .body_zh = "網址和 QR 在設定頁最後一列「說明」，電腦用 Chrome 開啟；改好的電台馬上出現在這個清單",
    .body_en = "The address and QR are in the last row Help of the settings page; open it in Chrome. Changed stations show up in this list at once",
};

const ui_help_t HELP_LISTS = {   /* fx T14; fonts rebuilt at batch close */
    .id = "lists",
    .title_zh = "清單", .title_en = "Lists",
    .web_zh = "在電腦上可以：新增、修改清單內容", .web_en = "On the computer you can: add lists and change what is in them",
    .body_zh = "網址和 QR 在設定頁最後一列「說明」，電腦用 Chrome 開啟；改好的清單下次打開就是新的",
    .body_en = "The address and QR are in the last row Help of the settings page; open it in Chrome. A changed list is new the next time it is opened",
};

/* fx apps T15: the slot pay list, the Bluetooth ring, the alarm list and the pomodoro ring; fonts rebuilt at batch close */
const ui_help_t HELP_SLOT = {
    .id = "slot",
    .title_zh = "拉霸", .title_en = "Slot machine",
    .body_zh = "五條線：三橫排、兩斜線。三個一樣才中，幸運草可代替任何圖案\n\n"
               "左起兩顆櫻桃（幸運草也算）+2",
    .body_en = "Five lines: three rows, two diagonals. Three of a kind wins; clover stands for any symbol\n\n"
               "Two cherries from the left (clover counts) +2",
};

const ui_help_t HELP_BT = {
    .id = "bt",
    .title_zh = "藍牙", .title_en = "Bluetooth",
    .body_zh = "模式：點一下選取，再點一次（或點中間）才會切換；換模式會重開機，重開後才生效\n\n"
               "現況：點一下打開現況頁，看現在的來源、處理、音效和輸出\n\n"
               "清除配對：忘記所有配對過的手機和耳機，要先開啟藍牙模式才能清除",
    .body_en = "Modes: a tap selects, a second tap (or the centre) switches; a new mode restarts the player and takes effect after the restart\n\n"
               "Status: a tap opens the status page with the source, the processing, the effects and the output as they are now\n\n"
               "Forget paired: forgets every paired phone and headphones; a Bluetooth mode has to be on first",
};

const ui_help_t HELP_ALARM = {
    .id = "alarm",
    .title_zh = "鬧鐘", .title_en = "Alarm",
    .body_zh = "重複：平日、每天、週末、只一次，點一下換下一個，轉動可往兩邊調\n\n"
               "需要對時：鬧鐘那一列寫著「尚未對時」時鬧鐘不會響；開啟鬧鐘時會自動連 Wi-Fi 對時\n\n"
               "鈴聲：預設是內建鐘聲；「鈴聲改用目前播放的歌」只接受 MP3；點鈴聲那一列兩次可改回內建鐘聲",
    .body_en = "Repeat: weekdays, every day, weekend or once; a tap steps to the next one and turning goes both ways\n\n"
               "Needs the time: while the alarm row says not synced the alarm does not ring; switching it on connects to Wi-Fi and sets the time\n\n"
               "Sound: the built-in chime by default; Use the playing song takes an MP3 only; tap the sound row twice to go back to the chime",
};

const ui_help_t HELP_POMO = {
    .id = "pomo",
    .title_zh = "番茄鐘", .title_en = "Pomodoro",
    .body_zh = "點一下畫面開啟選單：中間開始、暫停或繼續\n\n"
               "專注結束後休息，幾輪之後是長休息\n\n"
               "停止要點兩次：第一次先確認，第二次才停止並歸零\n\n"
               "錶面格換外觀：資訊、圓環、HUD（設計錶面）；顏色格換 HUD 的配色\n\nHUD 錶面要有卡上的 _player/faces.zip；沒有它就不提供 HUD，只剩資訊和圓環",
    .body_en = "Tap the page to open the ring: the centre starts, pauses or resumes\n\n"
               "A focus period is followed by a break, and after a few rounds a long break\n\n"
               "Stop takes two taps: the first asks, the second stops and resets\n\n"
               "The Face cell changes the look: Info, Rings, HUD (a designed face); the Colour cell changes the HUD's colours\n\nThe HUD faces need _player/faces.zip on the card; without it HUD is not offered, only Info and Rings",
};

const ui_help_t HELP_STOPWATCH = {
    .id = "stopwatch",
    .title_zh = "碼錶", .title_en = "Stopwatch",
    .body_zh = "點一下畫面開啟選單：中間開始、暫停或繼續\n\n"
               "計圈記下每一圈的時間，畫面顯示最近幾圈，最快的一圈有星號\n\n"
               "歸零要點兩次；離開這一頁碼錶照樣走，音樂也不受影響\n\n"
               "錶面格換外觀：資訊、圓環、HUD\n\nHUD 錶面要有卡上的 _player/faces.zip；沒有它就不提供 HUD，只剩資訊和圓環",
    .body_en = "Tap the page to open the ring: the centre starts, pauses or resumes\n\n"
               "Lap notes each lap's time; the page shows the latest laps and marks the fastest with a star\n\n"
               "Reset takes two taps; the stopwatch keeps running when you leave the page, and the music is not touched\n\n"
               "The Face cell changes the look: Info, Rings, HUD\n\nThe HUD faces need _player/faces.zip on the card; without it HUD is not offered, only Info and Rings",
};

const ui_help_t HELP_COUNTDOWN = {
    .id = "countdown",
    .title_zh = "倒數計時", .title_en = "Countdown",
    .body_zh = "點一下畫面開啟選單：分、秒格用轉動或點擊設定時間，中間開始、暫停或繼續\n\n"
               "最後 10 秒數字變紅（HUD 錶面整張轉成紅色「警戒」）；時間到會震動（圓形主機）、響一段提示音，不在這一頁時會跳出訊息\n\n"
               "歸零要點兩次；離開這一頁倒數照樣進行\n\n"
               "錶面格換外觀：資訊、圓環、HUD\n\nHUD 錶面要有卡上的 _player/faces.zip；沒有它就不提供 HUD，只剩資訊和圓環",
    .body_en = "Tap the page to open the ring: the Min and Sec cells set the time by turning or tapping, the centre starts, pauses or resumes\n\n"
               "In the last 10 seconds the digits turn red (the HUD face turns into its red WARNING look); at zero the round board buzzes, a sound plays, and a message appears when you are not on this page\n\n"
               "Reset takes two taps; the countdown keeps going when you leave the page\n\n"
               "The Face cell changes the look: Info, Rings, HUD\n\nThe HUD faces need _player/faces.zip on the card; without it HUD is not offered, only Info and Rings",
};

const ui_help_t HELP_TIMESET = {
    .id = "timeset",
    .title_zh = "設定時間", .title_en = "Set time",
    .body_zh = "年、月、日、時、分各一列：點一下加一，之後轉動可雙向調整；日會跟著月份的天數\n\n"
               "輸入的是本地時間（時區在網頁設定）；按「套用」才會生效，秒數從 0 開始\n\n"
               "「用 Wi-Fi 對時」會開啟 Wi-Fi 並向網路校時，之後取代這裡設定的時間\n\n"
               "圓形板沒有內建時鐘，關機後時間會遺失，需重新設定或對時；FakePod 有內建時鐘，設定或對時後關機也會保存",
    .body_en = "One row each for year, month, day, hour and minute: a tap adds one, then turning adjusts both ways; the day follows the month's length\n\n"
               "You enter local time (the time zone is set on the web page); it only takes effect when you tap Apply, and the seconds start at 0\n\n"
               "Sync over Wi-Fi turns Wi-Fi on and fetches the time from the network; it replaces the time set here\n\n"
               "The round board has no built-in clock, so the time is lost when the power goes off: set it again or sync. The FakePod has one: a time set or synced there is kept while it is off",
};

const ui_help_t HELP_EXT = {
    .id = "ext",
    .title_zh = "外接模式", .title_en = "External mode",
    .body_zh = "這台變成電腦的外接隨身碟和鍵盤\n\n"
               "點一格就送出那個鍵給電腦：多媒體鍵、快捷鍵、一段文字或一串巨集；轉動是這一層指定的用途，預設是電腦音量\n\n"
               "上一層、下一層換一組鍵；超過一頁的格子用手指拖曳翻頁\n\n"
               "鍵組寫在卡上的 _player/config/keys.txt：每層有層名、轉動的用途（volume、scroll、arrows、none）和一串鍵（名稱加 combo、text、macro 或 consumer）\n\n"
               "文字和巨集只送英文字母、數字和符號，最多 64 字；有任何一行錯，整個檔案都不採用，改用預設的媒體鍵層，錯在哪行看「設定」\n\n"
               "改完檔案在電腦上退出隨身碟，這裡會自動重讀；卡在電腦上時不能讀",
    .body_en = "This device becomes the computer's external drive and keypad\n\n"
               "A tap sends that key to the computer: a media key, a shortcut, a line of text or a macro; the turn does the job the layer names, the computer volume by default\n\n"
               "Previous and next layer switch the key set; more cells than a page turn by dragging\n\n"
               "The key sets live in _player/config/keys.txt on the card: each layer has a name, what turning does (volume, scroll, arrows, none) and a list of keys (name, then combo, text, macro or consumer)\n\n"
               "Text and macros send only English letters, digits and symbols, at most 64 characters; any bad line rejects the whole file and the default media layer is used, and Config shows which line\n\n"
               "After editing, eject the drive on the computer and the file is read again by itself; it cannot be read while the computer has the card",
};

const ui_help_t HELP_CAST = {
    .id = "cast",
    .title_zh = "廣播電台", .title_en = "Broadcast radio",
    .body_zh = "這個模式只播放，沒有播放器：電台從 Wi-Fi 收進來，用 Auracast 廣播出去，附近的耳機和喇叭都能收聽\n\n"
               "電台是進來之前在播放器裡選的那一台；要換台請先回播放器\n\n"
               "開機後先連 Wi-Fi，連上了才開廣播；沒有連上就不會廣播。Wi-Fi 帳號放在卡上的 _player/config/wifi.txt\n\n"
               "轉動調音量。按住畫面不放，或點兩次「回播放器」，重開機回到播放器",
    .body_en = "This mode only plays, with no player: the radio comes in over Wi-Fi and goes out as an Auracast broadcast that nearby headphones and speakers can listen to\n\n"
               "The station is the one chosen in the player before coming here; to change it, go back to the player first\n\n"
               "It connects to Wi-Fi first and starts the broadcast only once connected; without Wi-Fi there is no broadcast. The Wi-Fi login is _player/config/wifi.txt on the card\n\n"
               "Turning sets the volume. Hold the screen, or tap Player twice, to restart into the player",
};

/* one page of one filter: the block is refilled by each call (one filter page is open at a time) */
static ui_help_t s_fxapp_help;
const ui_help_t *ui_help_fx_app(int app)
{
    const pc_fxapp_t *a = fx_app(app);
    s_fxapp_help = (ui_help_t){ .id = "fxapp", .title_zh = a->zh, .title_en = a->en, .body_fn = ui_help_fx_app_body, .arg = app };
    return &s_fxapp_help;
}

int ui_help_fx_app_body(char *b, size_t n, bool zh, int app)
{
    const pc_fxapp_t *a = fx_app(app);
    int k = snprintf(b, n, "%s", zh ? TZ(a->desc_zh) : a->desc_en);
    char one[160];
    int first = -1;
    for (int i = -1; i < a->nparam && k < (int)n - 1; i++) {
        const pc_fxparam_t *p = i < 0 ? &pc_fxapp_mix_param : &a->param[i];
        if (i < 0 && !(a->flags & PC_FXA_MIX)) continue;
        if (p->flags & PC_FXP_BAND) { if (first < 0) first = i; if (i - first >= 4) break; }   /* the four band rows once: every band has the same ranges */
        char lo[40], hi[40], def[40];
        pc_fxparam_text(p, p->lo, zh, lo, sizeof lo); pc_fxparam_text(p, p->hi, zh, hi, sizeof hi); pc_fxparam_text(p, p->def, zh, def, sizeof def);
        snprintf(one, sizeof one, "%s: %s – %s, %s %s", ui_fxparam_name(p, zh), lo, hi, zh ? TZ("預設") : "default", def);
        k += snprintf(b + k, (size_t)k < n ? n - (size_t)k : 0, "\n\n%s", one);
    }
    return k < (int)n ? k : (int)n - 1;
}

#ifdef SIM
/* simulator test entry (console "help open longtest"): a body that fills the whole buffer and ends in the middle of a character, so the cut is tested */
static int long_body(char *b, size_t n, bool zh, int arg)
{
    static const char ch[] = "測";                      /* 3 bytes */
    (void)zh; (void)arg;
    b[0] = 'a';
    for (size_t i = 1; i < n - 1; i++) b[i] = ch[(i - 1) % 3];
    b[n - 1] = 0;
    return (int)(n - 1);
}
const ui_help_t HELP_TEST_LONG = { .id = "longtest", .title_zh = "長", .title_en = "Long", .body_fn = long_body };
#endif

/* cardfmt: the Memory card page's help, the only place the confirmation code is written down */
const ui_help_t HELP_CARD = {
    .id = "card",
    .title_zh = "記憶卡", .title_en = "Card",
    .body_zh = "播放器讀不了這張卡：可能是 GPT 分割，或卡上沒有檔案系統。格式化會做成 MBR 和一個分割區：32 GB 以下用 FAT32，更大的用 exFAT，2 GB 以下的小卡用 FAT16\n\n"
               "卡上所有資料都會消失，無法復原；要留的檔案請先用電腦備份\n\n"
               "步驟：先看警告點「繼續」，再輸入確認碼，最後點兩下確認。任何一步按返回都不會格式化\n\n"
               "確認碼：上 上 下 下 左 右 左 右 B A。按錯一下就從頭來，30 秒沒按會回到警告\n\n"
               "格式化時請勿拔卡，螢幕會一直亮著；完成後播放器會建好自己的資料夾，音樂放進 export 資料夾",
    .body_en = "The player cannot read this card: it has GPT partitions or no file system. Formatting makes an MBR with one partition: FAT32 up to 32 GB, exFAT above, FAT16 for small cards up to 2 GB\n\n"
               "Everything on the card is erased and cannot be brought back; copy what you want to keep to a computer first\n\n"
               "Steps: read the warning and tap Continue, enter the code, then tap twice to confirm. Back at any step formats nothing\n\n"
               "The code: up up down down left right left right B A. One wrong tap starts it over; 30 seconds without a tap goes back to the warning\n\n"
               "Do not pull the card while it formats; the screen stays on. Afterwards the player makes its own folders; music goes into the export folder",
};

/* the Games hub (games hub G0): how a game page works; the rules are each game's own help (games_help_entry, the last cell of the game's ring, G0b) */
const ui_help_t HELP_GAMES = {
    .id = "games",
    .title_zh = "遊戲", .title_en = "Games",
    .body_zh = "點一個遊戲進去，點棋盤下子，每一步都有震動；上面左邊的按鈕換對手（你先手或後手、雙人），右邊的按鈕寫著現在的規則，點它打開這個遊戲的選單：規則（每個遊戲有幾種玩法）、風格（棋盤的顏色和棋子的樣子）、重來，最後是這個遊戲的 說明\n\n結束後會出現結果卡（步數、時間、這次開機以來的最佳），點卡片或棋盤重來；玩的時候音樂不會停",
    .body_en = "Tap a game to open it and tap the board to move, each move has a haptic click; the button at the top left changes the opponent (you first or second, two players), the button next to it shows the current rules and opens the game's menu: Rules (each game has a few ways to play), Style (the board's colours and pieces), Restart, and last the game's Help\n\nAt the end a result card shows the moves, the time and the best since the device started; a tap on the card or the board starts again; the music keeps playing",
};

/* one game's help: the block is refilled by each call (one game page is open at a time) */
static ui_help_t s_game_help;
const ui_help_t *games_help_entry(int i)
{
    const ui_game_help_t *h = games_help(i);
    s_game_help = (ui_help_t){ .id = "game", .title_zh = games_name_zh(i), .title_en = games_name_en(i), .body_zh = h ? h->zh : "", .body_en = h ? h->en : "" };
    return &s_game_help;
}

/* sfx task 2: the Sound bank page's help */
const ui_help_t HELP_SFXBANK = {
    .id = "sfxbank",
    .title_zh = "音效庫", .title_en = "Sound bank",
    .body_zh = "點一下就播放那個音效。每個音效都是用晶片音色即時合成的（紅白機、Game Boy、AY 音源），沒有錄音檔\n\n"
               "音量跟著音樂走，音樂暫停時也聽得到\n\n"
               "設定裡的「小工具音效」關掉時，玩具和遊戲不出聲；這一頁點了照樣會播\n\n"
               "部分音效取自 ZzFX（MIT 授權）和 jsfxr（公有領域）的參數；旋律都是原創",
    .body_en = "Tap a row to play that sound. Every sound is made live from chip voices (NES, Game Boy, AY); there are no recordings\n\n"
               "They follow the volume like the music, and they sound while the music is paused too\n\n"
               "With Toy sounds off in Settings the toys and games stay silent; this page still plays when you tap\n\n"
               "Some sounds use parameters from ZzFX (MIT licence) and jsfxr (public domain); the melodies are original",
};

/* 調音器 (2026-11-17): the tuner's help (listen and tone pages) */
const ui_help_t HELP_TUNER = {
    .id = "tuner",
    .title_zh = "調音器", .title_en = "Tuner",
    .body_zh = "「聆聽」用機身的麥克風聽琴弦或人聲，找出最接近的音名，指針顯示偏差的音分（100 音分 = 一個半音）。指針落在綠色範圍內（5 音分以內）就是音準了\n\n"
               "聆聽時音樂會暫停，離開這一頁或螢幕熄滅後，原本在播的音樂會接著播。聲音太小或雜訊太多時，只會顯示「沒有聽到音」\n\n"
               "選單裡可以調 A4 基準音，430 到 450 赫茲，預設 440。同一個基準音也用在「發音」\n\n"
               "「發音」會播出指定的音（C2 到 C6），用來對耳朵或對別的樂器。音色有正弦波、兩種方波、三角波和鋸齒波，音高精確。選單裡的「吉他弦」一次跳一條空弦（E2 A2 D3 G3 B3 E4）\n\n"
               "發音走小工具音效的混音路徑，音量跟著音樂的音量，音樂暫停時也聽得到。聆聽時不錄音、不存任何聲音",
    .body_en = "Listen uses the built-in microphone to hear a string or a voice and finds the nearest note. The needle shows how far off it is in cents (100 cents = one semitone). In the green zone (within 5 cents) it is in tune\n\n"
               "The music pauses while you listen and goes on when you leave the page or the screen goes off, if it was playing. With too little sound or too much noise you just see No note heard\n\n"
               "The menu sets the A4 reference, 430 to 450 Hz, 440 by default. The same reference is used by Play a note\n\n"
               "Play a note sounds the note you pick (C2 to C6) to tune by ear or against another instrument. Voices: sine, two square waves, triangle and saw, at the exact pitch. Guitar string in the menu steps through the open strings (E2 A2 D3 G3 B3 E4)\n\n"
               "The note takes the toy sounds' mixing path: it follows the volume like the music and also sounds while the music is paused. Listening records nothing and stores no sound",
};

/* ideas G2: the Counters page's help */
const ui_help_t HELP_COUNTER = {
    .id = "counter",
    .title_zh = "多組計數器", .title_en = "Counters",
    .body_zh = "點一個扇形，那一組就加一，並且變成目前選的那一組；接著轉動，可以替目前這一組加減（四秒沒動作就還給音量）\n\n"
               "每數到十的倍數會響一聲。數字上限 9999，最小是 0\n\n"
               "上方的選單：數量可選 2、4、8 組；名稱可選 分數、圈數、次數 或 A 到 H；風格換顏色；歸零要點兩次才會全部歸零\n\n"
               "數字、數量、名稱和風格都記在記憶卡上，下次開機還在；沒有記憶卡時只記到關機",
    .body_en = "Tap a sector to add one to that counter; it becomes the selected one. Then turn to add or take away one on the selected counter (after four idle seconds the turn is the volume again)\n\n"
               "Every multiple of ten plays a tick. The most is 9999, the least 0\n\n"
               "The menu at the top: Counters 2, 4 or 8; Names Score, Laps, Times or A to H; Style changes the colours; Zero needs two taps and zeroes every counter\n\n"
               "The numbers, the count, the names and the style are kept on the card and are still there after a restart; without a card they last until the device is off",
};

/* ideas G5: the Ambient scenes' help */
const ui_help_t HELP_AMBIENT = {
    .id = "ambient",
    .title_zh = "氛圍頁", .title_en = "Ambient scenes",
    .body_zh = "四個會慢慢動的場景，放著當裝飾也可以一邊聽音樂：熔岩燈（蠟球受熱上升、遇冷下沉，碰到會融合）、魚缸（小魚游來游去，氣泡往上飄）、篝火（火焰和火星）、夜燈（慢慢呼吸、顏色緩緩變化的光，沒有時鐘）\n\n"
               "點一下場景會戳它一下：熔岩燈的蠟球分裂成兩個；魚缸點空白處會丟一粒飼料，最近的魚游過去吃掉並長大一點，點到魚則牠會衝開；篝火會竄高；夜燈會亮一下。偶爾會出現稀有的東西：金色的蠟球、金魚、一陣火星、螢火蟲，點到金色的有聲音和震動\n\n"
               "點過場景後四秒內轉動可以調亮度（五段，最亮也不刺眼），之後轉動又是音量；轉動調音量時會出現返回\n\n"
               "上方左邊的格子換速度，右邊的格子打開選單：場景、色調（經典、霓虹、糖果）、亮度、重來。音樂播放時場景會隨音量變得更有活力。螢幕關掉或打開選單時場景會停住，回來後自己接著動",
    .body_en = "Four slow scenes to leave on as decoration, with music if you like: a lava lamp (wax warms and rises, cools and sinks, blobs that touch merge), an aquarium (fish wander, bubbles drift up), a campfire (flames and embers) and a night light (a glow that breathes slowly and drifts in colour; no clock)\n\n"
               "A tap pokes the scene: a lava blob splits in two; in the aquarium a tap on open water drops a food pellet that the nearest fish swims to, eats and grows a little from, and a tap on a fish makes it dart; the fire flares; the night light pulses. Now and then something rare shows up: a golden blob, a golden fish, a burst of sparks, a firefly. Catching the golden one plays a sound and a buzz\n\n"
               "For four seconds after a tap a turn steps the brightness (five steps, never full), then a turn is the volume again; turning for the volume also shows Back\n\n"
               "The left cell at the top changes the speed, the right one opens the menu: Scene, Tone (Classic, Neon, Candy), Brightness, Restart. With music playing the scenes get livelier with the level. With the screen off or the menu open the scene stops, and goes on by itself when it is back",
};

/* ideas G7: Drink reminder and Habits (clock) */
const ui_help_t HELP_DRINK = {
    .id = "drink",
    .title_zh = "喝水提醒", .title_en = "Drink reminder",
    .body_zh = "按「喝了」或中間的數字就多算一杯，圈圈會一格一格補滿；「稍後」把下一次提醒延後十分鐘。目標杯數達成後當天不再提醒\n\n"
               "提醒在選單裡開啟（預設是關的）：到了時間，機器會震動並響一聲，音量跟著音樂走；不會叫醒螢幕，也不會跳出畫面，螢幕關著時只有震動和聲音。鬧鐘響著時不提醒\n\n"
               "選單：提醒開關；間隔 30、45、60、90 分鐘；開始和結束的時刻（只在這段時間提醒，第一次提醒在開始後一個間隔）；目標杯數；少一杯（點兩次，算錯時用）\n\n"
               "杯數按當地日期計算，午夜歸零，用的是時鐘頁選的時區；記在記憶卡上，沒有記憶卡時只記到關機。需要先對時：時鐘頁選單的「立即對時」",
    .body_en = "Tap Drank or the number in the middle to count a cup; the ring fills one segment a cup. Later puts the next reminder off by ten minutes. Once the goal is reached there are no more reminders that day\n\n"
               "Reminders are switched on in the menu (they are off by default): at the time the device buzzes and plays one note, at the music's volume. It never wakes the screen or opens a page; with the screen off you only feel and hear it. While an alarm rings there is no reminder\n\n"
               "The menu: reminders on or off; Every 30, 45, 60 or 90 minutes; From and Until (reminders only within these hours; the first one comes one interval after From); the Goal in cups; Minus one (two taps, for a cup counted by mistake)\n\n"
               "Cups count by the local date and go back to zero at midnight, by the time zone chosen on the clock page. They are kept on the card, and without a card only until the device is off. The time has to be set first: Sync now in the clock menu",
};
const ui_help_t HELP_HABITS = {
    .id = "habits",
    .title_zh = "習慣打卡", .title_en = "Habits",
    .body_zh = "點一個扇形，今天就打卡（一天一次，再點沒有作用），那一格亮起來並成為目前選的習慣。標題寫它的名字和連續天數，左上的小格是最近七天（最舊在左，今天在右；# 有打卡，. 沒有）\n\n"
               "漏掉一天，連續天數就歸零；昨天打了今天還沒打，連續天數照舊。午夜（時鐘頁選的時區的當地時間）換日\n\n"
               "選單：數量可選 2、4、8 個；這格可從 16 個預設名稱中換（轉動會跳過已在別格的名稱，每個名稱的紀錄各自保存，換名不會弄丟）；取消今天要點兩次，打錯時用\n\n"
               "紀錄記在記憶卡上，沒有記憶卡時只記到關機。需要先對時：時鐘頁選單的「立即對時」",
    .body_en = "Tap a sector to tick that habit for today (once a day; tapping again does nothing); it lights up and becomes the selected one. The title gives its name and streak, and the small cell at the top left is the last seven days (oldest left, today right; # ticked, . not)\n\n"
               "Miss a day and the streak goes back to zero; ticked yesterday and not yet today, the streak stands. The day changes at midnight, local time of the time zone chosen on the clock page\n\n"
               "The menu: 2, 4 or 8 habits; This one changes the name from 16 presets (turning skips the names already on another sector, and every name keeps its own record, so a rename loses nothing); Undo today needs two taps, for a tick by mistake\n\n"
               "The records are kept on the card, and without a card only until the device is off. The time has to be set first: Sync now in the clock menu",
};

/* ideas G7: the Breathing coach's help */
const ui_help_t HELP_BREATH = {
    .id = "breath",
    .title_zh = "呼吸教練", .title_en = "Breathing coach",
    .body_zh = "跟著圓圈呼吸：圓圈變大時吸氣，停在最大時屏息，變小時吐氣，上方寫著現在的動作和倒數秒數，也會剩下多少時間；每換一個動作輕輕震一下，外圈慢慢填滿代表整段練習的進度。時間到會震一下並寫完成，點一下可以再來一次\n\n"
               "節奏有三種：4-7-8（吸 4 秒、屏 7 秒、吐 8 秒，幫助入睡）、箱式（4-4-4-4：吸、屏、吐、停各 4 秒，專注用）、5-5（吸 5 秒、吐 5 秒，沒有屏息，放鬆用）。上方左邊的格子選練習長度：3、5 或 10 分鐘\n\n"
               "噪音跟隨：如果正在播放白噪音場景，可以讓聲音隨著呼吸起伏，吸氣時漸漸變大、吐氣時漸漸變小。輕最低降到七成五，深最低降到五成，永遠不會比你設定的音量更大。沒有播放場景（或是播噪音顏色）時沒有作用。點過圓圈後四秒內轉動可以換這個設定，之後轉動又是音量\n\n"
               "螢幕關掉或打開選單時練習會暫停，回來後自己接著走；聲音也會自己回到原本的大小",
    .body_en = "Breathe with the circle: it grows while you breathe in, stays at full size while you hold and shrinks while you breathe out. The top line says what to do now, the seconds left in the step and the time left in the session. A soft buzz marks each change and the outer ring fills as the session goes. When the time is up there is a buzz and Done; a tap starts another\n\n"
               "Three patterns: 4-7-8 (in 4 s, hold 7 s, out 8 s, to fall asleep), Box (4-4-4-4: in, hold, out, rest, 4 s each, for focus) and 5-5, coherent (in 5 s, out 5 s, no hold, to relax). The left cell at the top picks the length: 3, 5 or 10 minutes\n\n"
               "Noise follow: with a noise scene playing, the sound can rise and fall with your breath, swelling as you breathe in and fading as you breathe out. Soft goes down to 75 %, Deep down to 50 %, and it never goes above the level you set. It does nothing when no scene is playing (or a noise colour is). For four seconds after a tap on the circle a turn changes this setting, then a turn is the volume again\n\n"
               "With the screen off or the menu open the session waits and goes on by itself when you are back; the sound returns to its own level by itself",
};

/* The visualiser page (2026-11-17): the kinds, the size switch, voice rows / piano roll with mute and solo, the lyrics file rules and limits */
const ui_help_t HELP_VIZ = {
    .id = "viz",
    .title_zh = "視覺化", .title_en = "Visualiser",
    .body_zh = "種類：長條、環形、波形、瀑布、相位；播放晶片音樂或模組檔（NSF、GB、VGM、SPC、MOD、XM…）時多出聲部、聲部列、鋼琴捲簾；歌詞是最後一種\n\n"
               "點一下頁面打開設定環：種類、變化、色帶、上色、各種類自己的一格、靈敏度；方屏多一格大小；最後一格是說明。轉動換目前那一格，4 秒沒動作就收起\n\n"
               "大小（方屏）：放大是把圖放大到 440 像素，原尺寸不放大，比較銳利，每個畫面也少一次縮放。長條、環形、波形、聲部圖適用，瀑布和相位不放大\n\n"
               "聲部列：每個聲部一列：名稱、發音燈（新音一閃）、音量條與頂點、音名（沒有音高顯示 --）、聲像小標記；M＝靜音，S＝獨奏。最多 32 個聲部，超過 16 個時字會縮小\n\n"
               "鋼琴捲簾：最近 6 秒，右邊最新，上下是音高 C1 到 B7；每個聲部一種顏色（變化選「音高」改用音高上色）；沒有音高的聲部（噪音、取樣）畫在最上面的細條；亮點是新音的開始\n\n"
               "靜音與獨奏：在聲部列或鋼琴捲簾打開設定環，「聲部」格選一個聲部，「狀態」格依序換 開、靜音、獨奏。獨奏是只聽那一個聲部，換另一個獨奏會取消前一個；換曲就全部回到開。靜音只是不讓那個聲部進入解碼，音量與效果不受影響\n\n"
               "音高：NES、Game Boy、SN76489（VGM 的 PSG）、AY、PC Engine、VRC6 讀得出音名；FM 晶片（YM2612、YM2413）、SPC、FDS、N163、SCC 目前只有音量與新音；模組檔都有。聲像只有 SPC 和模組檔有\n\n"
               "歌詞：讀歌曲旁邊同名的 .lrc 檔（song.mp3 配 song.lrc）。檔案必須是 UTF-8（可以有 BOM），GBK 和 Big5 不會轉換，會顯示「歌詞檔不是 UTF-8」。看得懂 [mm:ss.xx] 時間標籤（一行可以有好幾個）和 [offset:毫秒]，其他標籤（ti、ar 等）忽略。上限：檔案只讀前 64 KB、400 行、每行 160 位元組、全部文字 24 KB，超過的丟掉。變化選三行或五行；長句會換行，太長的在邊界截掉。快轉、暫停、換曲都會跟上；沒有檔就顯示「沒有歌詞」。只有曲庫裡的歌有歌詞",
    .body_en = "Kinds: bars, ring, waveform, waterfall, phase; while a chip tune or tracker file (NSF, GB, VGM, SPC, MOD, XM...) plays there are also voices, voice rows and a piano roll; lyrics is the last one\n\n"
               "Tap the page to open the style ring: kind, variation, colour band, colouring, the kind's own cell, sensitivity; the square panel has a size cell too; the last cell is help. Turn changes the current cell, and the ring closes after 4 seconds without input\n\n"
               "Size (square panel): Zoomed enlarges the picture to 440 pixels; Original does not, so it is sharper and every frame saves one scaling step. It applies to bars, ring, waveform and the voice pictures; the waterfall and the phase picture are never enlarged\n\n"
               "Voice rows: one row per voice: its name, a key-on lamp (flashes at a new note), the level bar with its cap, the note name (-- when there is no pitch) and a small pan marker; M = muted, S = solo. Up to 32 voices; above 16 the text gets smaller\n\n"
               "Piano roll: the last 6 seconds, the newest at the right, pitch from C1 to B7; one colour per voice (the variation Pitch colours by pitch instead); voices without a pitch (noise, samples) are drawn in the thin strip at the top; a bright dot is the start of a note\n\n"
               "Mute and solo: open the style ring on the voice rows or the piano roll, pick a voice with the Voices cell and step its State cell through on, mute, solo. Solo hears only that voice, and soloing another voice ends the earlier solo; a track change puts every voice back on. Muting only keeps the voice out of the decoder, so the volume and the effects are not touched\n\n"
               "Pitch: NES, Game Boy, SN76489 (the PSG of a VGM), AY, PC Engine and VRC6 give note names; the FM chips (YM2612, YM2413), SPC, FDS, N163 and SCC give only the level and the new notes for now; tracker files give everything. Pan comes from SPC and tracker files only\n\n"
               "Lyrics: the .lrc file with the song's name next to it is read (song.mp3 with song.lrc). It must be UTF-8 (a BOM is fine); GBK and Big5 are not converted and show \"Lyrics file is not UTF-8\". [mm:ss.xx] time tags (several per line) and [offset:ms] are understood, other tags (ti, ar...) are ignored. Limits: only the first 64 KB of the file, 400 lines, 160 bytes per line and 24 KB of text in all are read; the rest is dropped. The variation picks three or five lines; a long line wraps and is cut at the edge of its band if it is still too long. Seeking, pause and track changes are followed; with no file it says \"No lyrics\". Only songs of the library have lyrics",
};

/* The nine-grid of invisible tap zones (FakePod's now-playing family, 2026-11-22; ui_ninegrid.c): the playing menu's last cell on the square panel */
const ui_help_t HELP_NINEGRID = {
    .id = "ninegrid",
    .title_zh = "九宮格", .title_en = "Nine-grid",
    .body_zh = "播放頁和頻譜頁是乾淨的：點一下任何地方，出現九格半透明的字（上方是歌名和時間）。顯示中點一格就執行，每次點擊重算 4 秒（實驗室的 nowb_hide_ms，0＝不自動收起）；沒顯示時點螢幕只會顯示，不會誤按\n\n"
               "上排：切換組合（顯示組合名和第幾組）、選單（打開原來的選項環）、下一頁（封面→頻譜→歌詞→封面，歌曲沒有歌詞就跳過）。中排：左右兩格是目前組合的功能，中間固定是播放／暫停。下排：上一首、關閉（馬上收起）、下一首\n\n"
               "組合：封面頁只有播放模式（隨機開關、循環全部／單曲）；頻譜頁有樣式（上一樣式／下一樣式）、配色（上一配色／下一配色）、播放模式。切換組合會輪流換。播放器目前不能在曲子裡快轉，所以沒有快轉倒轉\n\n"
               "九宮格顯示時，下面的畫面（頻譜、封面）會停住。邊緣甩動和兩個側鍵不變",
    .body_en = "The now-playing and spectrum pages are clean: tap anywhere and nine translucent labels appear (with the title and the time above). While they show, a tap on a zone runs it and restarts the 4 seconds (lab nowb_hide_ms, 0 = never hide); while hidden a tap only shows them, so nothing is pressed by accident\n\n"
               "Top row: Switch set (shows the set name and which one), Menu (the usual options ring), Next page (cover, spectrum, lyrics, cover; the lyrics are skipped when the song has none). Middle row: the two outer zones belong to the current set, the centre is always play / pause. Bottom row: Previous, Close (hides at once), Next\n\n"
               "Sets: the cover page has Play mode (shuffle on / off, repeat all / one); the spectrum page has Style (previous / next style), Colours (previous / next colours) and Play mode. Switch set goes round them. The player cannot seek inside a track yet, so there is no rewind / fast-forward set\n\n"
               "While the labels are up the picture under them (spectrum, cover) stands still. The edge flicks and the two side keys are unchanged",
};

const ui_help_t *const UI_HELPS[] = {
    &HELP_SETTINGS,
    &HELP_FX,
    &HELP_STATUS,
    &HELP_COVERS,
    &HELP_WIFI,
    &HELP_RADIO,
    &HELP_LISTS,
    &HELP_SLOT,
    &HELP_BT,
    &HELP_ALARM,
    &HELP_POMO,
    &HELP_STOPWATCH,
    &HELP_COUNTDOWN,
    &HELP_TIMESET,
    &HELP_EXT,
    &HELP_CAST,
    &HELP_RATETEST,
    &HELP_CARD,
    &HELP_CARDTEST,
    &HELP_GAMES,
    &HELP_SFXBANK,
    &HELP_TUNER,
    &HELP_COUNTER,
    &HELP_AMBIENT,
    &HELP_DRINK,
    &HELP_HABITS,
    &HELP_BREATH,
    &HELP_VIZ,
    &HELP_NINEGRID,
#ifdef SIM
    &HELP_TEST_LONG,
#endif
};

int ui_help_count(void) { return (int)(sizeof UI_HELPS / sizeof UI_HELPS[0]); }
