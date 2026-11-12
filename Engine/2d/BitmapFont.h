#pragma once
// ビットマップフォント（自作した「文字画像」を並べて文字を出す部品）
//
//   フォントのデータ（.ttf など）は読み込まない。Aseprite などで自分で作った文字画像（PNG）を読み込んで、
//   1文字ずつ切り出して並べる。Release ビルドでも使える（ImGui を使わない）。
//
// ■ 文字画像の作り方
//   ・白い文字、背景は透明の PNG にする（色はプログラムで変えられる）
//   ・同じ大きさのマスを並べる。1マス = cellW x cellH ピクセル
//   ・1行に columns 個ずつ、左上から右へ、行ごとに下へ、決めた順に文字を並べる
//   ・何も指定しなければ、ASCII の「スペース ! " # ... ~」の95文字をこの順に並べる
//       例: 1マス 16x32、16列 x 6行 → 画像の大きさは 256 x 192
//   ・日本語や記号を使いたいときは、並べた文字をそのまま文字列で渡す（Initialize の charset）
//
// ■ 使い方
//   BitmapFont font_;
//   font_.Initialize(ctx_->spriteCommon, "resources/font.png", 16, 32, 16);   // シーンの Initialize で
//   font_.Finalize();                                                          // シーンの Finalize で
//
//   // 毎フレームの Draw（SpriteCommon::PreDraw() のあとに）
//   font_.BeginFrame();
//   font_.Draw("SCORE 1200", { 40.0f, 30.0f }, 32.0f, { 1.0f, 1.0f, 1.0f, 1.0f });  // 左上の位置, 文字の高さ(px), 色(RGBA)
//
//   ・同じ画面に出す文字の数だけ、スプライトを使い回す（足りなくなったら自動で増える）
//   ・文字の幅は、すべて同じ（等幅）
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "2d/Sprite.h"
#include "2d/SpriteCommon.h"
#include "math/Vector2.h"
#include "math/Vector4.h"

class BitmapFont
{
public:
    // texturePath: 文字画像 / cellW, cellH: 1マスの大きさ（画像のピクセル） / columns: 1行に並べたマスの数
    // charset: 画像に並べた文字（UTF-8）。空なら ASCII の 0x20〜0x7E
    void Initialize(SpriteCommon* common, const std::string& texturePath, int cellW, int cellH, int columns, const std::string& charset = "")
    {
        common_ = common;
        texturePath_ = texturePath;
        cellW_ = cellW > 0 ? cellW : 1;
        cellH_ = cellH > 0 ? cellH : 1;
        columns_ = columns > 0 ? columns : 1;
        pool_.clear();
        used_ = 0;
        glyphIndex_.clear();

        int index = 0;
        if (charset.empty()) {
            for (char32_t c = 0x20; c <= 0x7E; ++c) {
                glyphIndex_[c] = index++;
            }
        }
        else {
            size_t pos = 0;
            char32_t c = 0;
            while (NextCodePoint(charset, pos, c)) {
                if (glyphIndex_.find(c) == glyphIndex_.end()) {
                    glyphIndex_[c] = index;
                }
                ++index; // 重複した文字も、マスの数には数える
            }
        }
    }

    // デバイスより先にGPUリソースを解放する
    void Finalize()
    {
        pool_.clear();
        used_ = 0;
    }

    // フレームの最初に1回呼ぶ（スプライトの使い回しを最初に戻す）
    void BeginFrame() { used_ = 0; }

    // 文字を描く。leftTop は文字列の左上（ゲーム画面のピクセル）、heightPx は文字の高さ、color は RGBA（0〜1）
    void Draw(const std::string& utf8, const Vector2& leftTop, float heightPx, const Vector4& color)
    {
        if (common_ == nullptr) {
            return;
        }
        const float scale = heightPx / static_cast<float>(cellH_);
        const float glyphW = static_cast<float>(cellW_) * scale;
        const float advance = glyphW * (1.0f + letterSpacing);

        float x = leftTop.x;
        float y = leftTop.y;
        size_t pos = 0;
        char32_t c = 0;
        while (NextCodePoint(utf8, pos, c)) {
            if (c == U'\n') { // 改行：左端に戻って、1行ぶん下へ
                x = leftTop.x;
                y += heightPx * (1.0f + lineSpacing);
                continue;
            }
            const auto it = glyphIndex_.find(c);
            if (it != glyphIndex_.end() && c != U' ') {
                DrawGlyph(it->second, { x, y }, { glyphW, heightPx }, color);
            }
            x += advance;
        }
    }

    // 文字列の横幅（ピクセル）。中央寄せ・右寄せの計算に使う
    float Measure(const std::string& utf8, float heightPx) const
    {
        const float scale = heightPx / static_cast<float>(cellH_);
        const float advance = static_cast<float>(cellW_) * scale * (1.0f + letterSpacing);
        int count = 0;
        size_t pos = 0;
        char32_t c = 0;
        while (NextCodePoint(utf8, pos, c)) {
            ++count;
        }
        return advance * static_cast<float>(count);
    }

    // この文字が文字画像にあるか
    bool HasGlyph(char32_t c) const { return glyphIndex_.find(c) != glyphIndex_.end(); }

    // 文字と文字のすきま（文字の幅に対する割合。0.1 なら幅の1割だけ広げる。マイナスで詰める）
    float letterSpacing = 0.0f;

    // 行と行のすきま（文字の高さに対する割合）。改行（\n）を使うときだけ効く
    float lineSpacing = 0.2f;

    // UTF-8 の文字列から、次の1文字（コードポイント）を取り出す。取り出せたら true
    // 壊れた並びのときは、その1バイトを読み飛ばして ? の代わりに 0xFFFD を返す
    static bool NextCodePoint(const std::string& s, size_t& pos, char32_t& out)
    {
        if (pos >= s.size()) {
            return false;
        }
        const unsigned char b0 = static_cast<unsigned char>(s[pos]);
        int extra = 0;
        char32_t cp = 0;
        if (b0 < 0x80) { cp = b0; extra = 0; }
        else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; extra = 1; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; extra = 2; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; extra = 3; }
        else { ++pos; out = 0xFFFD; return true; }

        if (pos + static_cast<size_t>(extra) >= s.size()) { // 文字の途中で文字列が終わっている
            ++pos;
            out = 0xFFFD;
            return true;
        }
        for (int i = 1; i <= extra; ++i) {
            const unsigned char b = static_cast<unsigned char>(s[pos + static_cast<size_t>(i)]);
            if ((b & 0xC0) != 0x80) {
                ++pos;
                out = 0xFFFD;
                return true;
            }
            cp = (cp << 6) | (b & 0x3F);
        }
        pos += static_cast<size_t>(extra) + 1;
        out = cp;
        return true;
    }

private:
    void DrawGlyph(int index, const Vector2& leftTop, const Vector2& size, const Vector4& color)
    {
        if (used_ >= pool_.size()) {
            auto sprite = std::make_unique<Sprite>();
            sprite->Initialize(common_, texturePath_, { static_cast<float>(cellW_), static_cast<float>(cellH_) });
            pool_.push_back(std::move(sprite));
        }
        Sprite& sprite = *pool_[used_++];
        const int col = index % columns_;
        const int row = index / columns_;
        sprite.SetTextureRect({ static_cast<float>(col * cellW_), static_cast<float>(row * cellH_) },
            { static_cast<float>(cellW_), static_cast<float>(cellH_) });
        sprite.SetSize(size);
        sprite.GetTransform().Translate = { leftTop.x, leftTop.y, 0.0f };
        sprite.GetMaterial()->color = color;
        sprite.Update();
        sprite.Draw();
    }

    SpriteCommon* common_ = nullptr;
    std::string texturePath_;
    int cellW_ = 1;
    int cellH_ = 1;
    int columns_ = 1;
    std::unordered_map<char32_t, int> glyphIndex_;
    std::vector<std::unique_ptr<Sprite>> pool_;
    size_t used_ = 0;
};
