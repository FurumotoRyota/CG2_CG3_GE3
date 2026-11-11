#pragma once
#include "scene/Scene.h"

/// <summary>
/// タイトル画面（ENTER / SPACE / ゲームパッドAボタン / マウス左クリックで西部シューティングへ）
/// </summary>
class TitleScene : public Scene
{
public:
    // スタートアップから来たときだけ true（桜色の幕から明るくなる演出に使う）
    static inline bool s_fromStartup = false;

    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;

private:
    float time_ = 0.0f; // 点滅用
    bool fromStartup_ = false;
};
