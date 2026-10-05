#pragma once
#include "scene/Scene.h"

/// <summary>
/// 新ゲームの最初のシーン（無地）。ここにオブジェクトやスプライトを足していく
/// </summary>
class StartScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
};