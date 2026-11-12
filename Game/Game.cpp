#include "Game.h"
#include "StartScene.h"
#include "startup/StartupScene.h"
#include "scene/SceneContext.h"
#include "scene/SceneManager.h"
#include "core/FrameStats.h"
#include "debug/DebugDraw.h"
#include "debug/LogPanel.h"

#ifdef USE_IMGUI
#include "editor/EditorLayout.h"
#include "imgui/imgui.h"
#endif // USE_IMGUI

void Game::Initialize()
{
    Framework::Initialize();

    // デバッグ描画（F1キーで表示のON/OFF）。使い方は debug/DebugDraw.h の先頭のコメント
    DebugDraw::Instance().Initialize(&spriteCommon_);

    // スタートアップ演出が終わったら、StartScene へ進む（このエンジンの起動演出。ゲームが変わっても必ず流す）
    StartupScene::s_hooks.onFinish = [](SceneContext* ctx) {
        ctx->sceneManager->ChangeScene<StartScene>();
    };

    // 最初のシーン
    sceneManager_.ChangeScene<StartupScene>();
}

void Game::Finalize()
{
    DebugDraw::Instance().Finalize(); // GPUより先に解放する
    Framework::Finalize();
}

void Game::Update()
{
    // F1キーで、デバッグ描画（当たり判定の枠など）の表示を切り替える
    if (input_.TriggerKey(DIK_F1)) {
        DebugDraw::Instance().Toggle();
    }
}

void Game::DrawOverlay()
{
    // シーンの上に、デバッグ描画の線を重ねる（OFF のときは何も描かれない）
    spriteCommon_.PreDraw();
    DebugDraw::Instance().Flush();
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

    // 下部のシステムパネル（ログ・デバッグ描画・描画の統計）
    EditorLayout::Begin("システム", EditorLayout::Panel::System);
    if (ImGui::BeginTabBar("SystemTabs")) {
        LogPanel::DrawTab();

        if (ImGui::BeginTabItem("デバッグ描画")) {
            DebugDraw& debug = DebugDraw::Instance();
            bool enabled = debug.IsEnabled();
            if (ImGui::Checkbox("デバッグ描画を表示（F1キー）", &enabled)) {
                debug.SetEnabled(enabled);
            }
            ImGui::Text("このフレームの線の数: %u / %u", debug.LineCount(), debug.MaxLines());
            ImGui::TextWrapped("当たり判定の枠などは、コードから DebugDraw::Instance().Box3(...) などで描けます（使い方は debug/DebugDraw.h の先頭）。");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("描画の統計")) {
            const FrameStats::Data& stats = FrameStats::Last();
            ImGui::Text("1フレーム: %.2f ms (FPS %.1f)", stats.frameMs, ImGui::GetIO().Framerate);
            ImGui::Text("Update: %.2f ms   Draw(CPU): %.2f ms   Present+GPU待ち: %.2f ms", stats.updateMs, stats.drawMs, stats.presentMs);
            ImGui::Text("スプライト: %u 回 / モデル: %u 回 / パーティクル: %u 回",
                stats.calls[FrameStats::kSprite], stats.calls[FrameStats::kModel], stats.calls[FrameStats::kParticle]);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
#endif // USE_IMGUI
}
