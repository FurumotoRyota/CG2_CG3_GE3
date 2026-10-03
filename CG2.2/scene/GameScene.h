#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Scene.h"
#include "../2d/Sprite.h"
#include "../3d/InstancedPlanes.h"
#include "../game/GameObject.h"
#include "../camera/Camera.h"
#include "../camera/DebugCamera.h"
#include "../input/KonamiCommand.h"
#include "../math/Transform.h"
#include "../math/Vector2.h"
#include "../particle/ParticleEmitter.h"
#include "../math/Vector4.h"

struct DirectionalLight;

/// <summary>
/// ゲーム画面（3Dオブジェクト・スプライト・コナミコマンドのパーティーモード）
/// Hierarchyの右クリック / [+ Add] でオブジェクトやスプライトを追加・複製・削除できる
/// BACKSPACEキー / ゲームパッドSTART(ボタン9)でタイトルへ戻る
/// </summary>
class GameScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;
    void Finalize() override;

private:
    // 3Dオブジェクト1体ぶんの状態（描画はGameObject/Object3dが担当）
    struct ObjectSlot
    {
        std::unique_ptr<GameObject> object;
        int objIndex = 0;       // ImGuiのComboで選ばれている objFileList_ 内のインデックス
        int loadedObjIndex = 0; // 実際にモデルを読み込み済みのインデックス
        Vector4 konamiOriginalColor{ 1.0f, 1.0f, 1.0f, 1.0f }; // パーティーモード開始前の色
    };

    // スプライト1枚ぶんの状態
    struct SpriteSlot
    {
        std::unique_ptr<Sprite> sprite;
        int texIndex = 0;       // textureFileList_ 内のインデックス
        int loadedTexIndex = 0;
        Vector2 size{ 640.0f, 360.0f }; // 作成時の縦横サイズ（複製に使う）
    };

    // 板ポリ大量配置1グループぶんの状態（同じテクスチャの板を count 枚、DrawInstanced 1回で描く）
    struct PlanesSlot
    {
        std::unique_ptr<InstancedPlanes> planes;
        int texIndex = 0;       // textureFileList_ 内のインデックス
        int loadedTexIndex = 0;
    };

    // Hierarchyでの選択
    enum class SelectKind { None, Object, Sprite, Planes, Emitter, Light };
    struct Selection
    {
        SelectKind kind = SelectKind::None;
        int index = 0;
    };

    void ScanResources();
    int AddObject(int objIndex, const Vector3& translate);
    int AddSprite(int texIndex, const Vector2& size);
    void DuplicateObject(int index);
    void DuplicateSprite(int index);
    int AddPlanes(int texIndex, const Vector3& position);
    void DuplicatePlanes(int index);
    int AddEmitter(ParticlePreset preset, const Vector3& position);
    void DuplicateEmitter(int index);
    void RunPendingCommands();
    void DeleteSelection(); // 選択中のオブジェクト/スプライト/エミッターを削除する
    void StartPlay(); // 実行開始：今のシーンを保存してから動かす
    void StopPlay();  // 実行停止：保存しておいた状態に戻す
    void UpdateKonami();
    void UpdateObjects();
    void UpdateViewportInteraction(); // Viewport上のクリック選択・ドラッグ移動
    void UpdateCameraControl();       // 右クリックホールド中だけカメラを操作できるようにする
    void DrawSelectionOutline();      // 選択中の項目の枠と、拡大縮小ハンドルをViewportに重ねる
    // 選択中の項目のハンドル位置（ゲーム画面ピクセル座標）。0～3:四隅(左上・右上・右下・左下) 4～7:辺の中点(上・右・下・左)
    bool GetSelectionHandles(Vector2 outHandles[8]) const;

    std::unique_ptr<Camera> camera_;
    DebugCamera* debugCamera_ = nullptr; // camera_の中身（操作の有効/無効の切り替え用。所有はcamera_）
    bool cameraActive_ = false;           // 右クリックホールドでカメラ操作中
    std::vector<ObjectSlot> objects_;
    std::vector<SpriteSlot> sprites_;
    std::vector<PlanesSlot> planes_;        // 板ポリ大量配置
    std::vector<ParticleEmitter> emitters_; // シーンに置いたパーティクルエミッター

    // 実行(Play)モード。false の間は編集モードで、シーンは止まっている
    bool playing_ = false;
    struct Snapshot
    {
        struct Item
        {
            Transform transform;
            Transform uvTransform;
            Vector4 color;
        };
        std::vector<Item> objects;
        std::vector<Item> sprites;
        std::vector<InstancedPlanes::Settings> planes;
        std::vector<ParticleEmitter> emitters;
    };
    Snapshot snapshot_; // Play開始時のシーン（Stopで戻す）

    std::vector<std::string> objFileList_;
    std::vector<const char*> objFileListCStr_;
    std::vector<std::string> textureFileList_;  // "resources/xxx.png"
    std::vector<const char*> textureFileListCStr_;

    // 追加・削除・複製は描画中のリソースを壊さないよう、次フレームの頭でまとめて実行する
    std::vector<std::function<void()>> pendingCommands_;

    KonamiCommand konamiCommand_;
    bool konamiPartyMode_ = false; // コナミコマンドで切り替わるアニメーション演出モード
    float konamiTime_ = 0.0f;      // パーティーモード中の演出用タイマー

    Selection selection_;

    // Viewportでのドラッグ状態
    enum class DragMode { None, Move, ScaleSprite, ScaleObject, PanCamera };
    DragMode dragMode_ = DragMode::None;

    // 移動（Move）
    Vector2 dragSpriteOffset_{ 0.0f, 0.0f };       // スプライト：つかんだ位置とTranslateの差
    Vector3 dragPlanePoint_{ 0.0f, 0.0f, 0.0f };   // 3D：ドラッグ平面が通る点
    Vector3 dragPlaneNormal_{ 0.0f, 0.0f, 1.0f };  // 3D：ドラッグ平面の法線
    Vector3 dragObjectOffset_{ 0.0f, 0.0f, 0.0f }; // 3D：つかんだ位置とTranslateの差

    // 拡大縮小（ScaleSprite / ScaleObject）
    int scaleHandle_ = 0;                          // つかんだハンドル（0～3:四隅 4～7:辺の中点 上・右・下・左）
    int scaleAxis_ = -1;                           // 3D：拡大縮小する軸（0:X 1:Y 2:Z、-1なら全軸）
    Vector3 scaleStart_{ 1.0f, 1.0f, 1.0f };       // ドラッグ開始時のScale
    Vector2 scaleAnchor_{ 0.0f, 0.0f };            // スプライト：動かさない反対側の隅（画面座標）
    Vector2 scaleCenter_{ 0.0f, 0.0f };            // 3D：オブジェクト中心の画面座標
    float scaleStartDistance_ = 1.0f;              // 3D：開始時の中心からマウスまでの距離

    DirectionalLight* directionalLight_ = nullptr; // ImGuiで編集する用
};
