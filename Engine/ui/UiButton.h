#pragma once
// ボタン（ヘッダーだけで使える）。座標はどれもゲーム画面のピクセル（中心が基準）
//
//   // 初期化
//   UiButton start_;
//   start_.Set({ 640.0f, 400.0f }, { 360.0f, 64.0f }, "START");
//
//   // 毎フレームの Update で
//   if (start_.UpdateFromViewport()) {      // クリックされた瞬間だけ true
//       sceneManager_.ChangeScene<GameScene>();
//   }
//
//   // Draw で（UiRenderer に描いてもらう）
//   start_.Draw(ui_);
//
//   ・SetEnabled(false) にすると、灰色になってクリックできない
//   ・マウスの位置を自分で渡したいとき（Release ビルドなど）は Update(mouse, mouseValid, clicked) を使う
#include <string>

#include "UiCommon.h"
#include "UiRenderer.h"
#ifdef USE_IMGUI
#include "editor/EditorLayout.h"
#endif // USE_IMGUI

class UiButton
{
public:
    void Set(const Vector2& center, const Vector2& size, const std::string& label)
    {
        center_ = center;
        size_ = size;
        label_ = label;
    }

    void SetLabel(const std::string& label) { label_ = label; }
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    void SetTextSize(float px) { textSize_ = px; }

    // mouse: マウス位置（ゲーム画面のピクセル）、mouseValid: マウスが画面内にあるか、clicked: このフレームに左クリックしたか
    // 戻り値: クリックされた瞬間だけ true
    bool Update(const Vector2& mouse, bool mouseValid, bool clicked)
    {
        hover_ = enabled_ && mouseValid && Ui::Contains(center_, size_, mouse);
        return hover_ && clicked;
    }

#ifdef USE_IMGUI
    // ゲーム画面の上のマウスを使って判定する（ImGui のビューポート上のマウス。Debug / Development 用）
    bool UpdateFromViewport()
    {
        const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
        return Update(vp.mouse, vp.valid, vp.clicked);
    }
#endif // USE_IMGUI

    bool IsHover() const { return hover_; }
    bool IsEnabled() const { return enabled_; }

    void Draw(UiRenderer& ui) const
    {
        unsigned int back = Ui::Rgba(70, 50, 30, 220);
        unsigned int text = Ui::Rgba(220, 205, 175);
        if (!enabled_) {
            back = Ui::Rgba(50, 50, 50, 170);
            text = Ui::Rgba(130, 130, 130);
        }
        else if (hover_) {
            back = Ui::Rgba(150, 95, 35, 235);
            text = Ui::Rgba(255, 240, 200);
        }
        ui.DrawRect(center_, { size_.x + 4.0f, size_.y + 4.0f }, Ui::Rgba(0, 0, 0, 200)); // 黒い枠
        ui.DrawRect(center_, size_, back);
        ui.DrawLabel(center_, label_, textSize_, text);
    }

private:
    Vector2 center_{ 0.0f, 0.0f };
    Vector2 size_{ 200.0f, 50.0f };
    std::string label_;
    float textSize_ = 34.0f;
    bool enabled_ = true;
    bool hover_ = false;
};
