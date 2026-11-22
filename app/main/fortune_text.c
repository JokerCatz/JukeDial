// SPDX-License-Identifier: LGPL-2.1-or-later
/* Fortune texts: written for this player (not taken from temple slips or tarot books). const: flash only. */
#include "fortune_text.h"

const char *const fortune_kuji_zh[6] = { "大吉", "吉", "中吉", "小吉", "末吉", "凶" };
const char *const fortune_kuji_en[6] = { "Great luck", "Good luck", "Fair luck", "Small luck", "Late luck", "Bad luck" };
const char *const fortune_aspect_zh[4] = { "音樂", "工作", "人際", "健康" };
const char *const fortune_aspect_en[4] = { "Music", "Work", "People", "Health" };

const fortune_verse_t fortune_verses[] = {
    /* 大吉 (great luck) */
    { 0, "高音穿雲上九天\n一曲終了滿堂歡", "Your high note clears the clouds;\nthe whole hall cheers." },
    { 0, "春風翻過新樂譜\n每一頁都是晴天", "Spring wind turns the score;\nevery page is sunny." },
    { 0, "耳機裡的老歌\n今天唱出新的意思", "An old song in your ears\nsays something new today." },
    { 0, "節拍剛好對上心跳\n走到哪裡都是主旋律", "The beat matches your heart;\nyou are the melody." },
    /* 吉 (luck) */
    { 1, "琴弦調準了\n接下來只管彈", "The strings are in tune;\nnow just play." },
    { 1, "隨機播放正合心意\n好運在下一首", "Shuffle reads your mind;\nluck is in the next track." },
    { 1, "晨光像前奏\n慢慢鋪好今天", "Morning light is the intro,\nlaying out the day." },
    { 1, "和聲找到你\n不必一個人唱", "Harmony finds you;\nno need to sing alone." },
    /* 中吉 (good luck) */
    { 2, "副歌還沒來\n先把主歌唱穩", "The chorus is coming;\nsing the verse steady first." },
    { 2, "音量開一半\n剛好聽得見心聲", "Half volume is enough\nto hear what you mean." },
    { 2, "換一張專輯\n路就換一種顏色", "Change the album\nand the road changes colour." },
    { 2, "慢板也是音樂\n不急的人走得遠", "Adagio is music too;\nthe unhurried go far." },
    /* 小吉 (small luck) */
    { 3, "小小的休止符\n讓下一拍更響亮", "A small rest\nmakes the next beat ring." },
    { 3, "一首冷門歌\n藏著今天的答案", "A forgotten song\nholds today's answer." },
    { 3, "雨聲當節拍器\n安靜也有旋律", "Let the rain keep time;\nquiet has a tune." },
    { 3, "練習曲彈第十遍\n手指記得你的努力", "Tenth time through the etude;\nyour fingers remember." },
    /* 末吉 (late luck) */
    { 4, "前奏有點長\n好戲在後頭", "A long intro;\nthe good part comes later." },
    { 4, "跳針的唱片\n輕輕推一下就好", "A skipping record\nneeds only a gentle push." },
    { 4, "電量剩一格\n先充電再出發", "One bar of battery left;\ncharge, then go." },
    { 4, "走音不要緊\n再唱一次就準了", "Off key is fine;\nsing it once more." },
    /* 凶 (ill luck) */
    { 5, "低音太重蓋過人聲\n今天少說多聽", "The bass drowns the voice;\nlisten more, talk less." },
    { 5, "斷線的耳機\n提醒你抬頭看看四周", "A dropped connection\nsays: look up, look around." },
    { 5, "暴風雨也有節奏\n躲雨時聽一首慢歌", "Storms have rhythm too;\nshelter with a slow song." },
    { 5, "今天先按暫停\n明天的播放清單比較好", "Press pause today;\ntomorrow's playlist is better." },
};
const int fortune_nverses = sizeof fortune_verses / sizeof fortune_verses[0];

const fortune_tarot_t fortune_tarot[22] = {
    { "愚者", "The Fool", "按下播放鍵，不必先看曲目表", "Press play without reading the track list",
      "衝太快會錯過前奏，先停一拍", "Rushing skips the intro: wait one beat" },
    { "魔術師", "The Magician", "手邊的器材就夠，開始創作吧", "The gear you have is enough: start making",
      "技巧再多，也要有想說的話", "All technique, no song: find what to say" },
    { "女祭司", "The High Priestess", "安靜下來，答案在背景音裡", "Get quiet: the answer is in the background",
      "別只聽別人的評論，相信自己的耳朵", "Stop reading reviews; trust your own ears" },
    { "皇后", "The Empress", "滋養身邊的人，像暖暖的和弦", "Nurture those near you, like a warm chord",
      "照顧別人之前，先替自己調音", "Tune yourself before you tend to others" },
    { "皇帝", "The Emperor", "穩定的節拍讓大家跟得上", "A steady beat lets everyone follow",
      "太死板的節奏會讓樂手窒息", "A rigid click track smothers the band" },
    { "教皇", "The Hierophant", "向前輩學一首經典", "Learn a classic from those before you",
      "規則可以打破，但要先懂規則", "Break the rules, but know them first" },
    { "戀人", "The Lovers", "兩個聲部找到和聲", "Two voices find their harmony",
      "選擇卡住了，聽聽心裡比較想聽哪首", "Stuck choosing? Ask which song your heart hums" },
    { "戰車", "The Chariot", "全速前進，音量轉大", "Full speed ahead, volume up",
      "方向不同的樂手，先對好拍子", "Players pulling apart: count in together" },
    { "力量", "Strength", "溫柔的弱音也能撐起整首歌", "A soft pianissimo can hold a whole song",
      "別硬撐，休息也是練習的一部分", "Don't force it: rest is part of practice" },
    { "隱者", "The Hermit", "一個人戴上耳機，好好想一想", "Headphones on, alone, think it through",
      "關太久了，出來和大家合奏吧", "Too long alone: come out and jam" },
    { "命運之輪", "Wheel of Fortune", "隨機播放轉到好歌，順勢而為", "Shuffle lands on a gem: go with it",
      "低潮會過去，下一首就換了", "A slump passes; the next track is different" },
    { "正義", "Justice", "公平分配每個聲部的音量", "Give every part its fair share of the mix",
      "混音失衡了，重新檢查一次", "The mix is off balance: check it again" },
    { "吊人", "The Hanged Man", "倒過來聽，會聽到新的東西", "Listen upside down and hear something new",
      "停滯太久，該換一首了", "Stalled too long: change the song" },
    { "死神", "Death", "一張專輯結束，下一張正要開始", "One album ends, the next begins",
      "捨不得結尾，讓歌一直重播", "Afraid of the ending, stuck on repeat" },
    { "節制", "Temperance", "把兩種風格慢慢混在一起", "Blend two styles, slowly",
      "等化器拉太多，回到平坦試試", "EQ pushed too far: try it flat" },
    { "惡魔", "The Devil", "承認自己愛那首罪惡快感歌", "Own your guilty-pleasure song",
      "從無限重播裡掙脫出來", "Break free of the endless loop" },
    { "高塔", "The Tower", "舊的編曲倒下，新的空間出現", "The old arrangement falls; room for new",
      "小小的失誤，別讓它變成大崩潰", "A small slip; don't let it become a crash" },
    { "星星", "The Star", "夜空般的長音，帶來希望", "A long note like a night sky, full of hope",
      "暫時看不到星星，雲會散的", "No stars tonight; the clouds will part" },
    { "月亮", "The Moon", "夢一般的旋律，跟著感覺走", "A dreamlike melody: follow the feeling",
      "迷霧散去，看清楚真正的聲音", "The fog lifts: hear what is really there" },
    { "太陽", "The Sun", "大調的好日子，盡情唱", "A major-key day: sing out",
      "陽光有點刺眼，戴上墨鏡慢慢走", "Bright glare: put on shades, slow down" },
    { "審判", "Judgement", "重聽舊作，聽見自己的成長", "Replay old work and hear how you grew",
      "別對過去的走音太嚴苛", "Go easy on your old wrong notes" },
    { "世界", "The World", "整首曲子完成了，謝幕", "The piece is complete: take a bow",
      "差最後一小節，把它收好", "One last bar to go: finish it" },
};
