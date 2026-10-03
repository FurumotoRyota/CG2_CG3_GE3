#pragma once
#include "scene/Scene.h"

/// <summary>
/// タイトル画面（無地。ENTERキー / ゲームパッドAボタンで GameScene へ）
/// 表示したいものがあれば、GameSceneのようにここへオブジェクト/スプライトを追加していく
/// </summary>
class TitleScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
};
