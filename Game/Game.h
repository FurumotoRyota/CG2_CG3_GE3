#pragma once
#include "core/Framework.h"

/// <summary>
/// このゲーム固有の設定（最初のシーン・メニュー）
/// 3人で作るゲームの出発点。まずはここから機能を足していく
/// </summary>
class Game : public Framework
{
protected:
    void Initialize() override;
    void Finalize() override;
    const wchar_t* WindowTitle() const override { return L"My Game"; } // ウィンドウの題名（ゲーム名が決まったら変える）
    void Update() override;
    void DrawImGui() override;
    void DrawOverlay() override; // シーンの上に重ねるもの（デバッグ描画）
};
