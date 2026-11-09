#include "WesternShooterScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "base/WinApp.h"
#include "3d/Object3dCommon.h"
#include "input/Input.h"
#include "math/Collision.h"
#include "math/ScreenProjection.h"
#include "editor/EditorLayout.h"
#include "WesternShooterCommon.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

using namespace WesternShooter;

namespace {
    constexpr float kDt = 1.0f / 60.0f;

    constexpr const char* kMapPath = "resources/western_stage.csv";
    constexpr int kNoArea = 1 << 20;          // 区切りが無い・全部終わった、を表す番号（どの gate.area よりも大きい）

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
    constexpr float kPlayerSpeed = 6.0f;      // 1秒あたり
    constexpr int kPlayerMaxHp = 5;
    constexpr float kInvincibleTime = 1.0f;   // ダメージ後の無敵（秒）

    // --- 自機の弾 ---
    constexpr float kBulletSpeed = 16.0f;
    constexpr float kBulletLife = 1.0f;
    constexpr float kBulletRadius = 0.25f;
    constexpr float kFireInterval = 0.18f;    // リボルバーらしく、連射しすぎない
    constexpr size_t kPlayerBulletMax = 32;

    // --- 敵の弾 ---
    constexpr float kEnemyBulletSpeed = 7.0f;
    constexpr float kEnemyBulletLife = 3.0f;
    constexpr float kEnemyBulletRadius = 0.25f;
    constexpr size_t kEnemyBulletMax = 48;

    // --- 出現 ---
    constexpr float kFirstSpawnDelay = 0.8f;  // 区切りに入ってから最初の1体が出るまで

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
    camera_.SetPerspective(kCameraFov, static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight, 0.1f, 300.0f);

    // 斜め上から見るカメラ（画面の奥＝+z）。マウスの位置を求めるため、最初の1フレームの前に作っておく
    camera_.SetLookAt({ 0.0f, kCameraHeight, -kCameraBack }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

    player_.Initialize(ctx_->object3dCommon, "suzanne.obj", { 0.0f, kHeight, 0.0f });
    reticle_.Initialize(ctx_->object3dCommon, "teapot.obj", { 0.0f, kHeight, 0.0f });
    reticle_.GetObject3d().GetTransform().Scale = { 0.12f, 0.12f, 0.12f };
    SetTint(reticle_, 1.0f, 1.0f, 1.0f);

    // 弾も敵もマップの見た目も、最初にまとめて作っておく（出すたびに作らない）
    playerBulletPool_.Initialize(ctx_->object3dCommon, "teapot.obj", kPlayerBulletMax);
    enemyBulletPool_.Initialize(ctx_->object3dCommon, "teapot.obj", kEnemyBulletMax);
    brutePool_.Initialize(ctx_->object3dCommon, "teapot.obj", kBruteMax);
    gunmanPool_.Initialize(ctx_->object3dCommon, "suzanne.obj", kGunmanMax);
    mapView_.Initialize(ctx_->object3dCommon, ctx_->modelManager);
    doors_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName); // 立方体は mapView_ が登録したものを使う
    dust_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName);
    weeds_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName);

    LoadMap();
    Reset();
}

void WesternShooterScene::LoadMap()
{
    std::string error;
    MapData loaded;
    mapFromFile_ = false;

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
    doors_.Build(map_);
    if (!mapComplete_) {
        mapMessage_ += "（壁・樽が多すぎて、一部が描かれません）";
    }
}

void WesternShooterScene::Reset()
{
    playerBulletPool_.ReleaseAll();
    enemyBulletPool_.ReleaseAll();
    brutePool_.ReleaseAll();
    gunmanPool_.ReleaseAll();
    playerBullets_.clear();
    enemyBullets_.clear();
    enemies_.clear();
    doors_.Reset();
    dust_.Clear();
    weeds_.Clear();

    playerPos_ = { map_.startX + 0.5f, map_.startZ + 0.5f };
    cameraFocus_ = playerPos_;
    facing_ = { 0.0f, 1.0f };
    playerHp_ = kPlayerMaxHp;
    invincible_ = 0.0f;
    fireTimer_ = 0.0f;
    score_ = 0;
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
}

// ---------------------------------------------------------------- 毎フレーム

void WesternShooterScene::Update()
{
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
    fx_.Update(kDt); // 揺れ・ヒットストップは実時間で進める
    doors_.Step(kDt); // 扉の揺れ・砂ぼこりも実時間で進める
    dust_.Step(kDt);
    weeds_.Step(kDt, map_, cameraFocus_);

    if (state_ != State::Playing) {
        // ゲームオーバー・クリア後：Rでやり直し。クリア後は歩き回れる（敵はもう出ない）
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
    const float dt = fx_.IsHitStop() ? 0.0f : kDt;

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
    // 移動（右が+x、上が+y。上＝画面の奥＝+z）。壁と閉じた区切りには押し返される
    const Vector2 move = input_.Move();
    playerPos_.x += move.x * kPlayerSpeed * dt;
    playerPos_.y += move.y * kPlayerSpeed * dt;
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

    fireTimer_ -= dt;
    if (input_.Fire() && fireTimer_ <= 0.0f) {
        FirePlayerBullet();
        fireTimer_ = kFireInterval;
    }
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
    // 自機の弾：寿命が尽きるか、壁・樽に入ったら返す（マップの外は壁として扱われる）
    for (Projectile& b : playerBullets_) {
        if (!b.obj) {
            continue;
        }
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        b.life -= dt;
        const bool hitWall = IsSolidAt(b.pos);
        if (b.life <= 0.0f || hitWall) {
            if (hitWall) {
                dust_.Emit({ b.pos.x, kHeight, b.pos.y }, 3, 1.2f, 0.18f, 0.35f, 0.75f, 0.68f, 0.55f); // 壁に当たった小さな土煙
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
        if (!remove && state_ == State::Playing && Collision::CircleCircle(b.pos, kEnemyBulletRadius, playerPos_, kPlayerRadius)) {
            DamagePlayer();
            remove = true;
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
            if (!e.obj) {
                continue;
            }
            const float radius = (e.type == EnemyType::Brute) ? kBruteRadius : kGunmanRadius;
            if (!Collision::CircleCircle(b.pos, kBulletRadius, e.pos, radius)) {
                continue;
            }
            playerBulletPool_.Release(b.obj);
            b.obj = nullptr;
            if (--e.hp <= 0) {
                KillEnemy(e);
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
        state_ = State::Cleared;
    }
    else {
        for (size_t i = 0; i < map_.spawns.size() && i < spawnStates_.size(); ++i) {
            if (map_.spawns[i].area == next) {
                spawnStates_[i].timer = kFirstSpawnDelay;
            }
        }
    }
    for (size_t i = 0; i < map_.gates.size(); ++i) {
        mapView_.SetGateVisible(i, IsGateClosed(i));
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
    camera_.SetLookAt({ cx, kCameraHeight, cz - kCameraBack }, { cx, 0.0f, cz }, { 0.0f, 1.0f, 0.0f });
}

void WesternShooterScene::UpdateVisuals()
{
    // 無敵の間は赤く見せる
    if (invincible_ > 0.0f) {
        SetTint(player_, 1.0f, 0.4f, 0.4f);
    }
    else {
        SetTint(player_, 0.5f, 1.0f, 0.5f);
    }
    Place(player_, playerPos_);
    Place(reticle_, aimPoint_);

    ApplyCamera(); // 揺れを足し直す（描くのはこのカメラ）

    // 見た目の行列を更新（描く前に必ず）。使用中のものだけ
    const Matrix4x4& view = camera_.GetViewMatrix();
    const Matrix4x4& proj = camera_.GetProjectionMatrix();
    mapView_.Update(view, proj);
    doors_.Update(view, proj);
    dust_.Update(view, proj);
    weeds_.Update(view, proj);
    player_.GetObject3d().Update(view, proj);
    reticle_.GetObject3d().Update(view, proj);
    playerBulletPool_.Update(view, proj);
    enemyBulletPool_.Update(view, proj);
    brutePool_.Update(view, proj);
    gunmanPool_.Update(view, proj);
}

void WesternShooterScene::Draw()
{
    ctx_->object3dCommon->PreDraw();
    mapView_.Draw();
    weeds_.Draw();
    doors_.Draw();
    if (state_ != State::GameOver) {
        player_.Draw();
        if (aimValid_) {
            reticle_.Draw(); // マウスが指している点
        }
    }
    playerBulletPool_.Draw();
    enemyBulletPool_.Draw();
    brutePool_.Draw();
    gunmanPool_.Draw();
    dust_.Draw();
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

void WesternShooterScene::FirePlayerBullet()
{
    GameObject* obj = playerBulletPool_.Acquire({ playerPos_.x, kHeight, playerPos_.y });
    if (!obj) {
        return; // 満杯：出さない
    }
    obj->GetObject3d().GetTransform().Scale = { 0.2f, 0.2f, 0.2f };
    SetTint(*obj, 1.0f, 0.9f, 0.3f);

    Projectile b;
    b.obj = obj;
    b.pos = playerPos_;
    b.vel = { facing_.x * kBulletSpeed, facing_.y * kBulletSpeed };
    b.life = kBulletLife;
    playerBullets_.push_back(b);
}

bool WesternShooterScene::SpawnEnemy(size_t spawnIndex)
{
    const MapData::Spawn& spawn = map_.spawns[spawnIndex];
    const EnemyType type = (std::rand() % 2 == 0) ? EnemyType::Brute : EnemyType::Gunman;
    const Vector2 door = { spawn.x + 0.5f, spawn.z + 0.5f }; // ドアのマスの真ん中から出る

    GameObject* obj = PoolOf(type).Acquire({ door.x, kHeight, door.y });
    if (!obj) {
        return false; // 満杯：今回は出さない（呼び出し側があとでもう一度試す）
    }
    Enemy e;
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
        obj->GetObject3d().GetTransform().Scale = { 0.9f, 0.9f, 0.9f };
        SetTint(*obj, 0.9f, 0.3f, 0.3f);
    }
    else {
        e.hp = kGunmanHp;
        e.fireTimer = 0.5f; // 出てすぐには撃たない
        obj->GetObject3d().GetTransform().Scale = { 0.8f, 0.8f, 0.8f };
        SetTint(*obj, 0.3f, 0.5f, 0.9f);
    }
    enemies_.push_back(e);

    // 出現の演出：ドアがバタンと揺れ、足もとに砂ぼこりが立ち、画面がわずかに揺れる
    doors_.Open(spawnIndex);
    dust_.Emit({ door.x, 0.15f, door.y - 0.3f }, 8, 2.2f, 0.4f, 0.7f, 0.82f, 0.72f, 0.52f);
    fx_.Shake(0.04f, 0.1f);
    return true;
}

void WesternShooterScene::FireEnemyBullet(const Enemy& enemy)
{
    GameObject* obj = enemyBulletPool_.Acquire({ enemy.pos.x, kHeight, enemy.pos.y });
    if (!obj) {
        return;
    }
    obj->GetObject3d().GetTransform().Scale = { 0.18f, 0.18f, 0.18f };
    SetTint(*obj, 1.0f, 0.5f, 0.1f);

    const Vector2 dir = Normalized({ playerPos_.x - enemy.pos.x, playerPos_.y - enemy.pos.y }, { 0.0f, -1.0f });
    Projectile b;
    b.obj = obj;
    b.pos = enemy.pos;
    b.vel = { dir.x * kEnemyBulletSpeed, dir.y * kEnemyBulletSpeed };
    b.life = kEnemyBulletLife;
    enemyBullets_.push_back(b);
}

// ---------------------------------------------------------------- ダメージ・返す

void WesternShooterScene::DamagePlayer()
{
    if (invincible_ > 0.0f || state_ != State::Playing) {
        return;
    }
    --playerHp_;
    invincible_ = kInvincibleTime;
    fx_.Shake(0.3f, 0.25f);
}

void WesternShooterScene::KillEnemy(Enemy& enemy)
{
    PoolOf(enemy.type).Release(enemy.obj);
    enemy.obj = nullptr; // 返したあとは触らない
    ++score_;
    fx_.Shake(0.1f, 0.12f);
    fx_.HitStop(2); // 一瞬止める
}

GameObjectPool& WesternShooterScene::PoolOf(EnemyType type)
{
    return (type == EnemyType::Brute) ? brutePool_ : gunmanPool_;
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
    if (currentArea_ == kNoArea) {
        ImGui::Text("区切り: なし（%s）", state_ == State::Cleared ? "全部倒した" : "出現点なし");
    }
    else {
        ImGui::Text("いまの区切り: %d", currentArea_);
    }
    ImGui::Separator();
    ImGui::Text("HP: %d / %d", playerHp_, kPlayerMaxHp);
    ImGui::Text("倒した数: %d", score_);
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
    ImGui::End();
#endif // USE_IMGUI
}
