#include "PauseMenu.h"

#include "base/WinApp.h"
#include "editor/EditorLayout.h"
#include "input/Input.h"
#include "math/Vector2.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif

namespace {
    // 項目の並び（ゲーム画面のピクセル座標）。判定と描画で同じ値を使う
    constexpr float kItemTop = 0.42f;    // 1つ目の高さ（画面の高さに対する割合）
    constexpr float kItemStep = 74.0f;   // 項目の間隔
    constexpr float kItemW = 560.0f;
    constexpr float kItemH = 56.0f;
    constexpr int kPadStart = 7;         // ゲームパッドの Start ボタン（コントローラーによって番号が違うことがある）

    Vector2 ItemCenter(int i)
    {
        return { WinApp::kGameWidth * 0.5f, WinApp::kGameHeight * kItemTop + kItemStep * static_cast<float>(i) };
    }
}

bool PauseMenu::TogglePressed(const Input& input)
{
    return input.TriggerKey(DIK_ESCAPE) || input.TriggerKey(DIK_P) || input.TriggerGamepadButton(kPadStart);
}

void PauseMenu::Open()
{
    open_ = true;
    selected_ = 0;
    wait_ = 2;
}

int PauseMenu::Update(const Input& input, bool canToggle)
{
    if (canToggle && TogglePressed(input)) {
        if (open_) {
            open_ = false;
            return kClosed;
        }
        Open();
        return kNone;
    }
    if (!open_ || items_.empty()) {
        return kNone;
    }
    const int count = static_cast<int>(items_.size());

    // キー・十字キーで選ぶ（端でぐるっと回る）
    if (input.TriggerKey(DIK_UP) || input.TriggerKey(DIK_W) || input.TriggerPov(0)) {
        selected_ = (selected_ + count - 1) % count;
    }
    if (input.TriggerKey(DIK_DOWN) || input.TriggerKey(DIK_S) || input.TriggerPov(18000)) {
        selected_ = (selected_ + 1) % count;
    }

    bool clicked = false;
#ifdef USE_IMGUI
    {
        // マウスが乗った項目を選ぶ。そのままクリックで決める
        const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
        if (vp.valid) {
            for (int i = 0; i < count; ++i) {
                const Vector2 c = ItemCenter(i);
                if (vp.mouse.x >= c.x - kItemW * 0.5f && vp.mouse.x <= c.x + kItemW * 0.5f
                    && vp.mouse.y >= c.y - kItemH * 0.5f && vp.mouse.y <= c.y + kItemH * 0.5f) {
                    if (vp.mouse.x != lastMouse_.x || vp.mouse.y != lastMouse_.y || vp.clicked) {
                        selected_ = i; // 動かしたときだけ（キー操作の選択を奪わない）
                    }
                    clicked = vp.clicked && selected_ == i;
                }
            }
            lastMouse_ = vp.mouse;
        }
    }
#endif // USE_IMGUI

    if (wait_ > 0) {
        --wait_; // 開いた瞬間の入力（開いたキーと同じ Enter など）では決まらないようにする
        return kNone;
    }
    if (clicked || input.TriggerKey(DIK_RETURN) || input.TriggerKey(DIK_SPACE) || input.TriggerGamepadButton(0)) {
        return selected_;
    }
    return kNone;
}

void PauseMenu::Draw() const
{
#ifdef USE_IMGUI
    if (!open_) {
        return;
    }
    EditorLayout::DrawViewportDim(0.65f);
    EditorLayout::DrawViewportText({ WinApp::kGameWidth * 0.5f, WinApp::kGameHeight * 0.24f }, title_.c_str(), 96.0f, IM_COL32(240, 190, 90, 255));
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const bool sel = (i == selected_);
        const Vector2 c = ItemCenter(i);
        if (sel) {
            EditorLayout::DrawViewportBar(c, { kItemW, kItemH }, 1.0f, IM_COL32(150, 95, 35, 230)); // 選んでいる項目の下地
        }
        EditorLayout::DrawViewportText(c, items_[i].c_str(), 38.0f, sel ? IM_COL32(255, 240, 200, 255) : IM_COL32(200, 190, 170, 255));
    }
    EditorLayout::DrawViewportText({ WinApp::kGameWidth * 0.5f, WinApp::kGameHeight * 0.9f }, "UP/DOWN or W/S : select    ENTER / CLICK : OK    ESC / P : back", 24.0f, IM_COL32(190, 180, 160, 255));
#endif // USE_IMGUI
}
