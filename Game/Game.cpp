#include "Game.h"
#include "StartScene.h"
#include "startup/StartupScene.h"
#include "scene/SceneContext.h"
#include "scene/SceneManager.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

void Game::Initialize()
{
    Framework::Initialize();

    // スタートアップ演出が終わったら、StartScene へ進む（このエンジンの起動演出。ゲームが変わっても必ず流す）
    StartupScene::s_hooks.onFinish = [](SceneContext* ctx) {
        ctx->sceneManager->ChangeScene<StartScene>();
    };

    // 最初のシーン
    sceneManager_.ChangeScene<StartupScene>();
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
            if (ImGui::MenuItem("スタートアップ演出")) {
                sceneManager_.ChangeScene<StartupScene>();
            }
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
