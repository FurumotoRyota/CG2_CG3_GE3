#pragma once

class WinApp;
class DirectXCommon;

/// <summary>
/// ImGuiの初期化・フレーム開始/終了・描画・終了処理をまとめる
/// USE_IMGUI が未定義の場合は全て空処理になる
/// 使い方: 毎フレーム Begin() → ImGui::〜(UI記述) → End() → (描画中) Draw()
/// </summary>
class ImGuiManager
{
public:
    void Initialize(WinApp* winApp, DirectXCommon* dxCommon);
    void Finalize();

    void Begin(); // NewFrame
    void End();   // Render（UI記述の最後に呼ぶ）
    void Draw();  // コマンドリストへ積む（PostDrawの直前）

private:
    DirectXCommon* dxCommon_ = nullptr;
};
