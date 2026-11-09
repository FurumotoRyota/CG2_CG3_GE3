#include "ShooterExampleScene.h"

#include <algorithm>
#include <cmath>

#include "base/WinApp.h"
#include "3d/Object3dCommon.h"
#include "input/Input.h"
#include "math/Collision.h"

namespace {
    constexpr float kPlayerSpeed = 6.0f;   // 1秒あたり
    constexpr float kEnemySpeed = 2.5f;
    constexpr float kBulletSpeed = 14.0f;
    constexpr float kBulletLife = 1.5f;
    constexpr float kFireInterval = 0.12f;
    constexpr float kSpawnInterval = 1.0f;
    constexpr float kBulletRadius = 0.2f;
    constexpr float kEnemyRadius = 0.6f;
    constexpr float kHeight = 0.5f;        // 見た目の高さ(y)
}

void ShooterExampleScene::Place(GameObject& obj, const Vector2& pos, float y)
{
    obj.GetObject3d().GetTransform().Translate = { pos.x, y, pos.y };
}

void ShooterExampleScene::Initialize()
{
    camera_.SetPerspective(0.9f, static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight, 0.1f, 300.0f);

    player_ = std::make_unique<GameObject>();
    player_->Initialize(ctx_->object3dCommon, "suzanne.obj", { 0.0f, kHeight, 0.0f });
}

void ShooterExampleScene::Shoot()
{
    Bullet b;
    b.obj = std::make_unique<GameObject>();
    b.obj->Initialize(ctx_->object3dCommon, "teapot.obj", { playerPos_.x, kHeight, playerPos_.y });
    b.obj->GetObject3d().GetTransform().Scale = { 0.2f, 0.2f, 0.2f };
    b.pos = playerPos_;
    b.vel = { facing_.x * kBulletSpeed, facing_.y * kBulletSpeed };
    b.life = kBulletLife;
    bullets_.push_back(std::move(b));
}

void ShooterExampleScene::SpawnEnemy()
{
    // プレイヤーの周り（半径12）の円周上のどこかに出す
    const float angle = static_cast<float>(std::rand() % 628) / 100.0f;
    Enemy e;
    e.obj = std::make_unique<GameObject>();
    e.pos = { playerPos_.x + std::cos(angle) * 12.0f, playerPos_.y + std::sin(angle) * 12.0f };
    e.obj->Initialize(ctx_->object3dCommon, "teapot.obj", { e.pos.x, kHeight, e.pos.y });
    enemies_.push_back(std::move(e));
}

void ShooterExampleScene::Update()
{
    input_.Update(*ctx_->input);
    fx_.Update(1.0f / 60.0f); // 揺れ・ヒットストップは実時間で進める

    // ヒットストップ中は、ゲームの時間を進めない
    const float dt = fx_.IsHitStop() ? 0.0f : 1.0f / 60.0f;

    // --- プレイヤー ---
    const Vector2 move = input_.Move(); // 右が+x、上が+y（上＝画面の奥＝+z）
    playerPos_.x += move.x * kPlayerSpeed * dt;
    playerPos_.y += move.y * kPlayerSpeed * dt;
    if (move.x != 0.0f || move.y != 0.0f) {
        const float len = std::sqrt(move.x * move.x + move.y * move.y);
        facing_ = { move.x / len, move.y / len };
    }
    Place(*player_, playerPos_, kHeight);

    // --- 撃つ ---
    fireTimer_ -= dt;
    if ((input_.Fire() || ctx_->input->PushKey(DIK_J)) && fireTimer_ <= 0.0f) {
        Shoot();
        fireTimer_ = kFireInterval;
    }

    // --- 敵を出す ---
    spawnTimer_ -= dt;
    if (spawnTimer_ <= 0.0f) {
        SpawnEnemy();
        spawnTimer_ = kSpawnInterval;
    }

    // --- 動かす ---
    for (Bullet& b : bullets_) {
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        b.life -= dt;
        Place(*b.obj, b.pos, kHeight);
    }
    for (Enemy& e : enemies_) {
        const float dx = playerPos_.x - e.pos.x;
        const float dy = playerPos_.y - e.pos.y;
        const float len = (std::max)(0.0001f, std::sqrt(dx * dx + dy * dy));
        e.pos.x += dx / len * kEnemySpeed * dt;
        e.pos.y += dy / len * kEnemySpeed * dt;
        Place(*e.obj, e.pos, kHeight);
    }

    // --- 弾と敵の当たり判定：当たったら両方消す ---
    for (Bullet& b : bullets_) {
        for (Enemy& e : enemies_) {
            if (b.life > 0.0f && e.obj && Collision::CircleCircle(b.pos, kBulletRadius, e.pos, kEnemyRadius)) {
                b.life = 0.0f;   // 弾を消す（下で片付ける）
                e.obj.reset();   // 敵を消す（下で片付ける）
                ++score_;
                fx_.Shake(0.15f, 0.15f); // 画面を揺らす
                fx_.HitStop(2);          // 一瞬止める
                break;
            }
        }
    }
    // 消えたものを配列から取り除く
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Bullet& b) { return b.life <= 0.0f; }), bullets_.end());
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return !e.obj; }), enemies_.end());

    // --- カメラ：真上から追う。揺れはカメラの位置に足す ---
    const Vector2 sk = fx_.ShakeOffset();
    camera_.SetLookAt({ playerPos_.x + sk.x, 14.0f, playerPos_.y + sk.y }, { playerPos_.x + sk.x, 0.0f, playerPos_.y + sk.y }, { 0.0f, 0.0f, 1.0f });

    // --- 見た目の行列を更新（描く前に必ず） ---
    const Matrix4x4& view = camera_.GetViewMatrix();
    const Matrix4x4& proj = camera_.GetProjectionMatrix();
    player_->GetObject3d().Update(view, proj);
    for (Bullet& b : bullets_) { b.obj->GetObject3d().Update(view, proj); }
    for (Enemy& e : enemies_) { e.obj->GetObject3d().Update(view, proj); }
}

void ShooterExampleScene::Draw()
{
    ctx_->object3dCommon->PreDraw();
    player_->Draw();
    for (Bullet& b : bullets_) { b.obj->Draw(); }
    for (Enemy& e : enemies_) { e.obj->Draw(); }
}
