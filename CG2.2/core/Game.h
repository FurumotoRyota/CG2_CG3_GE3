#pragma once
#include "core/Framework.h"

/// <summary>
/// このゲーム固有の設定（BGM・最初のシーン・システム用ImGui）
/// </summary>
class Game : public Framework
{
protected:
    void Initialize() override;
    void Finalize() override;
    void Update() override;
    void DrawImGui() override;

private:
    void ToggleBgm(); // BGMの再生/一時停止を切り替える（最初の1回目は頭から再生する）

    Audio::SoundHandle bgmHandle_ = Audio::kInvalidHandle;
    bool bgmStarted_ = false; // 一度でも再生を始めたか（起動しただけでは鳴らさない）
};
