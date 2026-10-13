#include "TitleScene.h"
#include "GameScene.h"
#include "scene/SceneManager.h"
#include "input/Input.h"
#include "editor/EditorLayout.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

void TitleScene::Initialize()
{
}

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
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: タイトル");
    ImGui::Separator();
    ImGui::Text("ENTERキー / ゲームパッドのAボタンを押す");
    ImGui::Text("ゲーム開始");
    ImGui::End();
#endif // USE_IMGUI
}
