#include "TitleScene.h"
#include "GameScene.h"
#include "SceneManager.h"
#include "../input/Input.h"
#include "../editor/EditorLayout.h"

#ifdef USE_IMGUI
#include "../externals/imgui/imgui.h"
#endif // USE_IMGUI

void TitleScene::Initialize()
{}

void TitleScene::Update()
{
    if (ctx_->input->TriggerKey(DIK_RETURN) || ctx_->input->TriggerGamepadButton(0)) {
        ctx_->sceneManager->ChangeScene<GameScene>();
    }
}

void TitleScene::Draw()
{
    // 何も描かない（背景色のみ）
}

void TitleScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("Hierarchy", EditorLayout::Panel::Hierarchy);
    ImGui::Text("Scene: Title");
    ImGui::Separator();
    ImGui::Text("Press ENTER / GamePad A");
    ImGui::Text("to start the game");
    ImGui::End();
#endif // USE_IMGUI
}
