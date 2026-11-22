/* The theme's colour tokens: ONE table (X-macros; also read by tools/gen/gen_theme_template.py, which writes the commented template file of a theme).
 *   CORE(key, zh, en)                       the nine colours of pc_theme_t (components/player_core pc_theme_table.inc); a theme file sets them as `key = #RRGGBB`
 *   TOK(id, "key", n, (defaults...), zh, en) a named palette of n colours (n = 1: a plain token). A theme file sets entry i of an n > 1 palette as `key.i = #RRGGBB`.
 * Defaults are the built-in look: a page never holds a colour literal, it reads ui_tok(UI_PAL_<id>) / ui_pal(UI_PAL_<id>)[i] (ui_tokens.h).
 * zh/en say what the colour is and where it shows (the template file prints both). 0xFFFFFFFF = "not set": the colour follows another token (see the text). */
CORE(bg,     "整個畫面的底色", "The page background behind everything")
CORE(panel,  "面板／格子底色（未選取的格子、卡片的基底）", "Panels and cells that are not selected; the base the cards are mixed from")
CORE(panel2, "第二層面板（環形選單的格子、列表的雙數列）", "Second panel level: ring items, every other list row, plain help cells")
CORE(fg,     "文字與圖示的主色", "Main text and icon colour")
CORE(dim,    "次要文字（說明、單位、未啟用）", "Secondary text: hints, units, inactive things")
CORE(a1,     "第一強調色：選取、進度、頻譜主色（霓虹主題為青色）", "First accent: selection, progress, the spectrum's main colour")
CORE(a2,     "第二強調色：系統項目（返回、下一頁）、頻譜第二色", "Second accent: system items (Back, next page), the spectrum's second colour")
CORE(a3,     "第三強調色：頻譜第三色、遊戲的高亮", "Third accent: the spectrum's third colour, game highlights")
CORE(on,     "「開」的顏色（開關、已啟用）；請與 a1 色相差 60 度以上", "The 'switched on' colour (toggles, active); keep it 60 degrees or more from a1")

TOK(warn, "warn", 1, (0xFF4040), "警示色：錯誤框、掉球、危險", "Warning: an error frame, a lost ball, danger")
TOK(gold, "gold", 1, (0xFFC940), "金色：贏、最佳、幸運記號（運勢、骰子連線、答題十連對）", "Gold: a win, a best, a lucky mark (fortune, dice, quiz streak)")
TOK(help, "help", 1, (0xFFFFFFFF), "每個「說明」格／列的底色。不設定 = 與 panel2 相同（預設外觀）", "Fill of every Help (說明) cell, row and popup item. Unset = same as panel2 (the default look)")
TOK(help_text, "help_text", 1, (0xFFFFFFFF), "「說明」格上的文字色。不設定 = 與 fg 相同", "Text on a Help cell. Unset = same as fg")

TOK(dice, "dice", 4, (0xF4F1EA, 0x202020, 0xD03030, 0x202020),
    "骰子（小工具）：0 骰面 1 點 2 亮起的點（指定的那一點）3 熄滅的點", "Dice toy: 0 die face, 1 pip, 2 lit pip, 3 unlit pip")
TOK(slot_lines, "slot_lines", 5, (0xFF5A5A, 0xFFC940, 0x5EC8E5, 0x7BD88F, 0xC07CFF),
    "拉霸（小工具）：五條連線的顏色", "Slot machine toy: the five pay-line colours")
TOK(kuji, "kuji", 6, (0xFFC940, 0xFF8A3D, 0x7BD88F, 0x5EC8E5, 0xB59CFF, 0x8A94A6),
    "御神籤（運勢）：六個籤運等級的字色與籤條色，由大吉到凶", "Fortune (omikuji): the six rank colours (text and the stick), best to worst")
TOK(kuji_stick, "kuji_stick", 1, (0xE8D8B0), "御神籤：籤筒裡的籤與抽出的籤的木色", "Fortune (omikuji): the wooden sticks in the can and the drawn one")
TOK(jiao, "jiao", 2, (0xE0524A, 0x9B2C24), "擲筊：0 筊杯平面朝上的紅 1 圓面朝上的暗紅", "Jiaobei (moon blocks): 0 flat side up, 1 round side up")
TOK(rarity, "rarity", 4, (0x9AA3AF, 0x4FA3FF, 0xC07CFF, 0xFFC940), "塔羅／卡片稀有度的框色：普通、稀有、史詩、傳說", "Tarot / card rarity frames: common, rare, epic, legendary")
TOK(tarot_rev, "tarot_rev", 1, (0xB59CFF), "塔羅：逆位的牌位文字色", "Tarot: the text colour of a reversed card")
TOK(quiz, "quiz", 2, (0x2FA35A, 0xD04545), "單字測驗：0 答對的格 1 答錯的格", "Vocabulary quiz: 0 the right answer, 1 a wrong one")
TOK(moon, "moon", 2, (0xF2E8C8, 0x2A3038), "時鐘的月相：0 亮面 1 暗面", "Clock moon phase: 0 lit side, 1 dark side")
TOK(inst_voices, "inst_voices", 6, (0xFF8040, 0x40C0FF, 0xC060FF, 0x60FF90, 0xFFE040, 0xFF60A0),
    "樂器：第 4 個起的聲部顏色（前三個用 a1 a2 a3）", "Instrument: voice colours from the 4th voice on (the first three use a1 a2 a3)")
TOK(cards, "cards", 7, (0xECE6D0, 0x8A8470, 0x25262B, 0x3C5A8C, 0xE9C14A, 0x3A2A00, 0xFFE9A0),
    "撲克牌／骨牌（經典風格）：0 牌面 1 牌框 2 點數 3 牌背 4 金牌牌面 5 金牌點數 6 金牌牌框", "Cards and dominoes (classic style): 0 face, 1 rim, 2 pips, 3 back, 4 gold face, 5 gold pips, 6 gold rim")
TOK(dial, "dial", 8, (0x202020, 0x808080, 0xC0C0C0, 0xE0E0E0, 0xFFFFFF, 0x303030, 0xFF8040, 0xFF8040),
    "刻度盤／指針表（旋鈕玩具、儀表）：0 盤底 1-4 刻度由暗到亮 5 軌道 6 指針 7 提示", "Dials and meters: 0 face, 1-4 ticks dark to bright, 5 track, 6 needle, 7 cue")

TOK(game_classic, "game_classic", 4, (0xFF5252, 0x448AFF, 0xFFD740, 0x3DDC84),
    "遊戲「經典」風格：四色記憶的四個扇形顏色（其餘顏色跟隨主題）", "Games, Classic style: the four sector colours of the memory game (everything else follows the theme)")
TOK(game_neon, "game_neon", 10, (0x0B0B16, 0x34346A, 0x19E6FF, 0xFF4FD8, 0xFFF27A, 0x7DF9FF, 0xFF2BD6, 0x19E6FF, 0xFFF01F, 0x39FF14),
    "遊戲「霓虹」風格：0 空格 1 格線 2 甲方 3 乙方 4 高亮 5 特效 6-9 四個扇形", "Games, Neon style: 0 empty cell, 1 grid lines, 2 side A, 3 side B, 4 highlight, 5 effects, 6-9 the four sectors")
TOK(game_candy, "game_candy", 10, (0x3B2F4A, 0x5A4870, 0xFF8FAB, 0x8FD8FF, 0xFFE66D, 0xFFD1E0, 0xFF9AA2, 0xA0C4FF, 0xFFE08A, 0x9BE7B0),
    "遊戲「糖果」風格：順序同 game_neon", "Games, Candy style: same order as game_neon")
TOK(game_gold, "game_gold", 10, (0x2A2A2A, 0x444444, 0xFFC43D, 0xD9DEE8, 0xFFF1A8, 0xFFD76A, 0xFFC43D, 0xD9DEE8, 0xD08A4A, 0xF2A7C0),
    "黃金遊戲（稀有彩蛋）：順序同 game_neon；0 1 只在「經典」風格下當金色的中性場地", "The golden game (a rare surprise): same order as game_neon; 0 and 1 are the neutral field used when the base style is Classic")
TOK(game_board, "game_board", 6, (0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF),
    "遊戲「經典」風格的盤面（淺色主題用，讓空格／棋子在亮底上看得見）：0 空格 1 格線 2 甲方 3 乙方 4 高亮 5 特效。不設定 = 跟隨主題（panel panel2 a1 on a3 a1）", "Games, Classic style board (for light themes, so empty cells and pieces read on a light page): 0 empty cell, 1 grid lines, 2 side A, 3 side B, 4 highlight, 5 effects. Unset = follow the theme (panel panel2 a1 on a3 a1)")
TOK(game_field, "game_field", 3, (0xFF7A3D, 0xFFC43D, 0xFFFFFF), "場地遊戲（環形打磚塊）：0 爆開的磚 1 金磚 2 非經典風格的球", "Field games (ring breakout): 0 a bursting brick, 1 the golden brick, 2 the ball in the non-classic styles")

#define AMB(S, s, a, b, c) \
TOK(amb_##s##_1, "amb_" #s "_1", 7, a, "環境光「" S "」風格 1：0 底 1-6 六個色調（1 也是外框）", "Ambient light, scene " #s ", style 1: 0 background, 1-6 the six tones (1 is also the rim)") \
TOK(amb_##s##_2, "amb_" #s "_2", 7, b, "環境光「" S "」風格 2：順序同風格 1", "Ambient light, scene " #s ", style 2: same order") \
TOK(amb_##s##_3, "amb_" #s "_3", 7, c, "環境光「" S "」風格 3：順序同風格 1", "Ambient light, scene " #s ", style 3: same order")
AMB("熔岩", lava, (0x1A0B0B, 0x7A1F10, 0xFF5A2A, 0xFF8A3A, 0xFFD35A, 0xFFFFFF, 0xFFFFFF), (0x05061A, 0x1A3A9A, 0x20D0FF, 0xC040FF, 0xFFF060, 0xFFFFFF, 0xFFFFFF), (0x2A1A30, 0xA05A90, 0xFF8FC0, 0x9FD8FF, 0xFFF0A0, 0xFFFFFF, 0xFFFFFF))
AMB("魚缸", fish, (0x08202C, 0xE8C080, 0xFF9A30, 0x50C0D0, 0xBFE8FF, 0xFFD700, 0x101010), (0x020A10, 0xFF40A0, 0x30FFB0, 0xFF50D0, 0x70E0FF, 0xFFEE33, 0x000000), (0x23405A, 0xFFF0D0, 0xFFA0A0, 0xA0FFD0, 0xE0F4FF, 0xFFE880, 0x402030))
AMB("營火", fire, (0x120808, 0x3A3030, 0xC42010, 0xFF7A10, 0xFFC830, 0xFFF2B0, 0x4A2A14), (0x040810, 0x203050, 0x3030FF, 0x20A0FF, 0x40F0FF, 0xE8FFFF, 0x202A40), (0x2A1C2C, 0x70507A, 0xFF6A8A, 0xFFA070, 0xFFE090, 0xFFFFF0, 0x7A5A4A))
AMB("夜空", night, (0x04040C, 0x2A1850, 0x6A2A80, 0xC05A60, 0xFFB070, 0xFFFFFF, 0xFFFFFF), (0x000008, 0x0A2A50, 0x10709A, 0x20C0A0, 0x90FFB0, 0xC0F0FF, 0xFFFFFF), (0x1C1830, 0x4A3A7A, 0x9A70B0, 0xF0A0C0, 0xFFF0C0, 0xFFF8E0, 0xFFFFFF))
#undef AMB
