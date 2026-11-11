#pragma once
// 【部品】MapData を3Dで見せる（地面・壁・樽・区切りの柵）
//   壁・樽は GameObjectPool の立方体で表す（最大 kMaxBlocks 個。毎回作らない）。横に続く壁は1本にまとめるので、壁の数ではなく「壁の塊の数」が数えられる。
//   小さな壁の塊（遮蔽物）は荷馬車・木箱の山に、大きな壁は岩山（高さ・色に変化、サボテン付き）に見せる。地面にはまだら・草・影を足す。
//   建物（酒場・保安官事務所・雑貨店）は、数個〜十数個の立方体を組み合わせて作る（最大 kMaxBuildingBlocks 個）。正面は画面の手前(-z)側。
//   使い方:
//     view.Initialize(ctx_->object3dCommon, ctx_->modelManager);   // 最初に1回
//     view.Build(map);                                              // マップが変わったとき
//     view.SetGateVisible(i, false);                                // 区切りが開いたら柵を消す
//     view.Update(viewMatrix, projMatrix);  view.Draw();            // 毎フレーム（Draw は Object3dCommon::PreDraw のあと）
#include <array>
#include <cstddef>
#include <vector>

#include "GameObject.h"
#include "GameObjectPool.h"
#include "MapData.h"
#include "math/Matrix4x4.h"

class Object3dCommon;
class ModelManager;

class MapRenderer
{
public:
    static constexpr size_t kMaxBlocks = 2048;
    static constexpr size_t kMaxBuildingBlocks = 1536;
    static constexpr size_t kMaxPropBlocks = 3072; // 飾り（荷馬車・木箱、地面のまだら、影、サボテン、草）
    static constexpr const char* kCubeName = "ws_cube"; // Initialize で登録する単色の立方体（色は Material の color で付ける）。他の見た目でも使える

    void Initialize(Object3dCommon* common, ModelManager* models);

    // マップの見た目を作り直す。壁・樽・建物の立方体が足りなければ、足りない分は描かれず false を返す
    bool Build(const MapData& map);

    // 区切り（gates の番号）の柵を見せる・隠す
    void SetGateVisible(size_t index, bool visible);

    // 画面に映る範囲（地面の x,z の四角）。この外にある壁・建物・飾りは、更新も描画もしない（軽くするため）。
    // 呼ばなければ全部を描く。カメラが動くたびに Update の前に呼ぶ
    void SetVisibleRect(float minX, float minZ, float maxX, float maxZ);
    // 直前の Update で描く対象になった立方体の数（確認用）
    size_t VisibleCount() const { return visibleCount_; }

    void Update(const Matrix4x4& view, const Matrix4x4& proj);
    void Draw();

    // 壁の高さ・樽の高さ（見た目）
    static constexpr float kWallHeight = 1.6f;
    static constexpr float kBarrelHeight = 0.8f;

private:
    // 立方体1つと、その場所・大きさ（画面の外かどうかを調べるため）
    struct Item
    {
        GameObject* obj = nullptr;
        float x = 0.0f;
        float z = 0.0f;
        float radius = 0.0f;
        bool visible = true;
    };
    void Track(std::vector<Item>& items, GameObject* obj, float x, float z, float sx, float sz);
    void UpdateItems(std::vector<Item>& items, const Matrix4x4& view, const Matrix4x4& proj);
    static void DrawItems(std::vector<Item>& items);

    // 3Dモデル（resources/ws_*.obj。Low Poly Desert & Western Pack を、色をパレットに焼いたもの）
    enum ModelId : int
    {
        kMSaloon, kMStore, kMJail, kMBarrel, kMCrate, kMTnt, kMWagon, kMCoveredWagon, kMSaguaro,
        kMBarrelCactus, kMPricklyPear, kMDryBush, kMSkull, kMDeadTree, kMHitchingPost, kMWantedBoard,
        kMWaterTower, kMWindmill, kMMineCart, kMFence, kMArch, kMMesa, kMTumbleweed, kMRibcage, kMCampfire, kMHat, kMGold, kMSaddle,
        kModelCount
    };
    // 飾りのモデルを1つ置く（当たり判定は無い。見た目だけ）。yaw はY軸まわりの回転（ラジアン）。プールが満杯なら false
    bool AddModel(ModelId id, float px, float py, float pz, float yaw, float sx, float sy, float sz);

    bool AddBuilding(const MapData::BuildingRect& b);
    // 飾りの立方体を1つ置く（当たり判定は無い。見た目だけ）
    bool AddProp(float px, float py, float pz, float sx, float sy, float sz, float r, float g, float b);
    // 小さな壁の塊（x0..x1, z0..z1 のマス）を、荷馬車か木箱の山に見せる
    bool AddCover(int x0, int z0, int x1, int z1);
    // 地面の影（太陽は左＝-x 側。+x 側に len だけ伸びる）
    bool AddShadow(float xa, float za, float xb, float zb, float len);

    GameObjectPool blockPool_;
    GameObjectPool buildingPool_;
    GameObjectPool propPool_;
    std::array<GameObjectPool, kModelCount> modelPools_;
    GameObject floor_;
    GameObject gates_[MapData::kMaxGates];
    bool gateVisible_[MapData::kMaxGates] = {};
    size_t gateCount_ = 0;

    std::vector<Item> blockItems_;
    std::vector<Item> buildingItems_;
    std::vector<Item> propItems_;
    float visMinX_ = -1.0e9f, visMinZ_ = -1.0e9f, visMaxX_ = 1.0e9f, visMaxZ_ = 1.0e9f;
    size_t visibleCount_ = 0;
};
