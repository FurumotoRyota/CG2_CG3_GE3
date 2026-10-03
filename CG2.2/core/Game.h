#pragma once
#include "core/Framework.h"

/// <summary>
/// このゲーム固有の設定（BGM・最初のシーン・システム用ImGui）
/// </summary>
class Game : public Framework
{
protected:
    void Initialize() override;
    void Update() override;
    void DrawImGui() override;

private:
    Audio::SoundHandle bgmHandle_ = Audio::kInvalidHandle;
};
