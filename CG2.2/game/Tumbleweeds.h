#pragma once
// 【部品】タンブルウィード（風に転がる枯れ草。西部の雰囲気づくり。見た目だけで、当たり判定はない）
//   風上（西）の画面の外から現れて、風下（東）へ弾みながら転がっていく。壁・建物にぶつかると縮んで消える。
//   出す行は、風下へ12マス以上壁・建物が無い行だけ（すぐぶつかる行には出さない）。
//   一度に最大 kMax 個。出る間隔・速さ・高さはランダム。
//   使い方:
//     weeds.Initialize(ctx_->object3dCommon, "単色の立方体モデル名");   // 最初に1回（モデルは先に登録しておく）
//     weeds.Step(dt, map, focus);       // 毎フレーム：focus は画面の中心の地面の点 (x, z)
//     weeds.Update(view, proj);  weeds.Draw();
#include <cstddef>
#include <vector>

#include "GameObject.h"
#include "GameObjectPool.h"
#include "MapData.h"
#include "math/Matrix4x4.h"
#include "math/Vector2.h"

class Object3dCommon;

class Tumbleweeds
{
public:
    static constexpr size_t kMax = 6;

    void Initialize(Object3dCommon* common, const char* modelName);
    void Step(float dt, const MapData& map, const Vector2& focus);
    void Update(const Matrix4x4& view, const Matrix4x4& proj);
    void Draw();
    void Clear();

    size_t ActiveCount() const { return weeds_.size(); }

private:
    struct Weed
    {
        GameObject* core = nullptr;   // 立方体を2つ重ね、違う向きに回すと、とげとげした玉に見える
        GameObject* cross = nullptr;
        Vector2 pos{ 0.0f, 0.0f };    // 地面の位置 (x, z)
        float speed = 3.0f;
        float size = 0.7f;
        float spin = 0.0f;            // 転がりの角度
        float hopPhase = 0.0f;        // 弾みの位相
        float hopHeight = 0.4f;
        float wobblePhase = 0.0f;     // 進路のゆらぎ
        float age = 0.0f;             // 出てからの経過秒（出た直後は小さく現れる）
        float dying = -1.0f;          // 0以上なら、ぶつかって縮んでいる最中（経過秒）
    };

    void Apply(const Weed& w);
    void Remove(size_t index);

    GameObjectPool pool_;
    std::vector<Weed> weeds_;
    float spawnTimer_ = 2.0f;
};
