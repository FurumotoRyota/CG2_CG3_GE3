#include "TitleScene.h"
#include "WesternShooterScene.h"
#include "scene/SceneManager.h"
#include <algorithm>
#include "input/Input.h"
#include "base/WinApp.h"
#include "editor/EditorLayout.h"

#ifdef USE_IMGUI
#include <cmath>
#include "imgui/imgui.h"
#endif // USE_IMGUI

void TitleScene::Initialize()
{
    time_ = 0.0f;
    fromStartup_ = s_fromStartup;
    s_fromStartup = false;
}

void TitleScene::Update()
{
    time_ += 1.0f / 60.0f;
    bool click = false;
#ifdef USE_IMGUI
    click = EditorLayout::GetViewportInput().clicked;
#endif // USE_IMGUI
    if (ctx_->input->TriggerKey(DIK_RETURN) || ctx_->input->TriggerKey(DIK_SPACE)
        || ctx_->input->TriggerGamepadButton(0) || click) {
        ctx_->sceneManager->ChangeScene<WesternShooterScene>();
    }
}

void TitleScene::Draw()
{
    // 何も描かない（背景色のみ。文字は DrawImGui でゲーム画面の上に重ねる）
}

void TitleScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("Scene: Title");
    ImGui::Separator();
    ImGui::Text("ENTER / SPACE / Pad A / Click");
    ImGui::End();

    const float cx = WinApp::kGameWidth * 0.5f;
    const float cy = WinApp::kGameHeight * 0.5f;
    EditorLayout::DrawViewportDim(0.55f);
    // スタートアップから来たときは、桜色の幕がゆっくり消えて、そのままタイトルが現れる（暗転しない）
    if (fromStartup_) {
        const float open = (std::min)(time_ / 1.0f, 1.0f);
        const float veil = 1.0f - open * open * (3.0f - 2.0f * open);
        EditorLayout::DrawViewportTint(255, 219, 230, veil);
    }
    EditorLayout::DrawViewportText({ cx, cy - 90.0f }, "WILD WEST", 110.0f, IM_COL32(240, 190, 90, 255));
    EditorLayout::DrawViewportText({ cx, cy + 10.0f }, "SHOOTOUT", 110.0f, IM_COL32(240, 190, 90, 255));
    const float blink = 0.5f + 0.5f * std::sin(time_ * 4.0f);
    EditorLayout::DrawViewportText({ cx, cy + 140.0f }, "PRESS ENTER", 40.0f, IM_COL32(255, 255, 255, static_cast<int>(120 + 135 * blink)));
    EditorLayout::DrawViewportText({ cx, cy + 195.0f }, "WASD: move   Mouse: aim / shoot   Shift: Dead Eye   Space: roll", 22.0f, IM_COL32(200, 200, 200, 255));
#endif // USE_IMGUI
}
