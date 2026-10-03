#pragma once
#include "SceneContext.h"

/// <summary>
/// シーンの基底クラス
/// 流れ: Initialize → (毎フレーム) Update → DrawImGui → Draw → Finalize
/// Draw() は dxCommon->PreDraw() の後に呼ばれるので、描画コマンドだけを積むこと
/// </summary>
class Scene
{
public:
    virtual ~Scene() = default;

    virtual void Initialize() = 0;
    virtual void Update() = 0;
    virtual void Draw() = 0;
    virtual void DrawImGui() {}
    virtual void Finalize() {}

    // SceneManagerがInitializeの前に呼ぶ
    void SetContext(SceneContext* context) { ctx_ = context; }

protected:
    SceneContext* ctx_ = nullptr;
};
