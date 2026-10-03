#pragma once
#include <string>

#include "3d/Object3d.h"
#include "math/Vector3.h"

class Object3dCommon;

/// <summary>
/// ゲーム内オブジェクトの基底クラス（Object3dを1つ持つ）
/// 動きを変えたいときは継承して Update() をオーバーライドする
/// 例: class Enemy : public GameObject { void Update() override { ... } };
/// </summary>
class GameObject
{
public:
    virtual ~GameObject() = default;

    virtual void Initialize(Object3dCommon* common, const std::string& modelName, const Vector3& translate);

    // 毎フレームの動き（Transformの更新など）。基底は何もしない
    virtual void Update() {}

    // 描画（Object3dCommon::PreDraw の後に呼ぶ）
    virtual void Draw() { object_.Draw(); }

    Object3d& GetObject3d() { return object_; }

protected:
    Object3d object_;
};
