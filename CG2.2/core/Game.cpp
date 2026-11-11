#include "Game.h"
#include "../scene/StartupScene.h"
#include "../scene/TitleScene.h"
#include "../scene/WesternShooterScene.h"
#include "../scene/MapEditorScene.h"
#include "editor/EditorLayout.h"

#include <format>
#include <string>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

void Game::Initialize()
{
    Framework::Initialize();

    bgmHandle_ = audio_.LoadWave("resources/BGM.wav");
    // 読み込むだけで、再生はしない（Mキー / ゲームパッドBボタン / Systemパネルのボタンで鳴らす）

    // 最初のシーン
    sceneManager_.ChangeScene<StartupScene>();
}

void Game::Update()
{
    // BGMのON/OFF切り替え（Mキー or ゲームパッドBボタン）
    // ※SPACEはカメラの上移動に使うのでMキーにしてある
    // ※Bボタンの並びはコントローラーにより異なる。反応しない場合はImGuiの"GamePad"で番号を確認して差し替える
    if (input_.TriggerKey(DIK_M) || input_.TriggerGamepadButton(1)) {
        ToggleBgm();
    }
}

void Game::ToggleBgm()
{
    if (audio_.IsPlaying(bgmHandle_)) {
        audio_.Pause(bgmHandle_);
    }
    else if (!bgmStarted_) {
        audio_.Play(bgmHandle_, true); // 初めてのときは頭からループ再生
        bgmStarted_ = true;
    }
    else {
        audio_.Resume(bgmHandle_);
    }
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
            if (ImGui::MenuItem("タイトル")) {
                sceneManager_.ChangeScene<TitleScene>();
            }
            if (ImGui::MenuItem("西部シューティング")) {
                sceneManager_.ChangeScene<WesternShooterScene>();
            }
            if (ImGui::MenuItem("マップエディタ")) {
                sceneManager_.ChangeScene<MapEditorScene>();
            }
            ImGui::EndMenu();
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::EndMainMenuBar();
    }

    // 下部のSystemパネル
    EditorLayout::Begin("システム", EditorLayout::Panel::System);
    if (ImGui::BeginTabBar("SystemTabs")) {
        if (ImGui::BeginTabItem("サウンド")) {
            const bool bgmPlaying = audio_.IsPlaying(bgmHandle_);
            ImGui::Text("BGM: %s", bgmPlaying ? "再生中" : "停止中");
            ImGui::Text("切り替え: Mキー / ゲームパッドのBボタン");
            if (ImGui::Button(bgmPlaying ? "BGMを停止" : "BGMを再生")) {
                ToggleBgm();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("ゲームパッド")) {
            if (input_.HasGamepad()) {
                const DIJOYSTATE2& joyState = input_.GetJoyState();
                ImGui::Text("接続中");
                ImGui::Text("左スティック (lX, lY): %ld, %ld", joyState.lX, joyState.lY);
                ImGui::Text("右スティック (lRx, lRy): %ld, %ld", joyState.lRx, joyState.lRy);
                ImGui::Text("Z / Rz: %ld, %ld", joyState.lZ, joyState.lRz);
                ImGui::Text("十字キー (POV): %lu", joyState.rgdwPOV[0]);
                std::string buttons;
                for (int i = 0; i < 32; ++i) {
                    if (joyState.rgbButtons[i] & 0x80) {
                        buttons += std::format("[{}] ", i);
                    }
                }
                ImGui::Text("ボタン: %s", buttons.c_str());
            }
            else {
                ImGui::Text("未接続 (キーボードのみ)");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
#endif // USE_IMGUI
}
