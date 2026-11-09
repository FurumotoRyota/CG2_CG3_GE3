#pragma once
// 【部品】砂ぼこり（小さな立方体がふくらんで舞い上がり、縮んで消える）
//   プール(GameObjectPool)で使い回す。最大 kMax 個まで（超えた分は出さない）。
//   使い方:
//     dust.Initialize(ctx_->object3dCommon, "単色の立方体モデル名");   // 最初に1回（モデルは先に登録しておく）
//     dust.Emit({x, y, z}, 8, 1.5f, 0.35f, 0.6f, 0.8f, 0.7f, 0.5f);   // 出す（位置, 数, 飛ぶ速さ, 大きさ, 寿命秒, 色rgb）
//     dust.Step(dt);                                                  // 毎フレーム：動かす
//     dust.Update(view, proj);  dust.Draw();                          // 描く前に更新→描画（Object3dCommon::PreDraw のあと）
#include <cstddef>
#include <vector>

#include "GameObject.h"
#include "GameObjectPool.h"
#include "math/Matrix4x4.h"
#include "math/Vector3.h"

class Object3dCommon;

class DustBurst
{
public:
    static constexpr size_t kMax = 128;

    void Initialize(Object3dCommon* common, const char* modelName);

    // count 個を position から放射状に出す（足りなければ出せるだけ）
    void Emit(const Vector3& position, int count, float speed, float size, float life, float r, float g, float b);

    void Step(float dt);
    void Update(const Matrix4x4& view, const Matrix4x4& proj);
    void Draw();

    void Clear();
    size_t ActiveCount() const { return particles_.size(); }

private:
    struct Particle
    {
        GameObject* obj = nullptr;
        Vector3 pos{ 0.0f, 0.0f, 0.0f };
        Vector3 vel{ 0.0f, 0.0f, 0.0f };
        float age = 0.0f;
        float life = 1.0f;
        float size = 0.3f;
        float spin = 0.0f;
    };

    GameObjectPool pool_;
    std::vector<Particle> particles_;
};
