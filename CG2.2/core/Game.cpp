#include "Game.h"
#include "../scene/TitleScene.h"
#include "../scene/GameScene.h"
#include "../editor/EditorLayout.h"

#include <format>
#include <string>

#ifdef USE_IMGUI
#include "../externals/imgui/imgui.h"
#endif // USE_IMGUI

void Game::Initialize()
{
    Framework::Initialize();

    bgmHandle_ = audio_.LoadWave("resources/BGM.wav");
    audio_.Play(bgmHandle_, true); // 起動時からBGMをループ再生

    // 最初のシーン
    sceneManager_.ChangeScene<TitleScene>();
}

void Game::Update()
{
    // BGMのON/OFF切り替え（Mキー or ゲームパッドBボタン）
    // ※SPACEはカメラの上移動に使うのでMキーにしてある
    // ※Bボタンの並びはコントローラーにより異なる。反応しない場合はImGuiの"GamePad"で番号を確認して差し替える
    if (input_.TriggerKey(DIK_M) || input_.TriggerGamepadButton(1)) {
        if (audio_.IsPlaying(bgmHandle_)) {
            audio_.Pause(bgmHandle_);
        }
        else {
            audio_.Resume(bgmHandle_);
        }
    }
}

void Game::DrawImGui()
{
#ifdef USE_IMGUI
    // メニューバー
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Title")) {
                sceneManager_.ChangeScene<TitleScene>();
            }
            if (ImGui::MenuItem("Game")) {
                sceneManager_.ChangeScene<GameScene>();
            }
            ImGui::EndMenu();
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - 110.0f);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::EndMainMenuBar();
    }

    // 下部のSystemパネル
    EditorLayout::Begin("System", EditorLayout::Panel::System);
    if (ImGui::BeginTabBar("SystemTabs")) {
        if (ImGui::BeginTabItem("Sound")) {
            const bool bgmPlaying = audio_.IsPlaying(bgmHandle_);
            ImGui::Text("BGM: %s", bgmPlaying ? "Playing" : "Stopped");
            ImGui::Text("Toggle: M key / GamePad B button");
            if (ImGui::Button(bgmPlaying ? "Stop BGM" : "Play BGM")) {
                if (bgmPlaying) {
                    audio_.Pause(bgmHandle_);
                }
                else {
                    audio_.Resume(bgmHandle_);
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("GamePad")) {
            if (input_.HasGamepad()) {
                const DIJOYSTATE2& joyState = input_.GetJoyState();
                ImGui::Text("Connected");
                ImGui::Text("Left Stick  (lX, lY)  : %ld, %ld", joyState.lX, joyState.lY);
                ImGui::Text("Right Stick (lRx, lRy): %ld, %ld", joyState.lRx, joyState.lRy);
                ImGui::Text("Z / Rz                : %ld, %ld", joyState.lZ, joyState.lRz);
                ImGui::Text("POV(D-Pad)            : %lu", joyState.rgdwPOV[0]);
                std::string buttons;
                for (int i = 0; i < 32; ++i) {
                    if (joyState.rgbButtons[i] & 0x80) {
                        buttons += std::format("[{}] ", i);
                    }
                }
                ImGui::Text("Buttons: %s", buttons.c_str());
            }
            else {
                ImGui::Text("Not connected (keyboard only)");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
#endif // USE_IMGUI
}
