#pragma once
// 【部品】MapData を3Dで見せる（地面・壁・樽・区切りの柵）
//   壁・樽は GameObjectPool の立方体で表す（最大 kMaxBlocks 個。毎回作らない）。横に続く壁は1本にまとめるので、壁の数ではなく「壁の塊の数」が数えられる。
//   建物（酒場・保安官事務所・雑貨店）は、数個〜十数個の立方体を組み合わせて作る（最大 kMaxBuildingBlocks 個）。正面は画面の手前(-z)側。
//   使い方:
//     view.Initialize(ctx_->object3dCommon, ctx_->modelManager);   // 最初に1回
//     view.Build(map);                                              // マップが変わったとき
//     view.SetGateVisible(i, false);                                // 区切りが開いたら柵を消す
//     view.Update(viewMatrix, projMatrix);  view.Draw();            // 毎フレーム（Draw は Object3dCommon::PreDraw のあと）
#include <cstddef>

#include "GameObject.h"
#include "GameObjectPool.h"
#include "MapData.h"
#include "math/Matrix4x4.h"

class Object3dCommon;
class ModelManager;

class MapRenderer
{
public:
    static constexpr size_t kMaxBlocks = 1024;
    static constexpr size_t kMaxBuildingBlocks = 768;
    static constexpr const char* kCubeName = "ws_cube"; // Initialize で登録する単色の立方体（色は Material の color で付ける）。他の見た目でも使える

    void Initialize(Object3dCommon* common, ModelManager* models);

    // マップの見た目を作り直す。壁・樽・建物の立方体が足りなければ、足りない分は描かれず false を返す
    bool Build(const MapData& map);

    // 区切り（gates の番号）の柵を見せる・隠す
    void SetGateVisible(size_t index, bool visible);

    void Update(const Matrix4x4& view, const Matrix4x4& proj);
    void Draw();

    // 壁の高さ・樽の高さ（見た目）
    static constexpr float kWallHeight = 1.6f;
    static constexpr float kBarrelHeight = 0.8f;

private:
    bool AddBuilding(const MapData::BuildingRect& b);

    GameObjectPool blockPool_;
    GameObjectPool buildingPool_;
    GameObject floor_;
    GameObject gates_[MapData::kMaxGates];
    bool gateVisible_[MapData::kMaxGates] = {};
    size_t gateCount_ = 0;
};
