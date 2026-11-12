#include "Game.h"
#include "StartScene.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

void Game::Initialize()
{
    Framework::Initialize();

    // 最初のシーン
    sceneManager_.ChangeScene<StartScene>();
}

void Game::Update()
{
    // ゲーム全体で毎フレーム行う処理があればここに書く
}

void Game::DrawImGui()
{
#ifdef USE_IMGUI
    // メニューバー
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("ファイル")) {
            if (ImGui::MenuItem("終了")) {
                PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("シーン")) {
            if (ImGui::MenuItem("スタート")) {
                sceneManager_.ChangeScene<StartScene>();
            }
            // 新しいシーンを作ったら、ここにメニューを足すと切り替えられる
            ImGui::EndMenu();
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::EndMainMenuBar();
    }
#endif // USE_IMGUI
}
