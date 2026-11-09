#pragma once
// 【部品】酒場のスイングドア（両開きの扉。蝶番を中心にバタンと揺れて、ばねのように揺れ戻る）
//   マップの出現点(MapData::spawns)の位置に1組ずつ置く。出現点が建物の正面の前にあれば、その建物の向きに合わせて、
//   建物の外側へ開く（どの建物の前でもなければ、画面の手前(-z)向き）。
//   使い方:
//     doors.Initialize(ctx_->object3dCommon, "単色の立方体モデル名");   // 最初に1回（モデルは先に登録しておく）
//     doors.Build(map);                                                 // マップが変わったとき
//     doors.Open(i);                                                    // i 番目の出現点から誰か出た（扉を揺らす）
//     doors.Step(dt);                                                   // 毎フレーム：揺らす
//     doors.Update(view, proj);  doors.Draw();                          // 描く前に更新→描画
#include <cstddef>
#include <vector>

#include "GameObject.h"
#include "GameObjectPool.h"
#include "MapData.h"
#include "math/Matrix4x4.h"

class Object3dCommon;

class SaloonDoors
{
public:
    static constexpr size_t kMaxDoors = MapData::kMaxSpawns;

    void Initialize(Object3dCommon* common, const char* modelName);
    void Build(const MapData& map);
    void Open(size_t index);
    void Step(float dt);
    void Update(const Matrix4x4& view, const Matrix4x4& proj);
    void Draw();

    // 揺れを止めて閉じた状態に戻す
    void Reset();

private:
    struct Door
    {
        float cx = 0.0f;       // 扉の真ん中（ワールド）
        float cz = 0.0f;
        float lx = 1.0f;       // 扉の幅方向の向き（長さ1）
        float lz = 0.0f;
        float ox = 0.0f;       // 開く向き＝建物の外側（長さ1）
        float oz = -1.0f;
        float angle = 0.0f;    // 開いた角度（ラジアン）。+ が外側へ開く向き
        float velocity = 0.0f; // 角速度
        GameObject* left = nullptr;
        GameObject* right = nullptr;
    };

    void Apply(const Door& d);

    GameObjectPool leafPool_;
    std::vector<Door> doors_;
};
