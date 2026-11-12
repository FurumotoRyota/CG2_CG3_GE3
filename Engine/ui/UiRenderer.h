#pragma once
// UI部品を画面に描く係（四角と文字）。UiButton / UiGauge / ScreenFade / UiToast は、これに描いてもらう
//
//   スプライトだけで描くので、Release ビルドでも表示される（ImGui を使わない）。
//   文字は BitmapFont（自作の文字画像）で描く。
//
//   // 初期化（シーンの Initialize で）
//   font_.Initialize(ctx_->spriteCommon, "resources/font.png", 16, 32, 16);
//   ui_.Initialize(ctx_->spriteCommon, &font_);
//
//   // 後始末（シーンの Finalize で。ui_ → font_ の順）
//   ui_.Finalize();
//   font_.Finalize();
//
//   // 毎フレーム（シーンの Draw で、SpriteCommon::PreDraw() のあとに）
//   ui_.BeginFrame();
//   button_.Draw(ui_);
//   hp_.Draw(ui_, "HP");
//   fade_.Draw(ui_);          // フェードは最後に呼ぶ（いちばん手前に出す）
//
//   ※描いた順に重なる（あとに描いたものが手前）
#include <memory>
#include <string>
#include <vector>

#include "2d/BitmapFont.h"
#include "2d/Sprite.h"
#include "2d/SpriteCommon.h"
#include "UiCommon.h"
#include "math/Vector2.h"
#include "math/Vector4.h"

class UiRenderer
{
public:
    // whiteTexture: 真っ白な画像（四角を塗るのに使う）。エンジンに入っているものが初期値
    void Initialize(SpriteCommon* common, BitmapFont* font, const std::string& whiteTexture = "Engine/resources/startup/startup_white.png")
    {
        common_ = common;
        font_ = font;
        whiteTexture_ = whiteTexture;
        rects_.clear();
        rectsUsed_ = 0;
    }

    void Finalize()
    {
        rects_.clear();
        rectsUsed_ = 0;
    }

    // フレームの最初に1回呼ぶ
    void BeginFrame()
    {
        rectsUsed_ = 0;
        if (font_ != nullptr) {
            font_->BeginFrame();
        }
    }

    // 塗りつぶしの四角。center は中心、size は大きさ、color は Ui::Rgba で作った色（透明度つき）
    void DrawRect(const Vector2& center, const Vector2& size, unsigned int color)
    {
        if (common_ == nullptr) {
            return;
        }
        if (rectsUsed_ >= rects_.size()) {
            auto sprite = std::make_unique<Sprite>();
            sprite->Initialize(common_, whiteTexture_, { 1.0f, 1.0f });
            rects_.push_back(std::move(sprite));
        }
        Sprite& sprite = *rects_[rectsUsed_++];
        sprite.SetSize(size);
        sprite.GetTransform().Translate = { center.x - size.x * 0.5f, center.y - size.y * 0.5f, 0.0f };
        sprite.GetMaterial()->color = ToVector4(color);
        sprite.Update();
        sprite.Draw();
    }

    // 文字（中央寄せ）。center は文字列の中心、heightPx は文字の高さ。影つき
    void DrawLabel(const Vector2& center, const std::string& text, float heightPx, unsigned int color)
    {
        if (font_ == nullptr) {
            return;
        }
        const float w = font_->Measure(text, heightPx);
        DrawLabelLeft({ center.x - w * 0.5f, center.y }, text, heightPx, color);
    }

    // 文字（左寄せ）。leftCenter は文字列の左端の、縦の中心。影つき
    void DrawLabelLeft(const Vector2& leftCenter, const std::string& text, float heightPx, unsigned int color)
    {
        if (font_ == nullptr) {
            return;
        }
        const Vector2 leftTop = { leftCenter.x, leftCenter.y - heightPx * 0.5f };
        const float off = (heightPx * 0.06f > 1.0f) ? heightPx * 0.06f : 1.0f;
        font_->Draw(text, { leftTop.x + off, leftTop.y + off }, heightPx, { 0.0f, 0.0f, 0.0f, 0.86f * ToVector4(color).w }); // 影
        font_->Draw(text, leftTop, heightPx, ToVector4(color));
    }

    // Ui::Rgba の色を、0〜1 の RGBA に直す
    static Vector4 ToVector4(unsigned int c)
    {
        return { static_cast<float>(c & 0xFFu) / 255.0f, static_cast<float>((c >> 8) & 0xFFu) / 255.0f,
                 static_cast<float>((c >> 16) & 0xFFu) / 255.0f, static_cast<float>((c >> 24) & 0xFFu) / 255.0f };
    }

private:
    SpriteCommon* common_ = nullptr;
    BitmapFont* font_ = nullptr;
    std::string whiteTexture_;
    std::vector<std::unique_ptr<Sprite>> rects_;
    size_t rectsUsed_ = 0;
};
