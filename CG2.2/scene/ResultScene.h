#pragma once
#include "scene/Scene.h"

/// <summary>
/// 結果画面（ゲームオーバー / クリア）。ENTER でタイトル、R でもう一度
/// </summary>
class ResultScene : public Scene
{
public:
    ResultScene(bool cleared, int kills) : cleared_(cleared), kills_(kills) {}

    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;

private:
    bool cleared_ = false;
    int kills_ = 0;
    float time_ = 0.0f;
};
