#pragma once
#include "Scene.h"
#include "../2d/Sprite.h"

/// <summary>
/// タイトル画面（ENTERキー / ゲームパッドAボタンで GameScene へ）
/// </summary>
class TitleScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;

private:
    Sprite sprite_;
};
