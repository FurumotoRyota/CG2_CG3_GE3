#include "PoolTestScene.h"

#include <algorithm>
#include <cmath>

#include "base/WinApp.h"
#include "3d/Object3dCommon.h"
#include "input/Input.h"
#include "editor/EditorLayout.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

namespace {
    constexpr float kDt = 1.0f / 60.0f;
    constexpr float kPlayerSpeed = 6.0f;   // 1秒あたり
    constexpr float kArenaHalf = 9.0f;     // 自機が動ける範囲（中心から±）
    constexpr float kBulletSpeed = 14.0f;
    constexpr float kBulletLife = 1.2f;    // 弾が消えるまでの秒数
    constexpr float kFireInterval = 0.06f; // 連射の間隔
    constexpr size_t kPoolSize = 24;       // 同時に出せる弾の数
    constexpr float kHeight = 0.5f;        // 見た目の高さ(y)
}

void PoolTestScene::Initialize()
{
    camera_.SetPerspective(0.9f, static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight, 0.1f, 300.0f);
    // 固定カメラ：真上から原点を見る（上＝画面の奥＝+z）
    camera_.SetLookAt({ 0.0f, 20.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });

    player_.Initialize(ctx_->object3dCommon, "suzanne.obj", { 0.0f, kHeight, 0.0f });

    // 弾は最初にまとめて作っておく（撃つたびに作らない）
    bulletPool_.Initialize(ctx_->object3dCommon, "teapot.obj", kPoolSize);
}

void PoolTestScene::Shoot()
{
    GameObject* obj = bulletPool_.Acquire({ playerPos_.x, kHeight, playerPos_.y });
    ++shotCount_;
    if (!obj) {
        ++missedCount_; // 満杯：出さない
        return;
    }
    obj->GetObject3d().GetTransform().Scale = { 0.2f, 0.2f, 0.2f };

    Bullet b;
    b.obj = obj;
    b.pos = playerPos_;
    b.vel = { facing_.x * kBulletSpeed, facing_.y * kBulletSpeed };
    b.life = kBulletLife;
    bullets_.push_back(b);
}

void PoolTestScene::Update()
{
    input_.Update(*ctx_->input);

    // --- 自機 ---
    const Vector2 move = input_.Move(); // 右が+x、上が+y（上＝画面の奥＝+z）
    playerPos_.x += move.x * kPlayerSpeed * kDt;
    playerPos_.y += move.y * kPlayerSpeed * kDt;
    playerPos_.x = std::clamp(playerPos_.x, -kArenaHalf, kArenaHalf);
    playerPos_.y = std::clamp(playerPos_.y, -kArenaHalf, kArenaHalf);
    if (move.x != 0.0f || move.y != 0.0f) {
        const float len = std::sqrt(move.x * move.x + move.y * move.y);
        facing_ = { move.x / len, move.y / len };
    }
    player_.GetObject3d().GetTransform().Translate = { playerPos_.x, kHeight, playerPos_.y };

    // --- 撃つ ---
    fireTimer_ -= kDt;
    if (input_.Fire() && fireTimer_ <= 0.0f) {
        Shoot();
        fireTimer_ = kFireInterval;
    }

    // --- 弾を動かす。寿命が尽きたらプールへ返す ---
    for (Bullet& b : bullets_) {
        b.pos.x += b.vel.x * kDt;
        b.pos.y += b.vel.y * kDt;
        b.life -= kDt;
        b.obj->GetObject3d().GetTransform().Translate = { b.pos.x, kHeight, b.pos.y };
        if (b.life <= 0.0f) {
            bulletPool_.Release(b.obj);
            b.obj = nullptr; // 返したあとは触らない
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Bullet& b) { return b.obj == nullptr; }), bullets_.end());

    // --- 見た目の行列を更新（描く前に必ず） ---
    const Matrix4x4& view = camera_.GetViewMatrix();
    const Matrix4x4& proj = camera_.GetProjectionMatrix();
    player_.GetObject3d().Update(view, proj);
    bulletPool_.Update(view, proj); // 使用中の弾だけ
}

void PoolTestScene::Draw()
{
    ctx_->object3dCommon->PreDraw();
    player_.Draw();
    bulletPool_.Draw(); // 使用中の弾だけ
}

void PoolTestScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: プール確認");
    ImGui::Separator();
    ImGui::Text("移動 WASD / 矢印 / 左スティック");
    ImGui::Text("撃つ J / マウス左 / RB・RT");
    ImGui::Separator();
    ImGui::Text("使用中の弾: %zu / %zu", bulletPool_.ActiveCount(), bulletPool_.Capacity());
    ImGui::Text("飛んでいる弾(配列): %zu", bullets_.size());
    ImGui::Text("撃った数: %d", shotCount_);
    ImGui::Text("満杯で出せなかった数: %d", missedCount_);
    ImGui::TextDisabled("使用中の数と配列の数が同じなら、返し忘れなし");
    ImGui::End();
#endif // USE_IMGUI
}
