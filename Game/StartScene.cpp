#include "StartScene.h"
#include "editor/EditorLayout.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

void StartScene::Initialize()
{}

void StartScene::Update()
{}

void StartScene::Draw()
{
    // 何も描かない（背景色のみ）
}

void StartScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("Hierarchy", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: スタート(新しいゲーム)");
    ImGui::End();
#endif // USE_IMGUI
}