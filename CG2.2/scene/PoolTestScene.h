#pragma once
// 【確認用】GameObjectPool が動くかを見るための最小のシーン
//   WASD / 矢印キー / 左スティック: 自機を動かす（向きは最後に動いた方向）
//   J / マウス左 / RB・RT: 弾を撃つ（弾はプールから借りて、消えたら返す）
//   画面左のパネルに「使用中の弾 / プールの数」が出る。連射して満杯になると、それ以上は出ない
#include <vector>

#include "scene/Scene.h"
#include "../game/GameObject.h"
#include "../game/GameObjectPool.h"
#include "camera/Camera.h"
#include "input/GameInput.h"
#include "math/Vector2.h"

class PoolTestScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;

private:
    // 地面はXZ平面。動きと判定は (x, z) の2次元で扱い、高さ(y)は固定にする
    struct Bullet
    {
        GameObject* obj = nullptr; // プールから借りているもの
        Vector2 pos{ 0.0f, 0.0f };
        Vector2 vel{ 0.0f, 0.0f };
        float life = 0.0f;
    };

    void Shoot();

    Camera camera_;
    GameInput input_;

    GameObject player_;
    Vector2 playerPos_{ 0.0f, 0.0f };
    Vector2 facing_{ 0.0f, 1.0f }; // 向いている方向（長さ1）

    GameObjectPool bulletPool_;
    std::vector<Bullet> bullets_;  // いま飛んでいる弾

    float fireTimer_ = 0.0f;
    int shotCount_ = 0;     // 撃った数
    int missedCount_ = 0;   // 満杯で出せなかった数
};
