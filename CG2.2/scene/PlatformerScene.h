#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "scene/Scene.h"
#include "camera/Camera.h"
#include "3d/Object3d.h"
#include "2d/ColorBatch.h"
#include "base/TextureManager.h"
#include "../game2d/PlatformerGame.h"

struct DirectionalLight;

/// <summary>
/// 2D軸の横スクロールアクション（メニュー：ゲームモード > 2Dアクション(3D描画)）
/// 判定・移動は x, y だけの2D（game2d/PlatformerGame）。描画は3Dオブジェクトで行い、
/// 地形は立方体を並べたメッシュ、キャラクターはobjモデル、光源・影・スカイボックスもそのまま使う。
///  操作: A/D 移動 / Space ジャンプ（敵は上から踏んで倒す） / R リスタート
/// </summary>
class PlatformerScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
    void Finalize() override;

private:
    struct EnemyView
    {
        std::unique_ptr<Object3d> body;
        std::unique_ptr<Object3d> shield; // 盾の敵だけ
    };

    std::unique_ptr<Object3d> MakeObject(const std::string& modelName);
    void SetFlatColor(Object3d& object, float r, float g, float b);
    // ワールド(x, y)に、モデルの外接ボックスの最大辺が size になるよう拡縮して置く（yaw は Y 軸回りの回転）
    void PlaceFit(Object3d& object, pf::Vec2 pos, float z, float size, float yaw, float extraScale = 1.0f);
    void PlaceBox(Object3d& object, pf::Vec2 pos, float z, float sx, float sy, float sz, float yaw);

    void RebuildTerrain();   // 地形メッシュ（見える面だけ）を作り直す
    void SyncObjects();      // ゲームの状態を3Dオブジェクトへ反映する
    void UpdateCamera();
    void RenderShadowPass();
    void DrawHud();
    void ResetGame();

    pf::Game game_;
    Camera camera_;
    ColorBatch hud_;
    DirectionalLight* light_ = nullptr;
    TextureManager::TextureHandle paletteHandle_ = 0;

    std::unique_ptr<Object3d> terrainTop_;
    std::unique_ptr<Object3d> terrainSide_;
    std::unique_ptr<Object3d> player_;
    std::unique_ptr<Object3d> goalPole_;
    std::unique_ptr<Object3d> goalFlag_;
    std::unordered_map<int, EnemyView> enemyViews_;
    std::vector<std::unique_ptr<Object3d>> particleCubes_;
    std::vector<Object3d*> drawList_; // このフレームに描くもの（描画・影で共通）

    int builtMapVersion_ = -1;
    int builtResetCount_ = -1;

    // 調整値
    float ppu_ = 48.0f;          // 1マスのピクセル数（大きいほど寄る）
    float yawDeg_ = 12.0f;       // カメラを横にずらす角度（立体感）
    float pitchDeg_ = 10.0f;     // カメラを上から見下ろす角度
    float modelYawOffsetDeg_ = 0.0f; // キャラの向きの補正
    bool shadowEnabled_ = true;
    std::string message_;
};
