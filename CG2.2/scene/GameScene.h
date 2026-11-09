#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "scene/Scene.h"
#include "2d/Sprite.h"
#include "3d/InstancedPlanes.h"
#include "../game/GameObject.h"
#include "camera/Camera.h"
#include "camera/DebugCamera.h"
#include "camera/CameraEffects.h"
#include "input/GameInput.h"
#include "input/KonamiCommand.h"
#include "math/Transform.h"
#include "math/Vector2.h"
#include "particle/ParticleEmitter.h"
#include "video/VideoPlayer.h"
#include "math/Vector4.h"

struct DirectionalLight;

/// <summary>
/// ゲーム画面（3Dオブジェクト・スプライト・パーティクル・ライト・動画・マップ編集）
/// Hierarchyの右クリック / [+ Add] でオブジェクトやスプライトを追加・複製・削除できる
/// ゲームパッドSTART(ボタン9)でタイトルへ戻る
/// ゲームのルール（モード・部品・当たり判定の自動処理など）は持たない。ゲームの動きはゲームのコードで書く
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

    // マップのチップ（ブロック1マスぶん）の設定
    struct BlockProps
    {
        int chip = 0;                       // チップの種類（kChips の番号）
        float height = 1.5f;                // 高さ（3Dマップ）
        bool ownColor = false;              // このマスだけ色を指定するか（false なら、全体のブロックの色）
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // 3Dオブジェクト1体ぶんの状態（描画はGameObject/Object3dが担当）
    struct ObjectSlot
    {
        std::unique_ptr<GameObject> object;
        std::unique_ptr<VideoPlayer> video; // set when this object is a 3D video screen
        std::string videoName;
        int objIndex = 0;       // ImGuiのComboで選ばれている objFileList_ 内のインデックス
        int loadedObjIndex = 0; // 実際にモデルを読み込み済みのインデックス
        Vector4 konamiOriginalColor{ 1.0f, 1.0f, 1.0f, 1.0f }; // パーティーモード開始前の色
        int role = 0;       // ゲーム用の役割 0:なし 1:プレイヤー 2:ブロック 3:敵 4:ゴール（2D軸アクションで使う）
        bool shield = false; // 敵の役割のとき、盾の敵（2回踏まないと倒せない）
        int texOverride = 0;       // テクスチャの上書き 0:モデルのまま i+1:textureFileList_[i]
        int loadedTexOverride = 0; // 実際に反映済みの値
        bool refit = false;        // モデルを差し替えたあと、大きさをマスに合わせ直す
        char name[32] = "";        // このオブジェクトの名前（ゲームのコードから探すときに使う）
        int chip = 0;                      // ブロックのとき：チップの種類
        bool ownColor = false;             // ブロックのとき：このマスだけ色を指定している
        Vector4 ownTint{ 1.0f, 1.0f, 1.0f, 1.0f }; // その色
    };

    // スプライト1枚ぶんの状態
    struct SpriteSlot
    {
        std::unique_ptr<Sprite> sprite;
        std::unique_ptr<VideoPlayer> video; // set when this sprite plays a video
        std::string videoName;
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

    // 配置したライト1つぶん（ポイント / スポット）
    struct LightSlot
    {
        int type = 0; // 0:ポイント 1:スポット
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vector3 position{ 0.0f, 3.0f, 0.0f };
        Vector3 direction{ 0.0f, -1.0f, 0.0f }; // スポットの向き
        float intensity = 1.0f;
        float range = 10.0f;
        float decay = 1.0f;
        float outerDeg = 60.0f;
        float innerDeg = 30.0f;
    };

    // Hierarchyでの選択
    enum class SelectKind { None, Object, Sprite, Planes, Emitter, Light, SceneLight };
    struct Selection
    {
        SelectKind kind = SelectKind::None;
        int index = 0;
    };

    void ScanResources();
    void ImportDroppedFiles(); // files dropped on the window: copy to resources/ and add to the scene
    int AddObject(int objIndex, const Vector3& translate);
    bool SaveScene(const char* path);   // 置いたオブジェクトをテキストで保存する
    bool LoadScene(const char* path);   // 保存したシーンを読み込む（今のオブジェクトは消える）
    Camera& ActiveCamera() { return mapEdit_ ? gameCamera_ : *camera_; }
    const Camera& ActiveCamera() const { return mapEdit_ ? gameCamera_ : *camera_; }
    // ---- マップ編集（AL4_2D風：ビューポートをクリック・ドラッグして、マスにブロックや敵を置く）----
    void UpdateMapEdit();          // 編集カメラ・マウス → マス・配置/消去・アンドゥ
    void DrawMapEditOverlay();     // グリッド・マスの枠をビューポートに重ねる
    int FindBlockAt(int x, int y); // そのマスのブロック役オブジェクト（なければ -1）
    bool SetBlockCell(int x, int y, bool solid, const BlockProps* props = nullptr); // ブロックを置く/消す（props があれば、その設定で置く／既にあれば作り替える。変化したら true）
    BlockProps GetBlockProps(int objectIndex); // ブロックの今の設定
    void EraseRoleObjectsNear(float x, float y, float radius, bool includeGoal); // 敵・ゴールを消す
    int AddRoleObject(int role, const std::string& modelName, const Vector3& cellCenter, float fitSize, const Vector4& color, bool shield);
    void UndoMapEdit();
    Vector3 ModelCenterOffset(int objectIndex); // オブジェクトの原点から見た、モデルの中心のずれ（回転は無視）
    bool ExportStageCsv(const char* path);
    void ApplyFlatUv(Object3d& object);          // テクスチャの白い1点だけを使う（単色）
    void ApplyBlockStyle(int objectIndex);       // ブロックの見た目（テクスチャ・色）を反映する
    void ApplyBlockStyleAll();
    void FitObjectToSize(int objectIndex, float size); // モデルの中心を動かさず、最大辺が size になるよう拡縮する
    bool ImportStageCsv(const char* path);
    int AddSprite(int texIndex, const Vector2& size);
    int AddVideoObject(const std::string& utf8Path); // video on a 3D plane (-1 on failure)
    int AddVideoSprite(const std::string& utf8Path); // returns -1 if the video cannot be opened
    void DuplicateObject(int index);
    void DuplicateSprite(int index);
    int AddPlanes(int texIndex, const Vector3& position);
    void DuplicatePlanes(int index);
    int AddEmitter(ParticlePreset preset, const Vector3& position);
    void DuplicateEmitter(int index);
    int AddLight(int type, const Vector3& position);
    void DuplicateLight(int index);
    void UpdateLights(); // 配置したライトを定数バッファへ書き込む
    void UpdateShadow();     // 影用のライトのビュー射影行列を更新する
    void RenderShadowPass(); // 影の深度テクスチャを描く（本描画の前に記録する）
    void RunPendingCommands();
    void DeleteSelection(); // 選択中のオブジェクト/スプライト/エミッターを削除する
    void StartPlay(); // 実行開始：今のシーンを保存してから動かす
    void StopPlay();  // 実行停止：保存しておいた状態に戻す
    void UpdateKonami();
    void UpdateObjects();

    int FindPlayerObject() const;           // 役割がプレイヤーの最初のオブジェクト（なければ-1）
    // ---- 地面の向き：地面はXZ平面（上がY） ----
    bool Ground3D() const { return true; }
    Vector2 Planar(const Vector3& p) const { return Ground3D() ? Vector2{ p.x, p.z } : Vector2{ p.x, p.y }; }                 // 地面の上の座標（横, 奥）
    Vector3 FromPlanar(const Vector3& base, float u, float v) const { return Ground3D() ? Vector3{ u, base.y, v } : Vector3{ u, v, base.z }; }
    void SetHeading(Transform& t, float theta) const { if (Ground3D()) { t.Rotate.y = -theta; } else { t.Rotate.z = theta; } } // 向き（+横方向を0、反時計回り）
    float Heading(const Transform& t) const { return Ground3D() ? -t.Rotate.y : t.Rotate.z; }
    Vector3 CellWorld(float x, float y, float h) const { return Ground3D() ? Vector3{ x, h, y } : Vector3{ x, y, 0.0f }; }     // マス(x,y)の中心（h は地面からの高さ）
    int MapW() const;                       // マップの横のマス数
    int MapH() const;                       // マップの縦（奥行き）のマス数
    float wallHeight_ = 1.5f;
    std::vector<unsigned char> visFlags_;   // ブロックごとの表示判定。1:画面に映る 2:影の範囲内（ブロックが多いとき、見えないものの描画・更新を省く）               // 3Dマップの壁の高さ
    std::vector<unsigned char> wallGrid_;   // 再生中：壁のあるマス（当たり判定用）
    void BuildWallGrid();
    bool WallAt(int cx, int cz) const;      // そのマスが壁か（マップの外も壁）
    bool PushOutOfWalls(Vector2& p, float r) const; // 円が壁にめり込んでいたら押し出す（めり込んでいたら true）
    // ---- 時間倍率・画面の揺れ・ヒットストップ ----
    float timeScale_ = 1.0f;                // ゲーム全体の時間の速さ（1で通常。ゲームのコードから変える）
    float Dt() const { return camFx_.IsHitStop() ? 0.0f : (1.0f / 60.0f) * timeScale_; } // 1フレームの進み（時間倍率込み。ヒットストップ中は0）
    CameraEffects camFx_;                   // 画面の揺れ・ヒットストップ
    bool RaycastWalls(const Vector2& from, const Vector2& to, Vector2* hit = nullptr) const; // 地面の上の線分が壁に当たるか（当たった位置を hit に返す）
    void UpdateViewportInteraction(); // Viewport上のクリック選択・ドラッグ移動
    void UpdateCameraControl();       // 右クリックホールド中だけカメラを操作できるようにする
    void DrawSelectionOutline();      // 選択中の項目の枠と、拡大縮小ハンドルをViewportに重ねる
    // 選択中の項目のハンドル位置（ゲーム画面ピクセル座標）。0～3:四隅(左上・右上・右下・左下) 4～7:辺の中点(上・右・下・左)
    bool GetSelectionHandles(Vector2 outHandles[8]) const;

    std::unique_ptr<Camera> camera_;
    DebugCamera* debugCamera_ = nullptr; // camera_の中身（操作の有効/無効の切り替え用。所有はcamera_）
    bool showMarkers_ = true;             // show position markers for all objects/sprites
    bool middleActive_ = false;           // wheel-button drag rotates the camera
    bool cameraActive_ = false;           // 右クリックホールドでカメラ操作中
    std::vector<ObjectSlot> objects_;
    std::vector<SpriteSlot> sprites_;
    std::vector<PlanesSlot> planes_;        // 板ポリ大量配置
    std::vector<ParticleEmitter> emitters_; // シーンに置いたパーティクルエミッター
    std::vector<LightSlot> lights_;         // シーンに置いたポイント/スポットライト

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
        std::vector<LightSlot> lights;
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

    Vector3 shadowCenter_{ 0.0f, 0.0f, 0.0f }; // 影を落とす範囲の中心
    float shadowRange_ = 20.0f;                // 影を落とす範囲の半径

    DirectionalLight* directionalLight_ = nullptr; // ImGuiで編集する用

    Camera gameCamera_;            // マップ編集中だけ使うカメラ
    std::string sceneMessage_;     // 保存・読み込みの結果表示

    // マップ編集
    struct BlockEdit
    {
        int x = 0;
        int y = 0;
        bool before = false; // 変更前にブロックがあったか
        BlockProps beforeProps; // 変更前の設定
    };
    BlockProps mapProps_;                   // これから置くチップの設定
    int mapChip_ = 0;                       // 選んでいるチップの種類
    bool mapRect_ = false;                  // 四角でまとめて塗る・消す
    bool rectDrag_ = false;                 // 四角をドラッグ中
    bool rectErase_ = false;                // その四角は消す操作か
    int rectX0_ = 0, rectY0_ = 0, rectX1_ = 0, rectY1_ = 0;
    bool selValid_ = false;                 // 範囲選択がある
    int selX0_ = 0, selY0_ = 0, selX1_ = 0, selY1_ = 0;
    struct ClipCell
    {
        int dx = 0;
        int dy = 0;
        BlockProps p;
    };
    std::vector<ClipCell> clip_;            // コピーしたチップ
    int clipW_ = 0;
    int clipH_ = 0;
    void CopySelection(bool cut);           // 選択範囲をコピー（cut なら消す）
    void TransformClip(int mode);           // コピーしたものを変形（0:右に90度回す 1:左右反転 2:上下反転）
    bool mapEdit_ = false;
    bool mapEditPrev_ = false;
    int mapTool_ = 0;   // 0:ブロック 1:消しゴム 2:敵 3:盾の敵 4:ゴール 5:プレイヤー開始位置
    float enemySize_ = 1.2f;                // マップに置く敵の配置マーカーの大きさ（最大辺）
    int mapBrush_ = 1;  // ブラシの大きさ（n x n マス）
    float editCamX_ = 10.0f;
    float editCamY_ = 5.0f;
    float editDist_ = 16.0f;
    float editYaw_ = -1.5707963f;   // 3Dマップの編集カメラ：向き（-90度で画面の上が奥）
    float editPitch_ = 1.0471976f;  // 3Dマップの編集カメラ：見下ろす角度（90度に近いほど真上）
    bool mapHoverValid_ = false;
    int mapHoverX_ = 0;
    int mapHoverY_ = 0;
    bool mapPainting_ = false;
    bool mapErasing_ = false;
    std::vector<BlockEdit> mapStroke_;                 // 押してから離すまでの変更
    std::vector<std::vector<BlockEdit>> mapUndo_;      // アンドゥ用（上限200ストローク）
    char csvPath_[160] = "resources/stage.csv";
    char scenePath_[160] = "resources/scene.txt"; // シーンファイルの場所
    GameInput gameInput_; // 入力の窓口（キーボード・マウス・ゲームパッドをまとめる）
    // ブロックの見た目（全ブロック共通）
    int blockTexIndex_ = 0;                                  // 0:単色 i+1:textureFileList_[i]
    Vector4 blockTint_{ 0.42f, 0.72f, 0.32f, 1.0f };         // 色（テクスチャがあるときは乗算）
    bool blockStyleDirty_ = false;                           // true なら次のUpdateで全ブロックへ反映する
    Vector2 cubeWhiteUv_{ 0.5f, 0.5f };                      // パレットの白のマスのUV
};
