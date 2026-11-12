#pragma once
// UI部品の共通の道具（ヘッダーだけで使える）
//   Ui::Rgba(r, g, b, a)  : 色を作る（0〜255）。EditorLayout や UI部品に渡す色は、これで作る
//   Ui::Contains(center, size, point) : 四角（中心とサイズ）の中に点があるか
//
// ※UIの絵は UiRenderer（スプライト + 自作の文字画像 BitmapFont）が描くので、Release ビルドでも表示される。
#include "math/Vector2.h"

namespace Ui
{
    // ImGui の IM_COL32 と同じ並びの色（ImGui に依存しないよう自前で作る）
    constexpr unsigned int Rgba(int r, int g, int b, int a = 255)
    {
        return (static_cast<unsigned int>(a) << 24) | (static_cast<unsigned int>(b) << 16)
            | (static_cast<unsigned int>(g) << 8) | static_cast<unsigned int>(r);
    }

    // 色の透明度だけを変える（alpha は 0〜1）
    constexpr unsigned int WithAlpha(unsigned int color, float alpha)
    {
        const float a = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
        return (color & 0x00FFFFFFu) | (static_cast<unsigned int>(a * 255.0f) << 24);
    }

    // 中心 center・大きさ size の四角の中に、点 p があるか
    inline bool Contains(const Vector2& center, const Vector2& size, const Vector2& p)
    {
        return p.x >= center.x - size.x * 0.5f && p.x <= center.x + size.x * 0.5f
            && p.y >= center.y - size.y * 0.5f && p.y <= center.y + size.y * 0.5f;
    }
}
