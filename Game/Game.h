#pragma once
#include "core/Framework.h"

/// <summary>
/// 新しいゲームの設定（最初のシーン・システム用ImGui）
/// </summary>
class Game : public Framework
{
protected:
    void Initialize() override;
    void DrawImGui() override;
};