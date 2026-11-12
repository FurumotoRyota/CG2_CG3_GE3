#pragma once
#include "GameObject.h"

/// <summary>
/// Y軸でくるくる回るオブジェクト（GameObjectを継承した例）
/// </summary>
class SpinningObject : public GameObject
{
public:
    void Update() override { object_.GetTransform().Rotate.y += kSpinSpeed; }

private:
    static constexpr float kSpinSpeed = 0.03f; // 1フレームあたりの回転量(ラジアン)
};
