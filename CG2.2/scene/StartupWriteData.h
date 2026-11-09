#pragma once
// startup_glyphs.png（1文字ずつ並べた画像）と startup_sub.png / startup_line.png の寸法データ
// 文字の画像を作り直したら、このデータも作り直す

constexpr int kTextW = 1107;   // 文字全体の大きさ（1文字ずつの位置の基準）
constexpr int kTextH = 160;
constexpr int kGlyphCount = 13;
// 各文字が、元の文字列の何文字目か（登場の順番に使う）
constexpr int kGlyphIndex[] = { 0, 1, 2, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14 };
// 画像（アトラス）の中の、各文字の位置と大きさ
constexpr int kGlyphAtlasX[] = { 0, 90, 115, 200, 282, 354, 426, 490, 592, 689, 777, 812, 909 };
constexpr int kGlyphW[] = { 86, 21, 81, 78, 68, 68, 60, 98, 93, 84, 31, 93, 75 };
constexpr int kGlyphH[] = { 104, 96, 104, 104, 80, 80, 102, 104, 78, 104, 93, 78, 73 };
// 文字全体の中での、各文字の左上の位置
constexpr int kGlyphDX[] = { 18, 114, 140, 264, 346, 417, 492, 595, 695, 791, 880, 913, 1014 };
constexpr int kGlyphDY[] = { 20, 20, 12, 17, 42, 42, 19, 21, 43, 44, 18, 43, 46 };
constexpr int kSubW = 424;
constexpr int kSubH = 76;
constexpr int kLineW = 640;
constexpr int kLineH = 6;
constexpr int kStringLength = 15;
