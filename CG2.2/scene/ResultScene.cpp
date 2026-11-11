#include "ResultScene.h"
#include "TitleScene.h"
#include "WesternShooterScene.h"
#include "scene/SceneManager.h"
#include "input/Input.h"
#include "base/WinApp.h"
#include "editor/EditorLayout.h"

#ifdef USE_IMGUI
#include <cstdio>
#include <cmath>
#include "imgui/imgui.h"
#endif // USE_IMGUI

namespace {
    constexpr float kInputDelay = 0.7f; // 出た直後の連打で飛ばないように、少しだけ入力を無視する
}

void ResultScene::Initialize()
{
    time_ = 0.0f;
}

void ResultScene::Update()
{
    time_ += 1.0f / 60.0f;
    if (time_ < kInputDelay) {
        return;
    }
    if (ctx_->input->TriggerKey(DIK_RETURN) || ctx_->input->TriggerGamepadButton(0)) {
        ctx_->sceneManager->ChangeScene<TitleScene>();
    }
    else if (ctx_->input->TriggerKey(DIK_R) || ctx_->input->TriggerGamepadButton(3)) {
        ctx_->sceneManager->ChangeScene<WesternShooterEntry>();
    }
}

void ResultScene::Draw()
{
}

void ResultScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("Scene: Result (%s)", cleared_ ? "Clear" : "Game Over");
    ImGui::Separator();
    ImGui::Text("ENTER: Title   R: Retry");
    ImGui::End();

    const float cx = WinApp::kGameWidth * 0.5f;
    const float cy = WinApp::kGameHeight * 0.5f;
    EditorLayout::DrawViewportDim(0.6f);
    if (cleared_) {
        EditorLayout::DrawViewportText({ cx, cy - 90.0f }, "BOUNTY CLEARED", 100.0f, IM_COL32(240, 190, 90, 255));
    }
    else {
        EditorLayout::DrawViewportText({ cx, cy - 90.0f }, "YOU DIED", 110.0f, IM_COL32(220, 60, 50, 255));
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Outlaws shot: %d", kills_);
    EditorLayout::DrawViewportText({ cx, cy + 30.0f }, buf, 44.0f, IM_COL32(255, 255, 255, 255));
    if (time_ >= kInputDelay) {
        EditorLayout::DrawViewportText({ cx, cy + 130.0f }, "ENTER: Title      R: Try again", 32.0f, IM_COL32(210, 210, 210, 255));
    }
#endif // USE_IMGUI
}
