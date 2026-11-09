#pragma once
// 【見本B】ゲームごとにシーンクラスを丸ごと書く形
//   エディタ(GameScene)は使わない。いまのエンジンの部品（Scene / GameObject / GameInput / Collision / CameraEffects）だけで動く。
//   操作: WASD=移動  J=撃つ（向いている方向へ）  敵は円周上から湧いて追ってくる
#include <memory>
#include <vector>

#include "scene/Scene.h"
#include "../game/GameObject.h"
#include "camera/Camera.h"
#include "camera/CameraEffects.h"
#include "input/GameInput.h"
#include "math/Vector2.h"

class ShooterExampleScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;

private:
    // 地面はXZ平面。判定と移動は (x, z) の2次元で行い、見た目の高さ(y)は固定にする
    struct Bullet
    {
        std::unique_ptr<GameObject> obj;
        Vector2 pos{ 0.0f, 0.0f };
        Vector2 vel{ 0.0f, 0.0f };
        float life = 0.0f;
    };
    struct Enemy
    {
        std::unique_ptr<GameObject> obj;
        Vector2 pos{ 0.0f, 0.0f };
    };

    void Shoot();
    void SpawnEnemy();
    static void Place(GameObject& obj, const Vector2& pos, float y);

    Camera camera_;
    CameraEffects fx_;      // 画面の揺れ・ヒットストップ
    GameInput input_;       // 入力の窓口

    std::unique_ptr<GameObject> player_;
    Vector2 playerPos_{ 0.0f, 0.0f };
    Vector2 facing_{ 0.0f, 1.0f }; // 向いている方向（長さ1）
    std::vector<Bullet> bullets_;
    std::vector<Enemy> enemies_;

    float fireTimer_ = 0.0f;
    float spawnTimer_ = 0.0f;
    int score_ = 0;
};
