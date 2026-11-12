#include "WesternShooterScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "base/WinApp.h"
#include "scene/SceneManager.h"
#include "ResultScene.h"
#include "TitleScene.h"
#include "3d/ShadowMap.h"
#include "3d/PostProcess.h"
#include "base/DirectXCommon.h"
#include "base/Logger.h"
#include "core/LoadQueue.h"
#include "3d/Object3dCommon.h"
#include "input/Input.h"
#include "math/Collision.h"
#include "math/ScreenProjection.h"
#include "editor/EditorLayout.h"
#include "../game/GridNav.h"
#include "WesternShooterCommon.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

using namespace WesternShooter;

namespace {
    constexpr float kDt = 1.0f / 60.0f;
    constexpr float kGameOverToResult = 2.0f; // 倒されてから結果画面へ（秒）
    constexpr float kClearToResult = 3.0f;    // 全部倒してから結果画面へ（秒）

    constexpr const char* kMapPath = "resources/western_stage.csv";
    constexpr int kNoArea = 1 << 20;          // 区切りが無い・全部終わった、を表す番号（どの gate.area よりも大きい）
    constexpr int kBossArea = kNoArea - 1;    // ボス戦の区切り（どの gate.area よりも大きいので、扉はすべて開く）

    // --- カメラ ---
    // 斜め上から見下ろす（真上からだと平べったく見えるため）。角度は atan(高さ / 手前への距離) ≒ 60度
    constexpr float kCameraHeight = 17.0f;
    constexpr float kCameraBack = 10.0f;      // 見ている点から、手前(-z)へ引いた距離
    constexpr float kCameraFov = 0.8f;
    constexpr float kCameraFollow = 8.0f;     // 追いつく速さ（大きいほどきびきび。1秒あたり）
    constexpr float kCameraLead = 0.2f;       // 狙っている方向へ、自機との距離のこの割合だけ先を見る
    constexpr float kCameraLeadMax = 3.0f;
    constexpr float kCameraMargin = 8.0f;     // 見ている点をマップの端からこれだけ内側に収める

    // --- 自機 ---
    constexpr float kPlayerSpeed = 6.8f;      // 1秒あたり
    constexpr int kPlayerMaxHp = 5;
    constexpr float kInvincibleTime = 1.0f;   // ダメージ後の無敵（秒）

    // --- 自機の弾 ---
    constexpr float kBulletSpeed = 22.0f;
    constexpr float kBulletLife = 0.8f;
    constexpr float kBulletRadius = 0.25f;
    constexpr float kReloadTime = 1.4f;       // 撃ってから、次を撃てるまで（装填。ワンショットワンキルなので長い）
    constexpr size_t kPlayerBulletMax = 32;

    // --- 敵の弾 ---
    constexpr float kEnemyBulletSpeed = 10.0f;
    constexpr float kEnemyBulletLife = 3.0f;
    constexpr float kEnemyBulletRadius = 0.25f;
    constexpr float kEnemyBulletScale = 0.22f;   // 敵の弾の見た目の大きさ（当たり判定とは別。大きいほど見やすい）
    constexpr size_t kEnemyBulletMax = 48;

    // --- 出現 ---
    constexpr float kFirstSpawnDelay = 0.8f;

    // --- 自機（リボルバー）の見た目と、撃った反動 ---
    constexpr float kPlayerBodyScale = 0.42f; // 人型のモデルの大きさ
    constexpr float kGunScale = 1.0f;         // 手に持つ銃の大きさ（モデルは長さ約0.9）
    // 自機モデルの静止姿勢での右手の位置（モデルの座標）と、そこから前へ1進んだ点。手の動きに合わせて銃を置く・向けるのに使う
    const Vector3 kHandRest = { 0.85f, 2.0095f, 1.35f };
    const Vector3 kHandRestFront = { 0.85f, 2.0095f, 2.35f };
    constexpr float kRecoilTime = 0.22f;      // 反動が戻りきるまでの時間（実時間・秒）
    constexpr float kBodyKick = 0.12f;        // 撃った瞬間に、体が後ろへ下がる距離

    // --- 回避（転がる） ---
    constexpr float kRollTime = 0.22f;
    constexpr float kRollSpeed = 15.0f;
    constexpr float kRollCooldown = 0.9f;

    // --- デッドアイ ---
    constexpr float kDeadEyeScale = 0.18f;    // 狙っている間のゲームの速さ
    constexpr float kVolleyScale = 0.35f;     // 連射している間のゲームの速さ
    constexpr float kVolleyInterval = 0.12f;  // 連射の間隔（ゲームの時間）
    constexpr float kDeadEyeDrain = 0.3f;     // 狙っている間に減るゲージ（実時間1秒あたり）
    constexpr float kDeadEyeMin = 0.15f;      // これ以上たまっていないと始められない
    // ゲージは「危ないことをした」ときに増える。離れた所から倒すだけでは、ほとんど増えない
    constexpr float kDeadEyeHit = 0.02f;      // 当てたときに増えるゲージ
    constexpr float kDeadEyeKillNear = 0.22f; // 近い敵（kKillNearDist 以内）を倒したときに増えるゲージ
    constexpr float kDeadEyeKillFar = 0.02f;  // 遠い敵（kKillFarDist 以上）を倒したときに増えるゲージ
    constexpr float kKillNearDist = 3.0f;
    constexpr float kKillFarDist = 8.0f;
    constexpr float kDeadEyeGraze = 0.07f;    // 敵の弾をかすめて避けたときに増えるゲージ
    constexpr float kGrazeRadius = 0.8f;      // 自機の半径に足す、かすめたと見なす距離
    constexpr float kDeadEyeStart = 0.15f;    // 最初から持っているゲージ（ちょうど始められる量）
    constexpr size_t kMaxMarks = 6;
    constexpr float kMarkPickRadius = 1.2f;   // 敵の半径に足す、マークできる距離
    constexpr float kDeadEyeBulletSpeed = 45.0f;

    // --- 演出 ---
    constexpr float kSlowScale = 0.3f;        // 最後の1体を倒したときのスローモーションの速さ
    constexpr float kSlowTime = 0.75f;        // その長さ（実時間・秒）
    constexpr float kCorpseLife = 0.6f;       // 倒れた敵が吹っ飛んで消えるまで
    constexpr float kCorpseSpeed = 9.0f;      // 吹っ飛ぶ最初の速さ
    constexpr float kHitFlashTime = 0.09f;    // 当たったとき白く光る時間  // 区切りに入ってから最初の1体が出るまで

    // マップのファイルの更新時刻（読めなければ0）。前回から変わったかどうかの目印にする
    long long MapStamp()
    {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(kMapPath, ec);
        return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
    }

    // マスの四角（ワールド座標）
    Collision::Rect CellRect(int x, int z)
    {
        return { static_cast<float>(x), static_cast<float>(z), static_cast<float>(x + 1), static_cast<float>(z + 1) };
    }

    // 内蔵のマップ：マップファイルが無くても遊べるようにするためのもの
    void MakeFallbackMap(MapData& map)
    {
        map.Reset(32, 24);
        for (int x = 0; x < map.width; ++x) {
            map.Set(x, 0, MapData::Wall);
            map.Set(x, map.height - 1, MapData::Wall);
        }
        for (int z = 0; z < map.height; ++z) {
            map.Set(0, z, MapData::Wall);
            map.Set(map.width - 1, z, MapData::Wall);
        }
        map.Set(10, 10, MapData::Barrel);
        map.Set(21, 10, MapData::Barrel);
        map.Set(15, 12, MapData::Wall);
        map.Set(16, 12, MapData::Wall);

        map.hasPlayerStart = true;
        map.startX = 16;
        map.startZ = 3;

        const int doorX[3] = { 8, 16, 24 };
        for (int x : doorX) {
            MapData::Spawn s;
            s.x = x;
            s.z = 21;
            s.area = 0;
            s.count = 5;
            s.interval = 1.2f;
            map.spawns.push_back(s);
        }
    }
}

// ---------------------------------------------------------------- 初期化・やり直し

void WesternShooterScene::Initialize()
{
    while (!InitStep()) {
    }
}

bool WesternShooterScene::InitStep()
{
    switch (initStep_) {
    case 0:
        camera_.SetPerspective(kCameraFov, static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight, 0.1f, 300.0f);
        // 斜め上から見るカメラ（画面の奥＝+z）。マウスの位置を求めるため、最初の1フレームの前に作っておく
        camera_.SetLookAt({ 0.0f, kCameraHeight, -kCameraBack }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

        player_.Initialize(ctx_->object3dCommon, "ws_man_player.fbx", { 0.0f, kHeight, 0.0f });
        player_.GetObject3d().GetTransform().Scale = { kPlayerBodyScale, kPlayerBodyScale, kPlayerBodyScale };
        if (Material* pm = player_.GetObject3d().GetMaterial()) {
            pm->lightingMode = 0; // 自機は陰影なしで明るく見せる（敵と見分けやすいように）
        }
        revolver_.Initialize(ctx_->object3dCommon, "ws_revolver.obj", { 0.0f, kHeight, 0.0f });
        if (Material* gm = revolver_.GetObject3d().GetMaterial()) {
            gm->lightingMode = 0;
        }
        reticle_.Initialize(ctx_->object3dCommon, "teapot.obj", { 0.0f, kHeight, 0.0f });
        reticle_.GetObject3d().GetTransform().Scale = { 0.12f, 0.12f, 0.12f };
        SetTint(reticle_, 1.0f, 1.0f, 1.0f);
        SetPbr(player_, 0.6f, 0.0f);
        SetPbr(revolver_, 0.3f, 0.85f); // 銃は金属のつや
        break;
    // 弾も敵もマップの見た目も、最初にまとめて作っておく（出すたびに作らない）
    case 1:
        playerBulletPool_.Initialize(ctx_->object3dCommon, "teapot.obj", kPlayerBulletMax);
        enemyBulletPool_.Initialize(ctx_->object3dCommon, "teapot.obj", kEnemyBulletMax);
        break;
    case 2:
        brutePool_.Initialize(ctx_->object3dCommon, "ws_man_brute.fbx", kBruteMax);   // ブロックで作った人型（カウボーイハット付き）
        brutePool_.ForEachSlot([](GameObject& o) { SetPbr(o, 0.75f, 0.0f); });
        break;
    case 3:
        gunmanPool_.Initialize(ctx_->object3dCommon, "ws_man_gunman.fbx", kGunmanMax);
        gunmanPool_.ForEachSlot([](GameObject& o) { SetPbr(o, 0.75f, 0.0f); });
        break;
    case 4:
        bossPool_.Initialize(ctx_->object3dCommon, "ws_man_boss.fbx", 1);
        bossPool_.ForEachSlot([](GameObject& o) { SetPbr(o, 0.7f, 0.0f); });
        break;
    case 5:
        mapView_.Initialize(ctx_->object3dCommon, ctx_->modelManager);
        break;
    case 6:
        doors_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName); // 立方体は mapView_ が登録したものを使う
        dust_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName);
        weeds_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName);
        markPool_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName, kMaxMarks);
        playerMark_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName, { 0.0f, 0.05f, 0.0f });
        if (Material* mm = playerMark_.GetObject3d().GetMaterial()) {
            mm->lightingMode = 0;
        }
        SetTint(playerMark_, 0.3f, 1.0f, 0.4f);
        aimPool_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName, kGunmanMax + kBruteMax + 1); // ガンマン・ごろつき・自機のぶん
        doors_.SetPbr(0.6f, 0.0f);
        break;
    case 7:
        LoadMap();
        Reset();
        break;
    default:
        return true;
    }
    ++initStep_;
    return initStep_ >= kInitSteps;
}

void WesternShooterScene::LoadMap()
{
    std::string error;
    MapData loaded;
    mapFromFile_ = false;
    mapStamp_ = MapStamp();

    if (!loaded.Load(kMapPath, error)) {
        mapMessage_ = std::string("マップを読めなかったので内蔵マップで遊びます: ") + kMapPath + " (" + error + ")";
    }
    else {
        map_ = loaded;
        mapFromFile_ = true;
        mapMessage_ = std::string("マップを読みました: ") + kMapPath;
        if (!map_.hasPlayerStart) {
            // 開始位置が置かれていないマップ：真ん中にいちばん近い空きマスから始める
            const int cx = map_.width / 2;
            const int cz = map_.height / 2;
            int best = -1;
            int bestDist = 0;
            for (int z = 0; z < map_.height; ++z) {
                for (int x = 0; x < map_.width; ++x) {
                    const int d = (x - cx) * (x - cx) + (z - cz) * (z - cz);
                    if (!map_.IsSolid(x, z) && (best < 0 || d < bestDist)) {
                        best = z * map_.width + x;
                        bestDist = d;
                    }
                }
            }
            if (best < 0) {
                mapFromFile_ = false; // 空きマスが1つも無い
                mapMessage_ = "マップに空きマスが無いので内蔵マップで遊びます";
            }
            else {
                map_.startX = best % map_.width;
                map_.startZ = best / map_.width;
                map_.hasPlayerStart = true;
                mapMessage_ += "（自機の開始位置が無いので、中央付近から始めます）";
            }
        }
    }

    if (!mapFromFile_) {
        map_ = MapData();
        MakeFallbackMap(map_);
    }
    mapComplete_ = mapView_.Build(map_);

    // 夜の明かり：建物の正面に、あたたかい灯りを置く（夜になると自動で点く）
    look_.lamps.Clear();
    for (const Vector3& spot : mapView_.LampSpots()) {
        TimedLights::Lamp lamp;
        lamp.position = spot;
        lamp.color = { 1.0f, 0.62f, 0.28f };
        lamp.intensity = 2.8f;
        lamp.range = 8.0f;
        look_.lamps.Add(lamp);
    }
    doors_.Build(map_);
    if (!mapComplete_) {
        mapMessage_ += "（壁・樽が多すぎて、一部が描かれません）";
    }
}

void WesternShooterScene::Reset()
{
    // ポーズ画面は閉じて、項目を整える
    freeCam_ = false;
    freeLooking_ = false;
    pause_.Close();
    pause_.SetTitle("PAUSED");
    pause_.SetItems({ "RESUME", "RETRY", "BACK TO TITLE", "FREE CAMERA (DEBUG)" });
    playerBulletPool_.ReleaseAll();
    enemyBulletPool_.ReleaseAll();
    brutePool_.ReleaseAll();
    gunmanPool_.ReleaseAll();
    bossPool_.ReleaseAll();
    bossSpawned_ = false;
    playerBullets_.clear();
    enemyBullets_.clear();
    enemies_.clear();
    doors_.Reset();
    for (Corpse& k : corpses_) {
        k.obj = nullptr; // 敵のオブジェクトは下の ReleaseAll でまとめて返してある
    }
    corpses_.clear();
    slowTimer_ = 0.0f;
    marks_.clear();
    markPool_.ReleaseAll();
    deadEyeActive_ = false;
    volley_ = false;
    deadEyeGauge_ = kDeadEyeStart;
    rollTimer_ = 0.0f;
    rollCooldown_ = 0.0f;
    burstGate_ = 0.0f;
    rushWait_ = 0.0f;
    callTimer_ = 0.0f;
    dust_.Clear();
    weeds_.Clear();

    playerPos_ = { map_.startX + 0.5f, map_.startZ + 0.5f };
    cameraFocus_ = playerPos_;
    facing_ = { 0.0f, 1.0f };
    playerHp_ = kPlayerMaxHp;
    invincible_ = 0.0f;
    fireTimer_ = 0.0f;
    score_ = 0;
    endTimer_ = 0.0f;
    state_ = State::Playing;

    // 出現点：全部「まだ出していない」状態に戻し、いちばん小さい区切りから始める
    spawnStates_.assign(map_.spawns.size(), SpawnState{});
    currentArea_ = kNoArea;
    for (size_t i = 0; i < map_.spawns.size(); ++i) {
        spawnStates_[i].remaining = map_.spawns[i].count;
        spawnStates_[i].timer = kFirstSpawnDelay;
        currentArea_ = (std::min)(currentArea_, map_.spawns[i].area);
    }

    for (size_t i = 0; i < map_.gates.size(); ++i) {
        mapView_.SetGateVisible(i, IsGateClosed(i));
    }

    // フェーズの順番（区切りの番号の小さい順）。最初のフェーズの登場演出から始める
    areaOrder_.clear();
    for (const MapData::Spawn& sp : map_.spawns) {
        if (std::find(areaOrder_.begin(), areaOrder_.end(), sp.area) == areaOrder_.end()) {
            areaOrder_.push_back(sp.area);
        }
    }
    std::sort(areaOrder_.begin(), areaOrder_.end());
    phaseIndex_ = 0;
    cutscene_ = Cutscene{};
    pillars_.clear();
    shotHeight_ = kCameraHeight;
    shotBack_ = kCameraBack;
    shotYaw_ = 0.0f;
    shotLookY_ = 0.0f;
    if (currentArea_ != kNoArea) {
        StartPhaseCutscene();
    }
}

void WesternShooterScene::Restart()
{
    // マップエディタで保存し直したときだけ読み直す（読み直しは重いので、変わっていなければ飛ばす）
    if (MapStamp() != mapStamp_) {
        LoadMap();
    }
    Reset();
}

// ---------------------------------------------------------------- 見た目（光・影）

void WesternShooterScene::BeginLook()
{
    look_.Begin(ctx_);
}

void WesternShooterScene::EndLook()
{
    look_.End();
}

void WesternShooterScene::RenderShadowPass()
{
    if (!look_.BeginShadowPass()) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = ctx_->dxCommon->GetCommandList();
    mapView_.DrawShadow(commandList, cameraFocus_.x, cameraFocus_.y + 3.0f, look_.shadowRange + 1.0f);
    doors_.DrawShadow(commandList);
    if (state_ != State::GameOver) {
        player_.GetObject3d().DrawShadow(commandList);
        if (rollTimer_ <= 0.0f) {
            revolver_.GetObject3d().DrawShadow(commandList);
        }
    }
    brutePool_.DrawShadow(commandList);
    gunmanPool_.DrawShadow(commandList);
    bossPool_.DrawShadow(commandList);
    look_.EndShadowPass();
}

// ---------------------------------------------------------------- 毎フレーム

void WesternShooterScene::Update()
{
    const auto perfUpdateStart = std::chrono::steady_clock::now();
    struct PerfEnd { WesternShooterScene* s; std::chrono::steady_clock::time_point t; ~PerfEnd() { s->perfUpdateMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t).count(); } } perfEnd{ this, perfUpdateStart };
    // マウスの状態をゲーム画面の座標で受け取る（エディタのビューポート上の位置）
    bool mouseDown = false;
#ifdef USE_IMGUI
    {
        const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
        mousePixel_ = vp.mouse;
        mouseInside_ = vp.valid
            && vp.mouse.x >= 0.0f && vp.mouse.x < static_cast<float>(WinApp::kGameWidth)
            && vp.mouse.y >= 0.0f && vp.mouse.y < static_cast<float>(WinApp::kGameHeight);
        mouseDown = vp.down;
    }
#endif // USE_IMGUI
    input_.SetMouse(mousePixel_, mouseInside_, mouseDown);
    input_.Update(*ctx_->input);
    // ポーズ：開いている間はゲームを進めない（光・天気・雨だけ動かして、シェーダーの動きを見られるようにする）
    if (UpdatePause(state_ == State::Playing && cutscene_.kind == CutsceneKind::None)) {
        return;
    }
    fx_.Update(kDt); // 揺れ・ヒットストップは実時間で進める
    for (Pillar& p : pillars_) {
        p.age += kDt; // 光の柱は実時間で細くなって消える
    }
    pillars_.erase(std::remove_if(pillars_.begin(), pillars_.end(), [](const Pillar& p) { return p.age > 1.4f; }), pillars_.end());
    // スローモーション中は、ゲーム・粒・吹っ飛ぶ敵をゆっくりにする。ヒットストップ中は止める
    if (slowTimer_ > 0.0f && !fx_.IsHitStop()) {
        slowTimer_ = (std::max)(0.0f, slowTimer_ - kDt);
    }
    float timeScale = slowTimer_ > 0.0f ? kSlowScale : 1.0f;
    if (state_ == State::Playing) {
        timeScale = (std::min)(timeScale, UpdateDeadEye()); // デッドアイ中は、さらにゆっくり
    }
    const float effectDt = fx_.IsHitStop() ? 0.0f : kDt * timeScale;
    doors_.Step(kDt); // 扉の揺れは実時間で進める
    dust_.Step(effectDt);
    UpdateCorpses(effectDt);
    weeds_.Step(kDt, map_, cameraFocus_);

    // 登場演出の間は、ゲームの時間を止めて、演出だけを進める
    if (cutscene_.kind != CutsceneKind::None && state_ == State::Playing) {
        UpdateCutscene();
        UpdateVisuals();
        return;
    }

    if (state_ != State::Playing) {
        // ゲームオーバー・クリア後：しばらく余韻を見せてから結果画面へ。Rですぐやり直し。クリア後は歩き回れる（敵はもう出ない）
        endTimer_ += kDt;
        if (endTimer_ >= (state_ == State::Cleared ? kClearToResult : kGameOverToResult)) {
            ctx_->sceneManager->ChangeScene(std::make_unique<ResultScene>(state_ == State::Cleared, score_));
            return;
        }
        if (ctx_->input->TriggerKey(DIK_R)) {
            Reset();
        }
        else if (state_ == State::Cleared) {
            UpdateCamera(kDt);
            UpdateAim();
            UpdatePlayer(kDt);
            UpdateProjectiles(kDt);
            RemoveDead();
        }
        UpdateVisuals();
        return;
    }

    // ヒットストップ中は、ゲームの時間を進めない
    const float dt = fx_.IsHitStop() ? 0.0f : kDt * timeScale;

    UpdateCamera(kDt); // 狙う点はカメラから求めるので、先にカメラを合わせる
    UpdateAim();
    UpdatePlayer(dt);
    UpdateSpawn(dt);
    UpdateEnemies(dt);
    UpdateProjectiles(dt);
    ResolveHits();
    RemoveDead();
    UpdateArea();

    if (playerHp_ <= 0) {
        state_ = State::GameOver;
    }
    UpdateVisuals();
}

void WesternShooterScene::UpdateAim()
{
    // マウスの位置から視線を伸ばし、弾の高さの水平な面と交わる点を求める
    aimValid_ = false;
    if (!mouseInside_) {
        return;
    }
    Vector3 ground{};
    if (ScreenProjection::ScreenToGround(mousePixel_, camera_.GetViewMatrix(), camera_.GetProjectionMatrix(),
        static_cast<float>(WinApp::kGameWidth), static_cast<float>(WinApp::kGameHeight), kHeight, ground)) {
        aimPoint_ = { ground.x, ground.z };
        aimValid_ = true;
    }
}

void WesternShooterScene::UpdatePlayer(float dt)
{
    // デッドアイ中は、自機だけ少し速く動ける（ほかはゆっくり）。ヒットストップ中は止まる
    const float moveDt = (deadEyeActive_ && dt > 0.0f) ? kDt * 0.5f : dt;
    rollCooldown_ = (std::max)(0.0f, rollCooldown_ - dt);

    // 移動（右が+x、上が+y。上＝画面の奥＝+z）。壁と閉じた区切りには押し返される
    const Vector2 move = input_.Move();
    if (rollTimer_ > 0.0f) {
        // 転がっている：決めた向きへ一直線
        rollTimer_ = (std::max)(0.0f, rollTimer_ - dt);
        playerPos_.x += rollDir_.x * kRollSpeed * dt;
        playerPos_.y += rollDir_.y * kRollSpeed * dt;
    }
    else {
        playerPos_.x += move.x * kPlayerSpeed * moveDt;
        playerPos_.y += move.y * kPlayerSpeed * moveDt;
    }
    PushOutOfGates(playerPos_, kPlayerRadius);
    PushOutOfWalls(playerPos_, kPlayerRadius);

    // 撃つ向き：マウスが指す点 → （ゲームパッドなら右スティック）→ 最後に動いた向き、の順で決める
    if (aimValid_) {
        facing_ = Normalized({ aimPoint_.x - playerPos_.x, aimPoint_.y - playerPos_.y }, facing_);
    }
    else if (input_.GetDevice() == GameInput::Device::Gamepad && input_.HasAim()) {
        facing_ = Normalized(input_.Aim(), facing_);
    }
    else if (Length2D(move) > 0.0f) {
        facing_ = Normalized(move, facing_);
    }

    invincible_ = (std::max)(0.0f, invincible_ - dt);

    // 回避：動いている向き（止まっていれば狙っている向き）へ転がる
    if (input_.JumpPressed() && rollCooldown_ <= 0.0f && rollTimer_ <= 0.0f && !deadEyeActive_ && !volley_ && dt > 0.0f) {
        rollDir_ = Normalized(move, facing_);
        rollTimer_ = kRollTime;
        rollCooldown_ = kRollCooldown;
        dust_.Emit({ playerPos_.x, 0.15f, playerPos_.y }, 5, 2.4f, 0.35f, 0.5f, 0.82f, 0.72f, 0.52f);
    }

    fireTimer_ -= dt;
    if (deadEyeActive_) {
        // デッドアイ：クリックで狙っている敵にマークをつける
        if (input_.FirePressed()) {
            TryMark();
        }
    }
    else if (!volley_ && rollTimer_ <= 0.0f && input_.Fire() && fireTimer_ <= 0.0f && playerBulletPool_.ActiveCount() == 0) {
        // ふつうの射撃：弾は同時に1発だけ。消えてから装填が終わるまで撃てない
        FirePlayerBullet(facing_, kBulletSpeed, false);
        fireTimer_ = kReloadTime;
    }
    if (playerBulletPool_.ActiveCount() > 0 && !deadEyeActive_ && !volley_) {
        fireTimer_ = (std::max)(fireTimer_, 0.3f); // 弾が飛んでいる間は撃てない（消えたあとも少し間をおく）
    }
}

// デッドアイの状態を進める。ゲームの時間の速さ（1=ふつう）を返す
float WesternShooterScene::UpdateDeadEye()
{
    // マークした敵へ連射している間
    if (volley_) {
        if (!fx_.IsHitStop()) {
            volleyTimer_ -= kDt * kVolleyScale;
        }
        if (volleyTimer_ <= 0.0f) {
            if (volleyIndex_ < marks_.size()) {
                FireDeadEyeShot(marks_[volleyIndex_++]);
                volleyTimer_ = kVolleyInterval;
            }
            else {
                volley_ = false;
                marks_.clear();
            }
        }
        return kVolleyScale;
    }

    const bool want = input_.DeadEye();
    if (deadEyeActive_) {
        deadEyeGauge_ -= kDt * kDeadEyeDrain;
        if (!want || deadEyeGauge_ <= 0.0f) {
            deadEyeActive_ = false;
            deadEyeGauge_ = (std::max)(0.0f, deadEyeGauge_);
            if (!marks_.empty()) {
                volley_ = true;
                volleyIndex_ = 0;
                volleyTimer_ = 0.0f;
                return kVolleyScale;
            }
            return 1.0f;
        }
        return kDeadEyeScale;
    }
    if (want && deadEyeGauge_ >= kDeadEyeMin && rollTimer_ <= 0.0f) {
        deadEyeActive_ = true;
        marks_.clear();
        fx_.Shake(0.08f, 0.12f);
        return kDeadEyeScale;
    }
    return 1.0f;
}

// 狙っている点に一番近い敵にマークをつける（自機から見えて、射程内の敵だけ）
void WesternShooterScene::TryMark()
{
    if (!aimValid_ || marks_.size() >= kMaxMarks) {
        return;
    }
    const Enemy* best = nullptr;
    float bestDist = 1.0e9f;
    for (const Enemy& e : enemies_) {
        if (!e.obj) {
            continue;
        }
        const float radius = RadiusOf(e.type) + kMarkPickRadius;
        const float d = Length2D({ aimPoint_.x - e.pos.x, aimPoint_.y - e.pos.y });
        if (d > radius || d >= bestDist) {
            continue;
        }
        if (Length2D({ e.pos.x - playerPos_.x, e.pos.y - playerPos_.y }) > kSightRange || !GridNav::HasLineOfSight(map_, playerPos_, e.pos)) {
            continue;
        }
        best = &e;
        bestDist = d;
    }
    if (best) {
        marks_.push_back(best->id);
        dust_.EmitBurst({ best->pos.x, kHeight, best->pos.y }, { 0.0f, 1.0f, 0.0f }, 4, 3.0f, 0.5f, 0.2f, 0.25f, 1.0f, 0.2f, 0.2f, 0.0f, 3.0f, true);
    }
}

// マークした敵へ、まっすぐ速い弾を1発
void WesternShooterScene::FireDeadEyeShot(int enemyId)
{
    for (const Enemy& e : enemies_) {
        if (e.obj && e.id == enemyId) {
            facing_ = Normalized({ e.pos.x - playerPos_.x, e.pos.y - playerPos_.y }, facing_);
            FirePlayerBullet(facing_, kDeadEyeBulletSpeed, true);
            return;
        }
    }
    volleyTimer_ = 0.0f; // 先に倒された：すぐ次へ
}

void WesternShooterScene::UpdateSpawn(float dt)
{
    // いまの区切りのドアだけが、決まった数・決まった間隔で敵を出す
    for (size_t i = 0; i < map_.spawns.size() && i < spawnStates_.size(); ++i) {
        const MapData::Spawn& spawn = map_.spawns[i];
        SpawnState& st = spawnStates_[i];
        if (spawn.area != currentArea_ || st.remaining <= 0) {
            continue;
        }
        st.timer -= dt;
        if (st.timer > 0.0f) {
            continue;
        }
        if (SpawnEnemy(i)) {
            --st.remaining;
            st.timer = spawn.interval;
        }
        else {
            st.timer = 0.0f; // プールが満杯：空き次第すぐ出す
        }
    }
}

void WesternShooterScene::UpdateProjectiles(float dt)
{
    ++tracerTick_;
    // 自機の弾：寿命が尽きるか、壁・樽に入ったら返す（マップの外は壁として扱われる）
    for (Projectile& b : playerBullets_) {
        if (!b.obj) {
            continue;
        }
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        b.life -= dt;
        if (tracerTick_ % 3 == 0 && dt > 0.0f) {
            dust_.EmitBurst({ b.pos.x, kHeight, b.pos.y }, { 0.0f, 0.0f, 0.0f }, 1, 0.0f, 0.0f, 0.12f, 0.12f, 1.0f, 0.9f, 0.4f, 0.0f, 1.0f, true); // 弾の尾（自機の弾だけ）
        }
        const bool hitWall = IsSolidAt(b.pos);
        if (b.life <= 0.0f || hitWall) {
            if (hitWall) {
                // 壁に当たった：土煙と、跳ね返る火花
                const Vector2 back = Normalized({ -b.vel.x, -b.vel.y }, { 0.0f, 0.0f });
                dust_.Emit({ b.pos.x, kHeight, b.pos.y }, 2, 1.6f, 0.22f, 0.35f, 0.75f, 0.68f, 0.55f);
                dust_.EmitBurst({ b.pos.x, kHeight, b.pos.y }, { back.x, 0.3f, back.y }, 3, 7.0f, 0.7f, 0.1f, 0.25f, 1.0f, 0.85f, 0.4f, 12.0f, 1.0f, true);
            }
            playerBulletPool_.Release(b.obj);
            b.obj = nullptr;
            continue;
        }
        Place(*b.obj, b.pos);
    }

    // 敵の弾：同じく。自機に当たったらダメージを与えて返す
    for (Projectile& b : enemyBullets_) {
        if (!b.obj) {
            continue;
        }
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        b.life -= dt;
        bool remove = b.life <= 0.0f || IsSolidAt(b.pos);
        if (remove && b.life > 0.0f) {
            dust_.Emit({ b.pos.x, kHeight, b.pos.y }, 1, 1.0f, 0.18f, 0.3f, 0.75f, 0.68f, 0.55f); // 壁に当たった小さな土煙
        }
        if (!remove && state_ == State::Playing && rollTimer_ <= 0.0f && Collision::CircleCircle(b.pos, kEnemyBulletRadius, playerPos_, kPlayerRadius)) {
            DamagePlayer();
            remove = true;
        }
        // かすめて避けた：ゲージが増える（転がりながらなら2倍）
        if (!remove && !b.grazed && state_ == State::Playing && Collision::CircleCircle(b.pos, kEnemyBulletRadius, playerPos_, kPlayerRadius + kGrazeRadius)) {
            b.grazed = true;
            deadEyeGauge_ = (std::min)(1.0f, deadEyeGauge_ + kDeadEyeGraze * (rollTimer_ > 0.0f ? 2.0f : 1.0f));
            dust_.Emit({ playerPos_.x, kHeight + 0.4f, playerPos_.y }, 3, 1.5f, 0.2f, 0.4f, 1.0f, 0.9f, 0.4f);
        }
        if (remove) {
            enemyBulletPool_.Release(b.obj);
            b.obj = nullptr;
            continue;
        }
        Place(*b.obj, b.pos);
    }
}

void WesternShooterScene::ResolveHits()
{
    // 自機の弾 × 敵。1発の弾は、最初に当たった敵1体にだけ当たる
    // 倒した敵は obj が nullptr になるので、同じフレームに別の弾が重ねて倒すことはない
    for (Projectile& b : playerBullets_) {
        if (!b.obj) {
            continue;
        }
        for (Enemy& e : enemies_) {
            if (!e.obj || e.spawnT > 0.0f) {
                continue; // 飛び出している間は、弾が当たらない
            }
            const float radius = RadiusOf(e.type);
            if (!Collision::CircleCircle(b.pos, kBulletRadius, e.pos, radius)) {
                continue;
            }
            playerBulletPool_.Release(b.obj);
            b.obj = nullptr;
            const Vector2 hitDir = Normalized(b.vel, { 0.0f, 1.0f });
            const bool fromDeadEye = b.deadEye; // デッドアイの弾ではゲージを増やさない
            // ボスは撃ったあとの隙（Recover）に当てると2倍のダメージ
            const int damage = (e.type == EnemyType::Boss && e.bossState == BossState::Recover) ? 2 : 1;
            e.hp -= damage;
            if (e.hp <= 0) {
                KillEnemy(e, hitDir);
                if (!fromDeadEye) {
                    // 近いほど、たくさん増える（引き付けずに遠くから倒しても、ほとんど増えない）
                    const float d = Length2D({ e.pos.x - playerPos_.x, e.pos.y - playerPos_.y });
                    const float closeness = std::clamp((kKillFarDist - d) / (kKillFarDist - kKillNearDist), 0.0f, 1.0f);
                    deadEyeGauge_ = (std::min)(1.0f, deadEyeGauge_ + (kDeadEyeKillFar + (kDeadEyeKillNear - kDeadEyeKillFar) * closeness));
                }
            }
            else {
                if (!fromDeadEye) {
                    deadEyeGauge_ = (std::min)(1.0f, deadEyeGauge_ + kDeadEyeHit);
                }
                // 倒れなかった：白く光り、血しぶきが散り、少し押される
                e.hitFlash = kHitFlashTime;
                e.hitReact = 1.0f; // のけぞる（体の傾きは WesternShooterEnemyAI.cpp が使う）
                e.hitReactDir = hitDir;
                if (e.type != EnemyType::Boss || damage > 1) {
                    e.obj->GetObject3d().PlayAnimation("Hit", false, 0.04f); // のけぞる（ボスは隙に当てたときだけ）
                    e.animLock = 0.4f;
                }
                if (e.type != EnemyType::Boss) {
                    e.pos.x += hitDir.x * 0.3f; // ボスは重いので押されない
                    e.pos.y += hitDir.y * 0.3f;
                }
                EmitBlood(e.pos, hitDir, damage > 1 ? 12 : 6);
                fx_.Shake(0.05f, 0.08f);
                fx_.HitStop(1);
            }
            break;
        }
    }
}

void WesternShooterScene::RemoveDead()
{
    auto deadProjectile = [](const Projectile& p) { return p.obj == nullptr; };
    playerBullets_.erase(std::remove_if(playerBullets_.begin(), playerBullets_.end(), deadProjectile), playerBullets_.end());
    enemyBullets_.erase(std::remove_if(enemyBullets_.begin(), enemyBullets_.end(), deadProjectile), enemyBullets_.end());
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return e.obj == nullptr; }), enemies_.end());
}

void WesternShooterScene::UpdateArea()
{
    // 出現点が無いマップ（currentArea_ == kNoArea）は、ずっと自由に遊べる
    if (currentArea_ == kNoArea || !enemies_.empty()) {
        return;
    }
    // いまの区切りに、まだ出していない敵がいれば続行
    for (size_t i = 0; i < map_.spawns.size() && i < spawnStates_.size(); ++i) {
        if (map_.spawns[i].area == currentArea_ && spawnStates_[i].remaining > 0) {
            return;
        }
    }

    // 倒し切った：次に大きい区切りへ。無ければクリア
    int next = kNoArea;
    for (const MapData::Spawn& s : map_.spawns) {
        if (s.area > currentArea_) {
            next = (std::min)(next, s.area);
        }
    }
    currentArea_ = next;
    if (next == kNoArea) {
        // 全部の区切りを倒した：まだボスを出していなければ、ボス戦へ。ボスを倒したらクリア
        if (!bossSpawned_) {
            currentArea_ = kBossArea;
            // ボスが出てくるドア：自機から12以上離れていて、自機へ道が通じているドアのうち、いちばん近いもの
            // （最後に使ったドアは、自機から遠い・壁の向こうのことがあり、ボスが出てこない・埋まる原因になった）
            {
                float best = 1.0e9f;
                bool found = false;
                for (const MapData::Spawn& s : map_.spawns) {
                    const Vector2 p = { s.x + 0.5f, s.z + 0.5f };
                    const float d = Length2D({ p.x - playerPos_.x, p.y - playerPos_.y });
                    std::vector<Vector2> path;
                    if (d >= 12.0f && d < best && GridNav::FindPath(map_, p, playerPos_, kBossRadius, path)) {
                        best = d;
                        lastDoorPos_ = p;
                        found = true;
                    }
                }
                (void)found; // 見つからなければ、最後に使ったドアのまま
            }
            StartBossCutscene(); // ボスは演出の途中で飛び出す
        }
        else {
            state_ = State::Cleared;
        }
    }
    else {
        for (size_t i = 0; i < map_.spawns.size() && i < spawnStates_.size(); ++i) {
            if (map_.spawns[i].area == next) {
                spawnStates_[i].timer = kFirstSpawnDelay;
            }
        }
        phaseIndex_ = static_cast<int>(std::find(areaOrder_.begin(), areaOrder_.end(), next) - areaOrder_.begin());
    }
    for (size_t i = 0; i < map_.gates.size(); ++i) {
        mapView_.SetGateVisible(i, IsGateClosed(i));
    }
    if (state_ == State::Playing && currentArea_ != kNoArea && currentArea_ != kBossArea) {
        StartPhaseCutscene(); // 次のフェーズ：敵が集団で飛び出してくる
    }
}

void WesternShooterScene::UpdateCamera(float dt)
{
    // 見ている点：自機。狙っている方向へ少し先を見る。マップの中に収める（マップが小さい向きは真ん中）
    Vector2 target = playerPos_;
    if (aimValid_) {
        Vector2 lead = { (aimPoint_.x - playerPos_.x) * kCameraLead, (aimPoint_.y - playerPos_.y) * kCameraLead };
        const float len = Length2D(lead);
        if (len > kCameraLeadMax) {
            lead = { lead.x / len * kCameraLeadMax, lead.y / len * kCameraLeadMax };
        }
        target.x += lead.x;
        target.y += lead.y;
    }
    auto fit = [](float v, float size) {
        if (size <= kCameraMargin * 2.0f) {
            return size * 0.5f;
        }
        return std::clamp(v, kCameraMargin, size - kCameraMargin);
    };
    target.x = fit(target.x, static_cast<float>(map_.width));
    target.y = fit(target.y, static_cast<float>(map_.height));

    // なめらかに追いつく（フレームの長さによらず同じ速さになる式）
    const float k = 1.0f - std::exp(-kCameraFollow * dt);
    cameraFocus_.x += (target.x - cameraFocus_.x) * k;
    cameraFocus_.y += (target.y - cameraFocus_.y) * k;

    ApplyCamera();
}

void WesternShooterScene::ApplyCamera()
{
    // 揺れはカメラの位置に足す
    const Vector2 sk = fx_.ShakeOffset();
    const float cx = cameraFocus_.x + sk.x;
    const float cz = cameraFocus_.y + sk.y;
    // 見ている点のまわりを yaw だけ回った位置から見る（ふだんは yaw=0 で、真後ろの手前から見下ろす）
    camera_.SetLookAt({ cx + std::sin(shotYaw_) * shotBack_, shotHeight_, cz - std::cos(shotYaw_) * shotBack_ }, { cx, shotLookY_, cz }, { 0.0f, 1.0f, 0.0f });
}

void WesternShooterScene::UpdateVisuals()
{
    // 無敵の間は赤く見せる
    if (invincible_ > 0.0f) {
        SetTint(player_, 1.0f, 0.4f, 0.4f);
    }
    else {
        SetTint(player_, 1.0f, 1.0f, 1.0f); // 銃の本来の色
    }
    recoil_ = (std::max)(0.0f, recoil_ - kDt / kRecoilTime);
    hurt_ = (std::max)(0.0f, hurt_ - kDt / 0.4f);
    playerAnimLock_ = (std::max)(0.0f, playerAnimLock_ - kDt);

    // 歩く速さ（歩き・走りのアニメーションと、足が着いたときの土ぼこりに使う）
    float playerSpeed = 0.0f;
    {
        playerSpeed = Length2D({ playerPos_.x - prevPlayerPos_.x, playerPos_.y - prevPlayerPos_.y }) / kDt;
        prevPlayerPos_ = playerPos_;
        const float target = (rollTimer_ > 0.0f) ? 0.0f : (std::min)(playerSpeed / 5.0f, 1.2f);
        playerWalkAmt_ += (target - playerWalkAmt_) * (std::min)(1.0f, kDt * 12.0f);
        const float before = playerWalkPhase_;
        playerWalkPhase_ += kDt * (8.0f + playerSpeed * 0.8f);
        // 足が着くたび、小さな土ぼこり
        if (playerWalkAmt_ > 0.5f && std::floor(before / 3.1415927f) != std::floor(playerWalkPhase_ / 3.1415927f)) {
            dust_.Emit({ playerPos_.x, 0.1f, playerPos_.y }, 1, 0.9f, 0.3f, 0.25f, 0.82f, 0.72f, 0.52f);
        }
    }
    Place(reticle_, aimPoint_);

    // 自機の足もとの印（明るい緑の平たい板）。ゆっくり脈打つ
    {
        Transform& mt = playerMark_.GetObject3d().GetTransform();
        const float pulse = 1.0f + 0.1f * std::sin(static_cast<float>(tracerTick_) * 0.08f);
        mt.Translate = { playerPos_.x, 0.05f, playerPos_.y };
        mt.Scale = { 1.7f * pulse, 0.02f, 1.7f * pulse };
    }

    // 自機の姿勢：向いている方向へ向く。動きは骨のアニメーション（待機・歩き・走り・撃つ・やられ・転がり・倒れる）
    //   右手の銃は、手の骨の動きに合わせて置く（撃つと腕が跳ね上がり、銃口も上を向く）。転がっている間は前転する
    {
        Object3d& po = player_.GetObject3d();
        Transform& pt = po.GetTransform();
        const bool rolling = rollTimer_ > 0.0f;
        const Vector2 dir = rolling ? rollDir_ : facing_;
        const float yaw = std::atan2(dir.x, dir.y);
        const float e = recoil_ * recoil_; // 撃った直後に大きく、すぐ戻る
        pt.Translate = { playerPos_.x - dir.x * kBodyKick * e, kHeight, playerPos_.y - dir.y * kBodyKick * e };
        pt.Rotate = { rolling ? (1.0f - rollTimer_ / kRollTime) * 6.2831853f : 0.0f, yaw, 0.0f };
        pt.Scale = { kPlayerBodyScale, kPlayerBodyScale, kPlayerBodyScale };

        // どのアニメーションを流すか（撃つ・やられは撃った瞬間に始めているので、その間は切り替えない）
        if (state_ == State::GameOver) {
            po.PlayAnimation("Death", false, 0.08f, 1.0f, false); // 倒れたまま（毎フレーム呼んでも、やり直さない）
        }
        else if (rolling) {
            po.PlayAnimation("Roll", true, 0.05f);
        }
        else if (playerAnimLock_ <= 0.0f) {
            if (playerSpeed > 3.2f) {
                po.PlayAnimation("Run", true, 0.1f, std::clamp(playerSpeed / 5.3f, 0.7f, 1.5f));
            }
            else if (playerSpeed > 0.5f) {
                po.PlayAnimation("Walk", true, 0.1f, std::clamp(playerSpeed / 2.2f, 0.6f, 1.6f));
            }
            else {
                po.PlayAnimation("Idle", true, 0.15f);
            }
        }
        po.UpdateSkin(kDt);

        // 右手の銃：手の位置に置き、手から前へ伸びる向きに向ける
        Transform& gt = revolver_.GetObject3d().GetTransform();
        po.GetBonePointWorld("ArmR", kHandRest, handPos_);
        Vector3 front = handPos_;
        po.GetBonePointWorld("ArmR", kHandRestFront, front);
        Vector3 gunDir = { front.x - handPos_.x, front.y - handPos_.y, front.z - handPos_.z };
        const float gl = std::sqrt(gunDir.x * gunDir.x + gunDir.y * gunDir.y + gunDir.z * gunDir.z);
        if (gl > 1e-4f) {
            gunDir = { gunDir.x / gl, gunDir.y / gl, gunDir.z / gl };
        }
        else {
            gunDir = { dir.x, 0.0f, dir.y };
        }
        gt.Translate = handPos_;
        // モデルの +z が銃口。x を正に回すと銃口が下がるので、上を向くときは負の角度
        gt.Rotate = { -std::asin(std::clamp(gunDir.y, -1.0f, 1.0f)), std::atan2(gunDir.x, gunDir.z), 0.0f };
        gt.Scale = { kGunScale, kGunScale, kGunScale };
    }

    // 照準の色：撃てる=白、装填中=暗い赤、デッドアイ中=赤（大きく）
    {
        const bool ready = deadEyeActive_ ? marks_.size() < kMaxMarks : (fireTimer_ <= 0.0f && playerBulletPool_.ActiveCount() == 0);
        const float rs = deadEyeActive_ ? 0.2f : 0.12f;
        reticle_.GetObject3d().GetTransform().Scale = { rs, rs, rs };
        if (deadEyeActive_) {
            SetTint(reticle_, 1.0f, 0.2f, 0.2f);
        }
        else if (ready) {
            SetTint(reticle_, 1.0f, 1.0f, 1.0f);
        }
        else {
            SetTint(reticle_, 0.45f, 0.2f, 0.2f);
        }
    }

    // 狙いの線（細長い箱）。壁か最大の長さで止まる。陰影なしで光って見せる
    aimPool_.ReleaseAll();
    auto drawLine = [&](const Vector2& from, const Vector2& dirIn, float maxLen, float width, float r, float g, float b) {
        const Vector2 d = Normalized(dirIn, { 0.0f, -1.0f });
        float len = 0.0f;
        while (len < maxLen && !IsSolidAt({ from.x + d.x * len, from.y + d.y * len })) {
            len += 0.25f;
        }
        if (len < 0.5f) {
            return;
        }
        GameObject* ln = aimPool_.Acquire({ from.x + d.x * len * 0.5f, kHeight, from.y + d.y * len * 0.5f });
        if (!ln) {
            return;
        }
        Transform& lt = ln->GetObject3d().GetTransform();
        lt.Scale = { len, 0.03f, width };
        lt.Rotate.y = std::atan2(-d.y, d.x);
        if (Material* m = ln->GetObject3d().GetMaterial()) {
            m->lightingMode = 0;
        }
        SetTint(*ln, r, g, b);
    };

    // 敵：ガンマンは撃つ前、ごろつきは突っ込む前。構え始めは赤く細く、向きが固定されたら黄色く太くなる。
    // 見やすさのため、全部で kMaxAimLines 本まで。固定された線（もうすぐ撃つ）を先に、残りは自機に近い順に出す
    constexpr int kMaxAimLines = 4;
    std::vector<const Enemy*> aiming;
    for (const Enemy& e : enemies_) {
        if (!e.obj) {
            continue;
        }
        const bool gunner = e.type == EnemyType::Gunman && e.state == AiState::Shoot && e.shotsLeft > 0;
        const bool charger = e.type == EnemyType::Brute && e.state == AiState::Rush && e.rushPhase == 0;
        if (gunner || charger) {
            aiming.push_back(&e);
        }
    }
    std::sort(aiming.begin(), aiming.end(), [&](const Enemy* a, const Enemy* b) {
        if (a->aimLocked != b->aimLocked) {
            return a->aimLocked; // 固定された線が先
        }
        return Length2D({ a->pos.x - playerPos_.x, a->pos.y - playerPos_.y }) < Length2D({ b->pos.x - playerPos_.x, b->pos.y - playerPos_.y });
    });
    if (aiming.size() > static_cast<size_t>(kMaxAimLines)) {
        aiming.resize(kMaxAimLines);
    }
    for (const Enemy* ep : aiming) {
        const Enemy& e = *ep;
        if (e.type == EnemyType::Gunman) {
            if (e.aimLocked) {
                drawLine(e.pos, e.aimDir, kAimLineLength, 0.12f, 1.0f, 0.95f, 0.5f);
            }
            else {
                drawLine(e.pos, e.aimDir, kAimLineLength, 0.05f, 0.9f, 0.2f, 0.15f);
            }
        }
        else {
            const float chargeLen = kBruteChargeSpeed * kBruteChargeTime;
            if (e.aimLocked) {
                drawLine(e.pos, e.aimDir, chargeLen, 0.9f, 1.0f, 0.95f, 0.5f);
            }
            else {
                drawLine(e.pos, e.aimDir, chargeLen, 0.35f, 0.9f, 0.2f, 0.15f);
            }
        }
    }

    // 登場演出の光の柱（戦隊モノ風）。着地した場所に立ち、細くなりながら消える
    for (const Pillar& p : pillars_) {
        const float life = std::clamp(p.age / 1.4f, 0.0f, 1.0f);
        const float w = 0.8f * p.scale * (1.0f - life);
        if (w < 0.02f) {
            continue;
        }
        GameObject* col = aimPool_.Acquire({ p.pos.x, 5.0f, p.pos.y });
        if (!col) {
            break;
        }
        col->GetObject3d().GetTransform().Scale = { w, 10.0f, w };
        if (Material* m = col->GetObject3d().GetMaterial()) {
            m->lightingMode = 0;
        }
        SetTint(*col, p.r, p.g, p.b);
    }

    // 飛び出している敵：着地点に赤い輪（着地の瞬間にこの中にいると踏みつぶされる）。近づくほど大きく濃くなる
    for (const Enemy& e : enemies_) {
        if (!e.obj || e.spawnT <= 0.0f) {
            continue;
        }
        const float t = 1.0f - e.spawnT / e.spawnDur;
        const float radius = (e.type == EnemyType::Boss ? kBossStompRadius : kSpawnStompRadius) * (0.6f + 0.4f * t);
        GameObject* ring = aimPool_.Acquire({ e.spawnTo.x, 0.06f, e.spawnTo.y });
        if (!ring) {
            continue;
        }
        Transform& rt = ring->GetObject3d().GetTransform();
        rt.Scale = { radius * 2.0f, 0.02f, radius * 2.0f };
        if (Material* m = ring->GetObject3d().GetMaterial()) {
            m->lightingMode = 0;
        }
        SetTint(*ring, 1.0f, 0.15f + 0.3f * t, 0.1f);
    }

    // ボス：構えの間は、撃つ向きの線が出る（扇は全部の弾の向き、連射は1本）。向きが固定されたら黄色く太くなる
    for (const Enemy& e : enemies_) {
        if (!e.obj || e.type != EnemyType::Boss || e.bossState != BossState::Telegraph) {
            continue;
        }
        const bool enraged = e.hp * 2 <= kBossHp;
        const int n = (e.bossAttack == BossAttack::Fan) ? (enraged ? kBossFanCount + 2 : kBossFanCount) : 1;
        for (int i = 0; i < n; ++i) {
            const float angle = (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * kBossFanSpread;
            const float c = std::cos(angle);
            const float sn = std::sin(angle);
            const Vector2 d = { e.aimDir.x * c - e.aimDir.y * sn, e.aimDir.x * sn + e.aimDir.y * c };
            if (e.aimLocked) {
                drawLine(e.pos, d, kAimLineLength, 0.12f, 1.0f, 0.95f, 0.5f);
            }
            else {
                drawLine(e.pos, d, kAimLineLength, 0.05f, 0.9f, 0.2f, 0.15f);
            }
        }
    }

    // 自機：撃つ向きの線。撃てるときは明るく、装填中は暗い赤。デッドアイ中は出さない（マークで狙うため）
    if (state_ != State::GameOver && !deadEyeActive_ && !volley_ && cutscene_.kind == CutsceneKind::None) {
        const bool ready = fireTimer_ <= 0.0f && playerBulletPool_.ActiveCount() == 0;
        if (ready) {
            drawLine(playerPos_, facing_, kBulletSpeed * kBulletLife, 0.04f, 0.9f, 0.9f, 0.7f);
        }
        else {
            drawLine(playerPos_, facing_, kBulletSpeed * kBulletLife, 0.03f, 0.4f, 0.12f, 0.12f);
        }
    }

    // マークの目印：マークした敵の頭の上で回る赤い箱
    markPool_.ReleaseAll();
    for (size_t i = 0; i < marks_.size(); ++i) {
        for (const Enemy& e : enemies_) {
            if (e.obj && e.id == marks_[i]) {
                GameObject* m = markPool_.Acquire({ e.pos.x, 1.4f, e.pos.y });
                if (m) {
                    Transform& mt = m->GetObject3d().GetTransform();
                    mt.Scale = { 0.35f, 0.35f, 0.35f };
                    mt.Rotate = { 0.6f, static_cast<float>(i) * 0.7f + deadEyeGauge_ * 20.0f, 0.6f };
                    SetTint(*m, 1.0f, 0.15f, 0.15f);
                }
                break;
            }
        }
    }

    PrepareRender();
}

void WesternShooterScene::PrepareRender()
{
    ApplyCamera(); // 揺れを足し直す（描くのはこのカメラ）
    look_.Update(ActiveCamera().GetProjectionMatrix(), { cameraFocus_.x, 0.0f, cameraFocus_.y + 3.0f }); // 光の向きと影の範囲を決めてから、各オブジェクトの行列（LightWVP）を更新する

    // 見た目の行列を更新（描く前に必ず）。使用中のものだけ
    const Matrix4x4& view = ActiveCamera().GetViewMatrix();
    const Matrix4x4& proj = ActiveCamera().GetProjectionMatrix();
    // カメラに映る範囲だけ更新・描画する（マップ全体の立方体は2000個近いので、毎フレーム全部だと重い）。
    // 範囲はカメラ（高さ17・手前10・縦の画角0.8）から求めた値に余裕を足したもの
    float pad = cutscene_.kind != CutsceneKind::None ? 10.0f : 0.0f; // 演出のカメラは回り込むので、壁・建物は広めに描く
    float propPad = 0.0f;
    Vector2 center = cameraFocus_;
    if (freeCam_) {
        // 自由カメラ：カメラの真下を中心に、広めに描く（どこへ飛んでも景色が欠けないように）
        const Vector3 fp = freeCamera_.GetPosition();
        center = { fp.x, fp.z };
        pad = 45.0f;
        propPad = 45.0f;
    }
    mapView_.SetPropRect(center.x - 28.0f - propPad, center.y - 12.0f - propPad, center.x + 28.0f + propPad, center.y + 22.0f + propPad); // 小物は通常は広げない（数が多く、画面の端なら気づかれにくい）
    mapView_.SetVisibleRect(center.x - 28.0f - pad, center.y - 12.0f - pad, center.x + 28.0f + pad, center.y + 22.0f + pad);
    // カメラと自機・敵の間にある建物は、半透明にして位置が分かるようにする
    {
        occlusionTargets_.clear();
        occlusionTargets_.push_back(playerPos_);
        for (const Enemy& oe : enemies_) {
            if (oe.obj) {
                occlusionTargets_.push_back(oe.pos);
            }
        }
        mapView_.SetOcclusionTargets(occlusionTargets_);
    }
    const auto perfMapStart = std::chrono::steady_clock::now();
    mapView_.Update(view, proj);
    perfMapMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - perfMapStart).count();
    doors_.Update(view, proj);
    dust_.Update(view, proj);
    weeds_.Update(view, proj);
    player_.GetObject3d().Update(view, proj);
    revolver_.GetObject3d().Update(view, proj);
    reticle_.GetObject3d().Update(view, proj);
    markPool_.Update(view, proj);
    playerMark_.GetObject3d().Update(view, proj);
    aimPool_.Update(view, proj);
    playerBulletPool_.Update(view, proj);
    enemyBulletPool_.Update(view, proj);
    brutePool_.Update(view, proj);
    gunmanPool_.Update(view, proj);
    bossPool_.Update(view, proj);
    RenderShadowPass();
}

Vector3 WesternShooterScene::WeatherFocus() const
{
    if (freeCam_) {
        const Vector3 p = freeCamera_.GetPosition();
        return { p.x, 0.0f, p.z };
    }
    return { cameraFocus_.x, 0.0f, cameraFocus_.y + 3.0f };
}

// ポーズの処理。ゲームを止めるべきときは true を返す（このフレームの Update はそこで終わり）
bool WesternShooterScene::UpdatePause(bool canPause)
{
    Input& in = *ctx_->input;

    // 自由カメラ中：メニューは隠れている。開閉キーでメニューへ戻る
    if (freeCam_) {
        if (in.TriggerKey(DIK_ESCAPE) || in.TriggerKey(DIK_P) || in.TriggerGamepadButton(7)) {
            freeCam_ = false;
            freeLooking_ = false;
            pause_.Open();
        }
        else {
            Vector2 delta{ 0.0f, 0.0f };
#ifdef USE_IMGUI
            {
                const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
                if (vp.rightDown) {
                    if (freeLooking_) {
                        delta = { vp.mouse.x - freeMouse_.x, vp.mouse.y - freeMouse_.y };
                    }
                    freeMouse_ = vp.mouse;
                    freeLooking_ = true;
                }
                else {
                    freeLooking_ = false;
                }
            }
#endif // USE_IMGUI
            freeCamera_.Update(in, delta, kDt);
        }
        PrepareRender();
        return true;
    }

    const int r = pause_.Update(in, canPause);
    if (!pause_.IsOpen()) {
        return false; // 閉じている（閉じたフレームもここ。そのまま再開する）
    }
    switch (r) {
    case 0: // 再開（クリックで決めたとき、そのクリックが弾を撃たないように、このフレームは進めない）
        pause_.Close();
        break;
    case 1: // リトライ
        Restart(); // Reset の中でメニューも閉じる
        break;
    case 2: // タイトルへ
        pause_.Close();
        ctx_->sceneManager->ChangeScene<TitleScene>();
        return true;
    case 3: // 自由カメラ（シェーダーや光の確認用）
        pause_.Close();
        freeCam_ = true;
        freeLooking_ = false;
        freeCamera_.StartFrom(camera_);
        break;
    default:
        break;
    }
    PrepareRender();
    return true;
}

void WesternShooterScene::Draw()
{
    const auto perfDrawStart = std::chrono::steady_clock::now();
    ctx_->object3dCommon->PreDraw();
    mapView_.Draw();
    perfMapDrawMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - perfDrawStart).count();
    weeds_.Draw();
    doors_.Draw();
    if (state_ != State::GameOver) {
        playerMark_.Draw();
        player_.Draw();
        if (rollTimer_ <= 0.0f) {
            revolver_.Draw(); // 転がっている間は、銃は描かない
        }
        if (aimValid_) {
            reticle_.Draw(); // マウスが指している点
        }
    }
    playerBulletPool_.Draw();
    enemyBulletPool_.Draw();
    brutePool_.Draw();
    gunmanPool_.Draw();
    bossPool_.Draw();
    aimPool_.Draw();
    markPool_.Draw();
    dust_.Draw();
    look_.DrawWeather(ActiveCamera().GetViewMatrix(), ActiveCamera().GetProjectionMatrix(), WeatherFocus()); // 雨（3D）
    perfDrawMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - perfDrawStart).count();
}

// ---------------------------------------------------------------- 壁・区切り

void WesternShooterScene::PushOutOfWalls(Vector2& pos, float radius) const
{
    // 円がかかるマスだけを調べ、壁・樽の四角から押し出す。角で押し合うことがあるので2回まわす
    for (int iter = 0; iter < 2; ++iter) {
        const int x0 = static_cast<int>(std::floor(pos.x - radius));
        const int x1 = static_cast<int>(std::floor(pos.x + radius));
        const int z0 = static_cast<int>(std::floor(pos.y - radius));
        const int z1 = static_cast<int>(std::floor(pos.y + radius));
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (map_.IsSolid(x, z)) { // マップの外も壁として扱う
                    pos = Collision::PushCircleOutOfRect(pos, radius, CellRect(x, z));
                }
            }
        }
    }
}

void WesternShooterScene::PushOutOfGates(Vector2& pos, float radius) const
{
    for (size_t i = 0; i < map_.gates.size(); ++i) {
        if (!IsGateClosed(i)) {
            continue;
        }
        const MapData::Gate& g = map_.gates[i];
        const Collision::Rect rect = { static_cast<float>(g.x0), static_cast<float>(g.z0),
                                       static_cast<float>(g.x1 + 1), static_cast<float>(g.z1 + 1) };
        pos = Collision::PushCircleOutOfRect(pos, radius, rect);
    }
}

bool WesternShooterScene::IsGateClosed(size_t index) const
{
    // 区切り k は、k 番目の敵を倒し切るまで閉じている（いまの区切り以降はすべて閉じている）
    return index < map_.gates.size() && map_.gates[index].area >= currentArea_;
}

bool WesternShooterScene::IsSolidAt(const Vector2& pos) const
{
    return map_.IsSolid(static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y)));
}

// ---------------------------------------------------------------- 出す

void WesternShooterScene::FirePlayerBullet(const Vector2& dir, float speed, bool deadEye)
{
    recoil_ = 1.0f; // 撃った反動（見た目）
    player_.GetObject3d().PlayAnimation("Shoot", false, 0.03f); // 撃つ：腕が跳ね上がる
    playerAnimLock_ = 0.3f;
    GameObject* obj = playerBulletPool_.Acquire({ playerPos_.x, kHeight, playerPos_.y });
    if (!obj) {
        return; // 満杯：出さない
    }
    const float bulletScale = deadEye ? kBulletScale * 1.3f : kBulletScale;
    obj->GetObject3d().GetTransform().Scale = { bulletScale, bulletScale, bulletScale };
    if (deadEye) {
        SetTint(*obj, 1.0f, 0.3f, 0.2f); // デッドアイの弾は赤い
    }
    else {
        SetTint(*obj, 1.0f, 0.9f, 0.3f);
    }

    Projectile b;
    b.obj = obj;
    b.pos = playerPos_;
    b.vel = { dir.x * speed, dir.y * speed };
    b.life = deadEye ? 0.6f : kBulletLife;
    b.deadEye = deadEye;
    playerBullets_.push_back(b);

    // 撃つ演出：銃口の閃光、飛び出す薬莢、わずかな画面の揺れ
    const Vector3 muzzle = { playerPos_.x + dir.x * 0.9f, kHeight, playerPos_.y + dir.y * 0.9f };
    dust_.EmitBurst(muzzle, { 0.0f, 0.0f, 0.0f }, 1, 0.0f, 0.0f, 0.6f, 0.06f, 1.0f, 0.95f, 0.7f, 0.0f, 1.0f, true);
    dust_.EmitBurst(muzzle, { dir.x, 0.05f, dir.y }, 2, 6.0f, 0.4f, 0.24f, 0.1f, 1.0f, 0.8f, 0.3f, 0.0f, 5.0f, true);
    dust_.EmitBurst({ playerPos_.x, kHeight + 0.2f, playerPos_.y }, { dir.y, 1.2f, -dir.x }, 1, 3.5f, 0.25f, 0.09f, 0.7f, 0.9f, 0.7f, 0.2f, 16.0f, 0.3f, false);
    fx_.Shake(0.03f, 0.05f);
}

bool WesternShooterScene::SpawnEnemy(size_t spawnIndex)
{
    const MapData::Spawn& spawn = map_.spawns[spawnIndex];
    const EnemyType type = PickEnemyType();
    const Vector2 door = { spawn.x + 0.5f, spawn.z + 0.5f }; // ドアのマスの真ん中から出る
    lastDoorPos_ = door;

    GameObject* obj = PoolOf(type).Acquire({ door.x, kHeight, door.y });
    if (!obj) {
        return false; // 満杯：今回は出さない（呼び出し側があとでもう一度試す）
    }
    Enemy e;
    e.id = nextEnemyId_++;
    e.type = type;
    e.obj = obj;
    e.pos = door;
    e.hunting = true;                                         // ドアから出てくる敵は、自機の居場所を知っている
    e.lastSeen = playerPos_;
    e.repathTimer = static_cast<float>(std::rand() % 100) * 0.004f; // 道の引き直しが同じフレームに集中しないように
    e.strafeSign = (std::rand() % 2 == 0) ? 1.0f : -1.0f;
    e.flankAngle = (0.6f + 0.6f * static_cast<float>(std::rand() % 100) * 0.01f) * e.strafeSign; // 左右どちらかへ回り込む
    if (type == EnemyType::Brute) {
        e.hp = kBruteHp;
        obj->GetObject3d().GetTransform().Scale = { kBruteScale, kBruteScale, kBruteScale };
        obj->GetObject3d().PlayAnimation("Idle", true, 0.0f); // 前に使われたときの姿勢（倒れた姿など）を残さない
        SetTint(*obj, 0.9f, 0.3f, 0.3f);
    }
    else {
        e.hp = kGunmanHp;
        e.fireTimer = 0.5f; // 出てすぐには撃たない
        obj->GetObject3d().GetTransform().Scale = { kGunmanScale, kGunmanScale, kGunmanScale };
        obj->GetObject3d().PlayAnimation("Idle", true, 0.0f);
        SetTint(*obj, 0.3f, 0.5f, 0.9f);
    }
    // 登場：ドアから自機の方へ大きく飛び出す。着地点は、自機のすぐ手前までで壁を避ける
    {
        const float toPlayer = Length2D({ playerPos_.x - door.x, playerPos_.y - door.y });
        const Vector2 dir = Normalized({ playerPos_.x - door.x, playerPos_.y - door.y }, { 0.0f, -1.0f });
        float d = (std::min)(kSpawnLeapDistance, (std::max)(1.5f, toPlayer - 2.5f));
        Vector2 to = door;
        for (; d >= 1.5f; d -= 0.5f) {
            const Vector2 cand = { door.x + dir.x * d, door.y + dir.y * d };
            if (!IsSolidAt(cand)) {
                to = cand;
                break;
            }
        }
        // 集団で飛び出しても重ならないように、着地点を左右と前後にずらす
        const Vector2 perp = { -dir.y, dir.x };
        const float lateral = (static_cast<float>(e.id % 3) - 1.0f) * 1.5f;
        const float depth = static_cast<float>(e.id % 2) * 1.0f;
        to = { to.x + perp.x * lateral - dir.x * depth, to.y + perp.y * lateral - dir.y * depth };
        PushOutOfWalls(to, RadiusOf(type));
        e.spawnFrom = door;
        e.spawnTo = to;
        e.spawnDur = e.spawnT = kSpawnFlightTime;
        e.flightPeak = kSpawnPeak;
    }
    enemies_.push_back(e);

    // 出現の演出：ドアが勢いよく開き、木の破片と砂ぼこりが飛び散り、画面が揺れる
    {
        // 建物の向こうからでも見えるように、ドアに光の柱を立てる（ごろつき=橙 / ガンマン=青）
        Pillar p;
        p.pos = door;
        p.scale = 0.7f;
        if (type == EnemyType::Brute) { p.r = 1.0f; p.g = 0.4f; p.b = 0.15f; }
        else { p.r = 0.3f; p.g = 0.6f; p.b = 1.0f; }
        pillars_.push_back(p);
    }
    doors_.Open(spawnIndex);
    dust_.Emit({ door.x, 0.15f, door.y - 0.3f }, 12, 3.5f, 0.55f, 0.8f, 0.82f, 0.72f, 0.52f);
    dust_.EmitBurst({ door.x, kHeight, door.y }, { 0.0f, 0.6f, -1.0f }, 8, 9.0f, 0.8f, 0.2f, 0.7f, 0.55f, 0.36f, 0.18f, 14.0f, 1.0f, false); // 木の破片
    fx_.Shake(0.14f, 0.18f);
    return true;
}

bool WesternShooterScene::SpawnBoss()
{
    GameObject* obj = bossPool_.Acquire({ lastDoorPos_.x, kHeight, lastDoorPos_.y });
    if (!obj) {
        return false;
    }
    Enemy e;
    e.id = nextEnemyId_++;
    e.type = EnemyType::Boss;
    e.obj = obj;
    e.pos = lastDoorPos_;
    e.hp = kBossHp;
    e.bossState = BossState::Intro;
    e.bossTimer = kBossIntroTime;
    e.strafeSign = (std::rand() % 2 == 0) ? 1.0f : -1.0f;
    obj->GetObject3d().GetTransform().Scale = { kBossScale, kBossScale, kBossScale };
    obj->GetObject3d().PlayAnimation("Idle", true, 0.0f);
    {
        const float toPlayer = Length2D({ playerPos_.x - lastDoorPos_.x, playerPos_.y - lastDoorPos_.y });
        const Vector2 dir = Normalized({ playerPos_.x - lastDoorPos_.x, playerPos_.y - lastDoorPos_.y }, { 0.0f, -1.0f });
        float d = (std::min)(kBossLeapDistance, (std::max)(2.0f, toPlayer - 5.0f)); // ボスは自機から間合いを空けて着地する
        Vector2 to = lastDoorPos_;
        for (; d >= 2.0f; d -= 0.5f) {
            const Vector2 cand = { lastDoorPos_.x + dir.x * d, lastDoorPos_.y + dir.y * d };
            std::vector<Vector2> path;
            if (!IsSolidAt(cand) && GridNav::FindPath(map_, cand, playerPos_, kBossRadius, path)) {
                to = cand; // 壁に囲まれた場所に着地しない（自機へ道が通じている場所だけ）
                break;
            }
        }
        PushOutOfWalls(to, kBossRadius);
        e.spawnFrom = lastDoorPos_;
        e.spawnTo = to;
        e.spawnDur = e.spawnT = kBossFlightTime;
        e.flightPeak = kBossPeak;
    }
    enemies_.push_back(e);
    bossSpawned_ = true;

    // 登場の演出：画面が大きく揺れ、砂ぼこりが立つ
    fx_.Shake(0.35f, 0.5f);
    dust_.Emit({ lastDoorPos_.x, 0.15f, lastDoorPos_.y - 0.3f }, 14, 3.5f, 0.6f, 1.0f, 0.82f, 0.72f, 0.52f);
    return true;
}

// ボスの弾（向きを指定して1発）
void WesternShooterScene::FireBossBullet(const Vector2& from, const Vector2& dir)
{
    GameObject* obj = enemyBulletPool_.Acquire({ from.x, kHeight, from.y });
    if (!obj) {
        return; // 満杯：撃たない
    }
    obj->GetObject3d().GetTransform().Scale = { kEnemyBulletScale * 1.4f, kEnemyBulletScale * 1.4f, kEnemyBulletScale * 1.4f };
    SetTint(*obj, 1.0f, 0.15f, 0.7f);
    Projectile b;
    b.obj = obj;
    b.pos = from;
    b.vel = { dir.x * kBossBulletSpeed, dir.y * kBossBulletSpeed };
    b.life = kEnemyBulletLife;
    enemyBullets_.push_back(b);
    dust_.EmitBurst({ from.x + dir.x * 1.2f, kHeight, from.y + dir.y * 1.2f }, { dir.x, 0.05f, dir.y }, 1, 5.0f, 0.4f, 0.3f, 0.1f, 1.0f, 0.4f, 0.6f, 0.0f, 5.0f, true);
}

void WesternShooterScene::FireEnemyBullet(const Enemy& enemy)
{
    GameObject* obj = enemyBulletPool_.Acquire({ enemy.pos.x, kHeight, enemy.pos.y });
    if (!obj) {
        return;
    }
    obj->GetObject3d().GetTransform().Scale = { kEnemyBulletScale, kEnemyBulletScale, kEnemyBulletScale };
    SetTint(*obj, 1.0f, 0.35f, 0.05f);

    // 狙いの線が固定されていたら、その向きへ撃つ（自機が動いていれば外れる）
    const Vector2 dir = enemy.aimLocked ? enemy.aimDir : Normalized({ playerPos_.x - enemy.pos.x, playerPos_.y - enemy.pos.y }, { 0.0f, -1.0f });
    Projectile b;
    b.obj = obj;
    b.pos = enemy.pos;
    b.vel = { dir.x * kEnemyBulletSpeed, dir.y * kEnemyBulletSpeed };
    b.life = kEnemyBulletLife;
    enemyBullets_.push_back(b);

    // 敵の銃口の閃光（撃たれる前の合図にもなる）
    dust_.EmitBurst({ enemy.pos.x + dir.x * 0.8f, kHeight, enemy.pos.y + dir.y * 0.8f }, { dir.x, 0.05f, dir.y }, 1, 5.0f, 0.4f, 0.22f, 0.09f, 1.0f, 0.55f, 0.15f, 0.0f, 5.0f, true);
}

// ---------------------------------------------------------------- ダメージ・返す

void WesternShooterScene::DamagePlayer()
{
    if (invincible_ > 0.0f || rollTimer_ > 0.0f || state_ != State::Playing) {
        return;
    }
    --playerHp_;
    invincible_ = kInvincibleTime;
    hurt_ = 1.0f;
    player_.GetObject3d().PlayAnimation("Hit", false, 0.04f); // のけぞる
    playerAnimLock_ = 0.4f;
    fx_.Shake(0.35f, 0.3f);
    fx_.HitStop(3);
    EmitBlood(playerPos_, { 0.0f, 0.0f }, 6); // 撃たれた：赤い粒が散る
}

void WesternShooterScene::KillEnemy(Enemy& enemy, const Vector2& hitDir)
{
    // 最後の1体か：ほかの敵がいなくて、いまの区切りの出現も残っていない
    bool last = true;
    for (const Enemy& other : enemies_) {
        if (other.obj && &other != &enemy) {
            last = false;
        }
    }
    for (size_t i = 0; i < map_.spawns.size() && i < spawnStates_.size(); ++i) {
        if (map_.spawns[i].area == currentArea_ && spawnStates_[i].remaining > 0) {
            last = false;
        }
    }

    // 倒れた敵は、撃たれた向きへ吹っ飛びながら回って消える（オブジェクトは Corpse が引き継ぐ）
    Corpse k;
    k.type = enemy.type;
    k.obj = enemy.obj;
    k.obj->GetObject3d().PlayAnimation("Death", false, 0.04f); // 後ろへ倒れる
    k.pos = enemy.pos;
    k.vel = { hitDir.x * kCorpseSpeed, hitDir.y * kCorpseSpeed };
    k.spinSign = (std::rand() % 2 == 0) ? 1.0f : -1.0f;
    corpses_.push_back(k);
    enemy.obj = nullptr; // 渡したあとは触らない
    ++score_;

    EmitBlood(enemy.pos, hitDir, 7);
    dust_.Emit({ enemy.pos.x, 0.2f, enemy.pos.y }, 3, 2.6f, 0.45f, 0.5f, 0.82f, 0.72f, 0.52f);
    fx_.Shake(0.16f, 0.16f);
    fx_.HitStop(3);

    if (last) {
        // 区切りの最後の1体：長めに止まってから、ゆっくり倒れていく
        fx_.Shake(0.4f, 0.4f, 20.0f);
        fx_.HitStop(7);
        slowTimer_ = kSlowTime;
    }
}

// 倒れた敵：弧を描いて飛び、回りながら縮んで消える
void WesternShooterScene::UpdateCorpses(float dt)
{
    for (size_t i = 0; i < corpses_.size();) {
        Corpse& k = corpses_[i];
        k.age += dt;
        if (k.age >= kCorpseLife) {
            PoolOf(k.type).Release(k.obj);
            corpses_[i] = corpses_.back();
            corpses_.pop_back();
            continue;
        }
        const float t = k.age / kCorpseLife;
        const float drag = std::exp(-4.0f * dt);
        k.vel.x *= drag;
        k.vel.y *= drag;
        Vector2 next = { k.pos.x + k.vel.x * dt, k.pos.y + k.vel.y * dt };
        if (IsSolidAt(next)) {
            k.vel = { 0.0f, 0.0f }; // 壁に当たったら、そこで止まる
        }
        else {
            k.pos = next;
        }
        const float scaleBase = (k.type == EnemyType::Brute) ? kBruteScale : (k.type == EnemyType::Boss ? kBossScale : kGunmanScale);
        const float shrink = t < 0.5f ? 1.0f : 1.0f - (t - 0.5f) / 0.5f;
        Transform& tr = k.obj->GetObject3d().GetTransform();
        // 倒れるアニメーションを流しながら、撃たれた向きへ滑っていく（自機の方を向いたまま、後ろへ倒れる）
        tr.Translate = { k.pos.x, kHeight, k.pos.y };
        k.obj->GetObject3d().UpdateSkin(dt);
        const float s = scaleBase * shrink;
        tr.Scale = { s, s, s };
        SetTint(*k.obj, 1.0f, 0.5f + 0.5f * t, 0.5f + 0.5f * t);
        ++i;
    }
}

// 血しぶき（赤い粒）。dir が 0 なら全方向、あれば撃たれた向きに多く飛ぶ
void WesternShooterScene::EmitBlood(const Vector2& pos, const Vector2& dir, int count)
{
    dust_.EmitBurst({ pos.x, kHeight, pos.y }, { dir.x, 0.5f, dir.y }, count, 7.0f, 0.9f, 0.16f, 0.6f, 0.75f, 0.05f, 0.05f, 14.0f, 1.2f, false);
}

GameObjectPool& WesternShooterScene::PoolOf(EnemyType type)
{
    if (type == EnemyType::Boss) {
        return bossPool_;
    }
    return (type == EnemyType::Brute) ? brutePool_ : gunmanPool_;
}

// ---------------------------------------------------------------- 登場演出（カットシーン）
//   フェーズの始まり: カメラがドアへ寄る → 敵が集団で次々に飛び出す → 着地で色つきの光の柱と煙（戦隊モノ風）→ カメラが自機へ戻る
//   ボス          : 暗くなって黒帯 → ボスが高く飛び出す（カメラが追う）→ 着地で閃光 → 低い位置から回り込み WANTED / 名前 / 賞金 → 戻る
//   どちらも Enter / Space / Esc / クリックでスキップできる。演出の間、ゲームの時間は止まる

namespace {
    float Smooth01(float x)
    {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    constexpr float kLeapAt = 1.0f;        // フェーズ演出：飛び出し始める時刻（それまではカメラが寄る）
    constexpr float kLeapStagger = 0.0f;   // 飛び出す間隔（0 = 全員が同時）
    constexpr float kPhaseReturn = 0.7f;   // カメラが自機へ戻る時間
    constexpr float kFormColors[5][3] = {  // 整列の色（左から：赤・青・黄・緑・桃）
        { 1.0f, 0.15f, 0.15f }, { 0.2f, 0.45f, 1.0f }, { 1.0f, 0.85f, 0.1f }, { 0.2f, 0.9f, 0.3f }, { 1.0f, 0.4f, 0.75f } };

    // ポーズ：左から順に違う決めポーズ。ドンと大きくなってから落ち着く
    void ApplyPose(Transform& tr, int index, float t, float baseScale, float yaw)
    {
        const float punch = 1.0f + 0.4f * std::exp(-t * 7.0f);
        float sx = baseScale * punch, sy = baseScale * punch, sz = baseScale * punch;
        tr.Rotate = { 0.0f, yaw, 0.0f }; // yaw = 自機の方を向く角度
        switch (index % 5) {
        case 0: tr.Rotate.z = 0.6f * (1.0f - std::exp(-t * 10.0f)); break;                       // 左へ傾く
        case 1: tr.Rotate.z = -0.6f * (1.0f - std::exp(-t * 10.0f)); break;                      // 右へ傾く
        case 2: tr.Rotate.y = yaw + 6.2831853f * Smooth01(t / 0.6f); break;                           // くるっと回る
        case 3: sy *= 0.75f; sx *= 1.15f; sz *= 1.15f; break;                                    // 低く構える
        default: tr.Translate.y += 1.4f * std::fabs(std::sin(t * 6.0f)) * std::exp(-t * 1.5f); break; // ぴょんと跳ねる
        }
        tr.Scale = { sx, sy, sz };
    }
    constexpr float kBossSpawnAt = 0.9f;   // ボス演出：ボスが飛び出す時刻
    constexpr float kBossOrbit = 2.8f;     // 着地してから、回り込んで名前を見せる時間
    constexpr float kBossReturn = 0.9f;
    constexpr float kBarHeight = 84.0f;    // 黒帯の厚み（ゲーム画面のピクセル）
}

int WesternShooterScene::PhaseKind() const
{
    // 区切りが1つしかないマップは、最初から両方。それ以外は 1番目=ごろつき 2番目=ガンマン 3番目以降=両方
    if (areaOrder_.size() <= 1) {
        return 2;
    }
    return phaseIndex_ >= 2 ? 2 : phaseIndex_;
}

WesternShooterScene::EnemyType WesternShooterScene::PickEnemyType() const
{
    const int kind = PhaseKind();
    if (kind == 0) {
        return EnemyType::Brute;
    }
    if (kind == 1) {
        return EnemyType::Gunman;
    }
    return (std::rand() % 2 == 0) ? EnemyType::Brute : EnemyType::Gunman;
}

void WesternShooterScene::StartPhaseCutscene()
{
    Cutscene c;
    c.kind = CutsceneKind::Phase;
    c.phase = phaseIndex_ + 1;
    c.mix = PhaseKind();
    c.startFocus = cameraFocus_;
    // この区切りのドアを、左から右へ。1つのドアから最大 kEnemiesPerDoor 体ずつ
    std::vector<size_t> doors;
    for (size_t i = 0; i < map_.spawns.size() && i < spawnStates_.size(); ++i) {
        if (map_.spawns[i].area == currentArea_ && spawnStates_[i].remaining > 0) {
            doors.push_back(i);
        }
    }
    std::sort(doors.begin(), doors.end(), [&](size_t a, size_t b) { return map_.spawns[a].x < map_.spawns[b].x; });
    for (int round = 0; round < 16 && c.doors.size() < kFormationCount; ++round) {
        for (size_t di : doors) {
            if (spawnStates_[di].remaining > round && c.doors.size() < kFormationCount) {
                c.doors.push_back(di);
            }
        }
    }
    std::sort(c.doors.begin(), c.doors.end(), [&](size_t a, size_t b) { return map_.spawns[a].x < map_.spawns[b].x; });
    if (c.doors.empty()) {
        return; // 出す敵がいない：演出なし
    }
    // カメラは、敵が着地する側（自機のいる側）に置いて、ドアのある壁を正面から見る。ドアは建物の街路側の壁にあるので、
    // ふだんのカメラ（南から見下ろす）だと建物の陰になって見えない
    const MapData::Spawn& first = map_.spawns[c.doors.front()];
    const Vector2 leap = Normalized({ playerPos_.x - (first.x + 0.5f), playerPos_.y - (first.z + 0.5f) }, { 0.0f, -1.0f });
    c.yaw0 = std::atan2(leap.x, -leap.y);

    // 整列する位置：ドアの前（自機の側）に、横一列（両端が少し奥へ下がるV字）で並ぶ
    const size_t n = c.doors.size();
    Vector2 doorsC{ 0.0f, 0.0f };
    for (size_t di : c.doors) {
        doorsC.x += map_.spawns[di].x + 0.5f;
        doorsC.y += map_.spawns[di].z + 0.5f;
    }
    doorsC.x /= static_cast<float>(n);
    doorsC.y /= static_cast<float>(n);
    Vector2 center = doorsC;
    for (float d = kFormationDistance; d >= 2.0f; d -= 0.5f) {
        const Vector2 cand = { doorsC.x + leap.x * d, doorsC.y + leap.y * d };
        if (!IsSolidAt(cand)) {
            center = cand;
            break;
        }
    }
    // 街路の両側（北と南）のドアから出るときは、街路の真ん中に横一列で並ぶ。カメラも真上に近い角度から、両側をまとめて映す
    float zMin = 1e9f, zMax = -1e9f;
    for (size_t di : c.doors) {
        zMin = (std::min)(zMin, map_.spawns[di].z + 0.5f);
        zMax = (std::max)(zMax, map_.spawns[di].z + 0.5f);
    }
    float xMin = 1e9f, xMax = -1e9f;
    for (size_t di : c.doors) {
        xMin = (std::min)(xMin, map_.spawns[di].x + 0.5f);
        xMax = (std::max)(xMax, map_.spawns[di].x + 0.5f);
    }
    c.both = (zMax - zMin) >= 8.0f && (xMax - xMin) > (zMax - zMin); // 広場のように横長に並ぶドアだけ（縦に並ぶ奥の通路は除く）
    if (c.both) {
        c.yaw0 = 0.0f;
        const float midZ = (zMin + zMax) * 0.5f;
        for (size_t i = 0; i < n; ++i) {
            Vector2 p = { doorsC.x + (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * kFormationSpacing, midZ };
            PushOutOfWalls(p, kBruteRadius);
            c.slots.push_back(p);
        }
        cutscene_ = c;
        pillars_.clear();
        return;
    }
    const Vector2 perp = { -leap.y, leap.x };
    for (size_t i = 0; i < n; ++i) {
        const float off = (static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * kFormationSpacing;
        Vector2 p = { center.x + perp.x * off - leap.x * std::fabs(off) * 0.35f, center.y + perp.y * off - leap.y * std::fabs(off) * 0.35f };
        PushOutOfWalls(p, kBruteRadius);
        c.slots.push_back(p);
    }
    // 画面の左から右（x の小さい順）に並べる。ドアも x の小さい順なので、順番に対応する
    std::sort(c.slots.begin(), c.slots.end(), [](const Vector2& a, const Vector2& b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    cutscene_ = c;
    pillars_.clear();
}

void WesternShooterScene::StartBossCutscene()
{
    Cutscene c;
    c.kind = CutsceneKind::Boss;
    c.startFocus = cameraFocus_;
    const Vector2 leap = Normalized({ playerPos_.x - lastDoorPos_.x, playerPos_.y - lastDoorPos_.y }, { 0.0f, -1.0f });
    c.yaw0 = std::atan2(leap.x, -leap.y); // 着地する側から、ボスが出てくるドアを正面に見る
    cutscene_ = c;
    pillars_.clear();
    bossSpawned_ = true; // 出すのは演出の途中。ここで立てておけば、倒したときにクリアへ進む
}

void WesternShooterScene::EndCutscene()
{
    // ポーズを解いて、ふつうの姿に戻す
    for (Enemy& e : enemies_) {
        if (!e.obj || e.poseIndex < 0) {
            continue;
        }
        const float base = (e.type == EnemyType::Brute) ? kBruteScale : kGunmanScale;
        Transform& tr = e.obj->GetObject3d().GetTransform();
        tr.Rotate = { 0.0f, 0.0f, 0.0f };
        tr.Scale = { base, base, base };
        e.poseIndex = -1;
        e.posed = false;
    }
    cutscene_ = Cutscene{};
    shotHeight_ = kCameraHeight;
    shotBack_ = kCameraBack;
    shotYaw_ = 0.0f;
    shotLookY_ = 0.0f;
}

void WesternShooterScene::UpdateCutscene()
{
    bool skip = ctx_->input->TriggerKey(DIK_RETURN) || ctx_->input->TriggerKey(DIK_SPACE)
        || ctx_->input->TriggerKey(DIK_ESCAPE) || ctx_->input->TriggerGamepadButton(0);
#ifdef USE_IMGUI
    skip = skip || EditorLayout::GetViewportInput().clicked;
#endif // USE_IMGUI
    if (skip) {
        // スキップ：最後まで一気に進める（ヒットストップは無視）
        for (int i = 0; i < 2000 && cutscene_.kind != CutsceneKind::None; ++i) {
            StepCutscene(kDt);
        }
        pillars_.clear();
        return;
    }
    StepCutscene(fx_.IsHitStop() ? 0.0f : kDt);
}

void WesternShooterScene::StepCutscene(float dt)
{
    Cutscene& c = cutscene_;
    const bool boss = c.kind == CutsceneKind::Boss;
    c.t += dt;
    c.flash = (std::max)(0.0f, c.flash - dt * 2.0f);
    // 1. 敵を出す
    if (!boss) {
        while (c.nextDoor < c.doors.size() && c.t >= kLeapAt + kLeapStagger * static_cast<float>(c.nextDoor)) {
            const size_t di = c.doors[c.nextDoor++];
            if (SpawnEnemy(di)) {
                --spawnStates_[di].remaining;
                spawnStates_[di].timer = map_.spawns[di].interval + 1.0f; // 残りは演出のあと、ふつうに出てくる
                // 整列：決まった位置へ、全員が同じ時間で飛んで着地する（同時に着地）
                const size_t slot = c.nextDoor - 1;
                Enemy& ne = enemies_.back();
                if (slot < c.slots.size()) {
                    ne.spawnTo = c.slots[slot];
                    ne.spawnDur = ne.spawnT = kFormationFlight;
                    ne.flightPeak = kSpawnPeak;
                    ne.poseIndex = static_cast<int>(slot);
                }
            }
        }
    }
    else if (!c.bossSpawned && c.t >= kBossSpawnAt) {
        c.bossSpawned = true;
        if (!SpawnBoss()) {
            EndCutscene();
            return;
        }
    }

    // 2. 飛んでいる敵を進め、見た目を合わせる（演出の間は、ふだんの敵の更新が動かないので、ここでやる）
    bool air = false;
    Vector2 sum{ 0.0f, 0.0f };
    int count = 0;
    const Enemy* bossEnemy = nullptr;
    for (Enemy& e : enemies_) {
        if (!e.obj) {
            continue;
        }
        if (e.spawnT > 0.0f) {
            UpdateSpawnFlight(e, dt);
        }
        if (e.spawnT > 0.0f) {
            air = true;
        }
        Place(*e.obj, e.pos);
        e.obj->GetObject3d().GetTransform().Rotate.y = std::atan2(playerPos_.x - e.pos.x, playerPos_.y - e.pos.y); // 人型は、自機の方を向く
        e.obj->GetObject3d().PlayAnimation(e.spawnT > 0.0f ? "Roll" : "Idle", true, 0.1f); // 飛んでいる間は体を丸め、着地したら立つ
        e.obj->GetObject3d().UpdateSkin(dt);
        if (e.spawnT > 0.0f) {
            e.obj->GetObject3d().GetTransform().Translate.y = kHeight + e.flightY;
        }
        // 整列のポーズ：着地のあと、左から順に決める
        if (e.poseIndex >= 0 && c.landedAt >= 0.0f) {
            if (!e.posed && c.t >= c.landedAt + kPoseStart + kPoseInterval * static_cast<float>(e.poseIndex)) {
                e.posed = true;
                e.poseT = 0.0f;
                const float* col = kFormColors[e.poseIndex % 5];
                Pillar p;
                p.pos = e.pos;
                p.r = col[0]; p.g = col[1]; p.b = col[2];
                pillars_.push_back(p);
                dust_.Emit({ e.pos.x, 0.3f, e.pos.y }, 12, 3.2f, 0.9f, 1.1f, col[0], col[1], col[2]);
                fx_.Shake(0.14f, 0.15f);
            }
            if (e.posed) {
                e.poseT += dt;
                const float base = (e.type == EnemyType::Brute) ? kBruteScale : kGunmanScale;
                ApplyPose(e.obj->GetObject3d().GetTransform(), e.poseIndex, e.poseT, base, std::atan2(playerPos_.x - e.pos.x, playerPos_.y - e.pos.y));
            }
        }
        TintEnemy(e);
        sum.x += e.pos.x;
        sum.y += e.pos.y;
        ++count;
        if (e.type == EnemyType::Boss) {
            bossEnemy = &e;
        }
    }

    // 3. 全員が着地した時刻を覚える
    const bool allSpawned = boss ? c.bossSpawned : c.nextDoor >= c.doors.size();
    if (allSpawned && !air && c.landedAt < 0.0f && c.t > (boss ? kBossSpawnAt : kLeapAt)) {
        c.landedAt = c.t;
        c.leaveAt = c.t + (boss ? kBossOrbit : kPoseStart + kPoseInterval * static_cast<float>(c.doors.size()) + kPoseHold);
        c.endAt = c.leaveAt + (boss ? kBossReturn : kPhaseReturn);
    }

    // 4. カメラ
    Vector2 doorsC = lastDoorPos_;
    if (!boss) {
        doorsC = { 0.0f, 0.0f };
        for (size_t di : c.doors) {
            doorsC.x += map_.spawns[di].x + 0.5f;
            doorsC.y += map_.spawns[di].z + 0.5f;
        }
        doorsC.x /= static_cast<float>(c.doors.size());
        doorsC.y /= static_cast<float>(c.doors.size());
    }
    const Vector2 groupC = count > 0 ? Vector2{ sum.x / static_cast<float>(count), sum.y / static_cast<float>(count) } : doorsC;
    const Vector2 bossPos = bossEnemy ? bossEnemy->pos : doorsC;
    const float bossAir = bossEnemy ? bossEnemy->flightY : 0.0f;

    Vector2 focus = cameraFocus_;
    float height = kCameraHeight;
    float back = kCameraBack;
    float yaw = 0.0f;
    float lookY = 0.0f;
    const bool leaving = c.leaveAt >= 0.0f && c.t >= c.leaveAt;
    if (!leaving) {
        if (!boss) {
            const Vector2 firstDoor = { map_.spawns[c.doors.front()].x + 0.5f, map_.spawns[c.doors.front()].z + 0.5f };
            if (c.t < kLeapAt) {
                // 最初のドアへ寄りながら、敵が着地する側へ回り込む
                const float k = Smooth01(c.t / kLeapAt);
                focus = { Lerp(c.startFocus.x, firstDoor.x, k), Lerp(c.startFocus.y, firstDoor.y, k) };
                height = Lerp(kCameraHeight, c.both ? 14.0f : 11.0f, k);
                back = Lerp(kCameraBack, c.both ? 4.0f : 8.0f, k);
                yaw = Lerp(0.0f, c.yaw0, k);
            }
            else {
                // 飛び出すドアを順に追い（左から右へ）、全員が飛び出したら集団を追う。ゆっくり左右に振りながら寄っていく
                Vector2 target = firstDoor;
                if (c.nextDoor > 0 && c.nextDoor < c.doors.size()) {
                    const MapData::Spawn& cur = map_.spawns[c.doors[c.nextDoor - 1]];
                    target = { cur.x + 0.5f, cur.z + 0.5f };
                    if (c.both && !c.slots.empty()) {
                        target.y = c.slots.front().y; // 両側のときは、街路の真ん中を映す
                    }
                }
                else if (c.nextDoor >= c.doors.size()) {
                    target = groupC;
                    if (c.landedAt >= 0.0f) {
                        // ポーズを決めている人へ、カメラが少し寄る
                        const int k = std::clamp(static_cast<int>((c.t - c.landedAt - kPoseStart) / kPoseInterval), 0, static_cast<int>(c.doors.size()) - 1);
                        for (const Enemy& e : enemies_) {
                            if (e.obj && e.poseIndex == k) {
                                target = { groupC.x * 0.5f + e.pos.x * 0.5f, groupC.y * 0.5f + e.pos.y * 0.5f };
                            }
                        }
                    }
                }
                const float follow = 1.0f - std::exp(-6.0f * dt);
                focus = { cameraFocus_.x + (target.x - cameraFocus_.x) * follow, cameraFocus_.y + (target.y - cameraFocus_.y) * follow };
                height = Lerp(c.both ? 14.0f : 11.0f, c.both ? 11.0f : 8.5f, Smooth01((c.t - kLeapAt) / 2.0f));
                back = Lerp(c.both ? 4.0f : 8.0f, c.both ? 4.5f : 6.5f, Smooth01((c.t - kLeapAt) / 2.0f));
                yaw = c.yaw0 + 0.35f * std::sin((c.t - kLeapAt) * 1.5f);
            }
        }
        else if (c.t < kBossSpawnAt) {
            // 暗くなりながら、ボスが出てくるドアへ
            const float k = Smooth01(c.t / kBossSpawnAt);
            focus = { Lerp(c.startFocus.x, doorsC.x, k), Lerp(c.startFocus.y, doorsC.y, k) };
            height = Lerp(kCameraHeight, 9.0f, k);
            back = Lerp(kCameraBack, 9.0f, k);
            yaw = Lerp(0.0f, c.yaw0, k);
        }
        else if (c.landedAt < 0.0f) {
            // 飛んでいるボスを追う（高く上がるほどカメラも上がる）
            focus = bossPos;
            height = 9.0f + bossAir;
            back = 9.0f;
            yaw = c.yaw0;
            lookY = bossAir * 0.6f;
        }
        else {
            // 着地のあと：低い位置から、ボスのまわりを回り込みながら寄る
            const float u = std::clamp((c.t - c.landedAt) / kBossOrbit, 0.0f, 1.0f);
            focus = bossPos;
            yaw = c.yaw0 + Lerp(0.0f, 2.4f, Smooth01(u));
            height = Lerp(5.4f, 4.2f, u);
            back = Lerp(8.5f, 6.0f, u);
            lookY = 1.3f;
        }
        c.holdFocus = focus;
        c.holdHeight = height;
        c.holdBack = back;
        c.holdYaw = yaw;
        c.holdLookY = lookY;
    }
    else {
        // 自機へ戻る
        const float k = Smooth01((c.t - c.leaveAt) / (boss ? kBossReturn : kPhaseReturn));
        focus = { Lerp(c.holdFocus.x, playerPos_.x, k), Lerp(c.holdFocus.y, playerPos_.y, k) };
        height = Lerp(c.holdHeight, kCameraHeight, k);
        back = Lerp(c.holdBack, kCameraBack, k);
        float hy = c.holdYaw; // 戻るときは、ぐるぐる回らず近い向きから戻る
        while (hy > 3.14159265f) { hy -= 6.2831853f; }
        while (hy < -3.14159265f) { hy += 6.2831853f; }
        yaw = Lerp(hy, 0.0f, k);
        lookY = Lerp(c.holdLookY, 0.0f, k);
    }
    cameraFocus_ = focus;
    shotHeight_ = height;
    shotBack_ = back;
    shotYaw_ = yaw;
    shotLookY_ = lookY;

    if (c.endAt >= 0.0f && c.t >= c.endAt) {
        EndCutscene();
    }
}

// 帯・文字・フラッシュ（ゲーム画面の上に重ねる）
void WesternShooterScene::DrawCutsceneOverlay()
{
#ifdef USE_IMGUI
    const Cutscene& c = cutscene_;
    if (c.kind == CutsceneKind::None) {
        return;
    }
    const bool boss = c.kind == CutsceneKind::Boss;
    const float W = static_cast<float>(WinApp::kGameWidth);
    const float H = static_cast<float>(WinApp::kGameHeight);
    const float outLen = boss ? kBossReturn : kPhaseReturn;
    const float env = Smooth01(c.t / 0.5f) * (c.leaveAt >= 0.0f ? 1.0f - Smooth01((c.t - c.leaveAt) / outLen) : 1.0f);

    if (boss) {
        EditorLayout::DrawViewportDim(0.4f * env); // ボス：画面を少し暗くして、ボスだけを浮かび上がらせる
    }
    const float bar = kBarHeight * env;
    if (bar > 1.0f) {
        EditorLayout::DrawViewportBar({ W * 0.5f, bar * 0.5f }, { W + 8.0f, bar }, 1.0f, IM_COL32(0, 0, 0, 255));
        EditorLayout::DrawViewportBar({ W * 0.5f, H - bar * 0.5f }, { W + 8.0f, bar }, 1.0f, IM_COL32(0, 0, 0, 255));
    }
    if (c.flash > 0.0f) {
        EditorLayout::DrawViewportTint(255, 255, 255, c.flash * 0.9f);
    }

    auto alphaOf = [&](float appear) { return static_cast<int>(255.0f * (std::min)(Smooth01(appear), env)); };
    if (!boss) {
        // PHASE n / 敵の組み合わせ
        const float fadeOut = 1.0f - Smooth01((c.t - 1.9f) / 0.4f);
        const float a1 = (std::min)(Smooth01((c.t - 0.15f) / 0.3f), fadeOut);
        const float a2 = (std::min)(Smooth01((c.t - 0.35f) / 0.3f), fadeOut);
        const char* sub = c.mix == 0 ? "BRUTES" : (c.mix == 1 ? "GUNMEN" : "ALL OUT!");
        char title[32];
        std::snprintf(title, sizeof(title), "PHASE %d", c.phase);
        EditorLayout::DrawViewportText({ W * 0.5f, H * 0.3f - 40.0f * (1.0f - Smooth01((c.t - 0.15f) / 0.3f)) }, title, 120.0f, IM_COL32(240, 190, 90, static_cast<int>(255.0f * a1)));
        EditorLayout::DrawViewportText({ W * 0.5f, H * 0.3f + 95.0f }, sub, 56.0f, IM_COL32(255, 245, 220, static_cast<int>(255.0f * a2)));
    }
    else if (c.landedAt >= 0.0f) {
        // WANTED（ドンと出る）→ 名前 → 賞金
        const float u = c.t - c.landedAt;
        const float punch = 1.0f + 0.7f * (1.0f - Smooth01((u - 0.5f) / 0.25f));
        EditorLayout::DrawViewportText({ W * 0.5f, H * 0.24f }, "WANTED", 130.0f * punch, IM_COL32(240, 190, 90, alphaOf((u - 0.5f) / 0.12f)));
        EditorLayout::DrawViewportText({ W * 0.5f, H * 0.24f + 125.0f }, "IRON JACK", 96.0f, IM_COL32(255, 238, 170, alphaOf((u - 1.2f) / 0.3f)));
        EditorLayout::DrawViewportText({ W * 0.5f, H * 0.24f + 205.0f }, "BOUNTY  $5,000", 40.0f, IM_COL32(255, 255, 255, alphaOf((u - 1.9f) / 0.3f)));
    }
    EditorLayout::DrawViewportText({ W - 150.0f, H - 28.0f }, "ENTER: SKIP", 22.0f, IM_COL32(220, 220, 220, 150));
#endif // USE_IMGUI
}

// ---------------------------------------------------------------- ImGui

void WesternShooterScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: 西部シューティング");
    ImGui::Separator();
    ImGui::Text("移動 WASD / 矢印 / 左スティック");
    ImGui::Text("狙い マウス   撃つ マウス左 / J");
    ImGui::TextDisabled("ゲームパッドは右スティックで狙える（未調整）");
    ImGui::Separator();
    ImGui::TextWrapped("%s", mapMessage_.c_str());
    ImGui::Text("マップ: %d x %d  出現点 %zu  区切り %zu", map_.width, map_.height, map_.spawns.size(), map_.gates.size());
    ImGui::Text("FPS %.0f  マップの描画数 %zu", ImGui::GetIO().Framerate, mapView_.VisibleCount());
    ImGui::Text("処理時間(ms) 更新%.2f  マップ更新%.2f  描画の命令%.2f（うちマップ%.2f）", perfUpdateMs_, perfMapMs_, perfDrawMs_, perfMapDrawMs_);
    if (ImGui::CollapsingHeader("画質（影・光）")) {
        look_.DrawImGui();
    }
    if (currentArea_ == kNoArea) {
        ImGui::Text("区切り: なし（%s）", state_ == State::Cleared ? "全部倒した" : "出現点なし");
    }
    else {
        ImGui::Text("いまの区切り: %d", currentArea_);
    }
    ImGui::Separator();
    ImGui::Text("HP: %d / %d", playerHp_, kPlayerMaxHp);
    ImGui::Text("倒した数: %d", score_);
    ImGui::ProgressBar(deadEyeGauge_, ImVec2(-1.0f, 0.0f), "DEAD EYE");
    ImGui::TextDisabled("Shift / Q / LB 押しっぱなし: スロー → クリックでマーク → 離すと連射");
    ImGui::TextDisabled("Space / A: 転がって避ける（弾が当たらない）");
    if (deadEyeActive_) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "デッドアイ マーク %zu / %zu", marks_.size(), kMaxMarks);
    }
    // ボス戦：体力ゲージと名前。登場の間は WANTED
    DrawCutsceneOverlay();
    for (const Enemy& e : enemies_) {
        if (!e.obj || e.type != EnemyType::Boss || cutscene_.kind != CutsceneKind::None) {
            continue;
        }
        const float frac = static_cast<float>((std::max)(e.hp, 0)) / static_cast<float>(kBossHp);
        const bool recover = e.bossState == BossState::Recover;
        EditorLayout::DrawViewportText({ WinApp::kGameWidth * 0.5f, 52.0f }, "IRON JACK", 34.0f, IM_COL32(240, 190, 90, 255));
        EditorLayout::DrawViewportBar({ WinApp::kGameWidth * 0.5f, 88.0f }, { 640.0f, 18.0f }, frac,
            recover ? IM_COL32(255, 215, 60, 255) : IM_COL32(200, 50, 60, 255));
        if (recover) {
            EditorLayout::DrawViewportText({ WinApp::kGameWidth * 0.5f, 125.0f }, "NOW! (x2 DAMAGE)", 30.0f, IM_COL32(255, 235, 120, 255));
        }
    }
    if (state_ == State::GameOver) {
        EditorLayout::DrawViewportText({ WinApp::kGameWidth * 0.5f, WinApp::kGameHeight * 0.4f }, "YOU DIED", 110.0f, IM_COL32(220, 60, 50, 255));
    }
    else if (state_ == State::Cleared) {
        EditorLayout::DrawViewportText({ WinApp::kGameWidth * 0.5f, WinApp::kGameHeight * 0.4f }, "BOUNTY CLEARED", 100.0f, IM_COL32(240, 190, 90, 255));
    }
    if (state_ == State::GameOver) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "ゲームオーバー（Rキーでやり直し）");
    }
    else if (state_ == State::Cleared) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "クリア！（Rキーでやり直し）");
    }
    ImGui::Separator();
    ImGui::Checkbox("敵の行動を色で見せる", &showAiColors_);
    if (showAiColors_) {
        ImGui::TextDisabled("ごろつき 赤=近づく 明るい赤=突っ込む 橙=取り巻き");
        ImGui::TextDisabled("ガンマン 青=近づく 水色=撃つ 灰青=隠れる / 黄=探す");
    }
    ImGui::Text("敵: %zu  (ごろつき %zu/%zu  ガンマン %zu/%zu)", enemies_.size(), brutePool_.ActiveCount(), brutePool_.Capacity(), gunmanPool_.ActiveCount(), gunmanPool_.Capacity());
    ImGui::Text("自機の弾: %zu/%zu  敵の弾: %zu/%zu", playerBulletPool_.ActiveCount(), playerBulletPool_.Capacity(), enemyBulletPool_.ActiveCount(), enemyBulletPool_.Capacity());
    ImGui::TextDisabled("使用中の数と配列の数が合わなければ、返し忘れ");
    // ポーズ画面（暗くして項目を描く）。いちばん最後に重ねる
    pause_.Draw();
    if (freeCam_) {
        EditorLayout::DrawViewportText({ WinApp::kGameWidth * 0.5f, 40.0f }, "FREE CAMERA", 34.0f, IM_COL32(120, 220, 255, 255));
        EditorLayout::DrawViewportText({ WinApp::kGameWidth * 0.5f, WinApp::kGameHeight - 36.0f },
            "WASD move  Space/C up/down  Z fast X slow  Arrows or RMB drag look  ESC/P back", 22.0f, IM_COL32(200, 230, 240, 255));
    }
    ImGui::End();
#endif // USE_IMGUI
}

// ---------------------------------------------------------------- 使い回しの窓口

std::unique_ptr<WesternShooterScene>& WesternShooterEntry::Shared()
{
    static std::unique_ptr<WesternShooterScene> instance;
    return instance;
}

namespace {
    bool g_ready = false; // 読み込みが全部終わったか
    LoadQueue g_queue;    // 読み込みの段階（エンジンの部品）。登録は最初の PreloadStep / Preload で1回だけ

    // 読み込みの段階を登録する（シーンを作る → 初期化の各段階）
    void RegisterLoadSteps(SceneContext* context, std::unique_ptr<WesternShooterScene>& shared)
    {
        if (!g_queue.IsEmpty()) {
            return;
        }
        g_queue.Add("Create scene", [context, &shared] {
            shared = std::make_unique<WesternShooterScene>();
            shared->SetContext(context);
        });
        for (int i = 0; i < WesternShooterScene::kInitSteps; ++i) {
            g_queue.Add("Init", [&shared] { shared->InitStep(); });
        }
    }
}

bool WesternShooterEntry::PreloadStep(SceneContext* context)
{
    if (g_ready) {
        return true;
    }
    RegisterLoadSteps(context, Shared());
    if (g_queue.Tick(context->dxCommon)) {
        g_ready = true;
        Logger::Log("WesternShooter preloaded");
    }
    return g_ready;
}

bool WesternShooterEntry::IsReady()
{
    return g_ready;
}

float WesternShooterEntry::Progress()
{
    return g_ready ? 1.0f : g_queue.Progress();
}

void WesternShooterEntry::DrawLoading(float screenW, float screenH)
{
    if (!g_ready) {
        g_queue.DrawNowLoading(screenW, screenH);
    }
}

void WesternShooterEntry::Preload(SceneContext* context)
{
    if (g_ready) {
        return;
    }
    RegisterLoadSteps(context, Shared());
    g_queue.RunAll(context->dxCommon); // 待たずに全部読む
    g_ready = true;
    Logger::Log("WesternShooter preloaded");
}

void WesternShooterEntry::Release()
{
    Shared().reset();
    g_queue.Clear();
    g_ready = false;
}

void WesternShooterEntry::Initialize()
{
    if (!g_ready) {
        // まだ読み込んでいなければ（ロード画面を通っていないとき）、ここで一気に読む
        Preload(ctx_);
    }
    std::unique_ptr<WesternShooterScene>& s = Shared();
    s->SetContext(ctx_);
    s->Restart();
    s->BeginLook();
}

void WesternShooterEntry::Finalize()
{
    if (Shared()) {
        Shared()->EndLook();
    }
}

void WesternShooterEntry::Update() { Shared()->Update(); }
void WesternShooterEntry::Draw() { Shared()->Draw(); }
void WesternShooterEntry::DrawImGui() { Shared()->DrawImGui(); }
