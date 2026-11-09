#pragma once
// 西部シューティング用のマップエディタ
//   3Dの見た目のまま、マスをマウスで塗って編集する。保存すると MapData の形式（resources/western_stage.csv など）になる。
//   操作（ビューポート上）:
//     左クリック / ドラッグ : 選んでいる道具で置く・塗る
//     右ボタン / ホイールボタンのドラッグ : 画面をつかんで動かす
//     ホイール : 拡大・縮小      WASD : 画面を動かす（文字を入力していないとき）
//   道具: 壁 / 樽 / 消す / 自機の開始位置 / 敵の出現点（ドア） / 区切り・建物（2回クリックで四角を作る）
//   見た目は MapRenderer（ゲームと同じ部品）で出す。建物の上でクリックして「消す」と、その建物がまるごと消える
#include <string>

#include "scene/Scene.h"
#include "../game/GameObject.h"
#include "../game/MapRenderer.h"
#include "../game/MapData.h"
#include "camera/Camera.h"
#include "math/Vector2.h"
#include "math/Vector3.h"

class MapEditorScene : public Scene
{
public:
    void Initialize() override;
    void Update() override;
    void Draw() override;
    void DrawImGui() override;

private:
    enum class Tool { Wall = 0, Barrel, Erase, PlayerStart, Spawn, Gate, Building };

    // --- 操作 ---
    void UpdateCamera();                 // 画面の移動・拡大縮小
    void UpdateHover();                  // マウスが指しているマスを求める
    void UpdateEditing();                // クリック・ドラッグでの編集
    void ApplyPaint(int x, int z);       // 壁・樽・消すを1マスに適用する
    void ApplyClick(int x, int z);       // 開始位置・出現点・区切り・建物を置く

    // --- 見た目 ---
    void RebuildWalls();                 // マップの壁・樽・建物を、見た目に反映する
    void ApplyCamera();
    void UpdateVisuals();
    void DrawOverlay();                  // 出現点・開始位置・区切りの印（ImGui）

    // --- ファイル ---
    void NewMap();
    void SaveMap();
    void LoadMap();

    static Vector3 CellCenter(int x, int z, float y) { return { x + 0.5f, y, z + 0.5f }; }
    bool CellToScreen(int x, int z, Vector2& out) const;

    Camera camera_;
    MapData map_;

    MapRenderer mapView_;       // 地面・壁・樽・建物・区切りの見た目（ゲームと同じ）
    GameObject cursor_;         // マウスが指しているマスの目印

    Tool tool_ = Tool::Wall;
    int currentArea_ = 0;       // 出現点・区切りに付ける区切り番号
    int spawnCount_ = 5;        // 新しい出現点の既定値
    float spawnInterval_ = 1.0f;

    // 画面の見え方
    Vector2 focus_{ 20.0f, 15.0f };   // 見ている地面の位置 (x, z)
    float distance_ = 26.0f;          // カメラの遠さ

    // マウス
    bool hover_ = false;
    int hoverX_ = 0;
    int hoverZ_ = 0;
    int lastPaintX_ = -1;
    int lastPaintZ_ = -1;
    bool gatePending_ = false;        // 区切りの1つ目の角を置いた
    int gateX_ = 0;
    int gateZ_ = 0;
    bool buildingPending_ = false;    // 建物の1つ目の角を置いた
    int buildingX_ = 0;
    int buildingZ_ = 0;
    int buildingKind_ = 0;            // 置く建物の種類（MapData::BuildingKind）
    int buildingFacing_ = 0;          // 置く建物の正面の向き（MapData::Facing。画面で 0=下 1=上 2=左 3=右）
    bool autoDoor_ = true;            // 酒場を置いたとき、正面に出現点（ドア）も置く

    // ファイル・メッセージ
    char path_[128] = "resources/western_stage.csv";
    int newWidth_ = 40;
    int newHeight_ = 30;
    std::string message_;
    bool dirty_ = true;               // 壁の見た目を作り直す必要がある
    bool poolFull_ = false;           // 壁・建物が多すぎて、立方体が足りない
};
