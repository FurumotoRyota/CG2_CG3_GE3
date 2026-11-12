#include "SakuraStorm.h"

#include <algorithm>
#include <cmath>

#include "base/WinApp.h"
#include "2d/SpriteCommon.h"

namespace
{
    constexpr float kScreenW = static_cast<float>(WinApp::kGameWidth);
    constexpr float kScreenH = static_cast<float>(WinApp::kGameHeight);
    constexpr float kCell = 64.0f;   // 花びらの画像1枚の大きさ
}

void SakuraStorm::Initialize(SpriteCommon* common)
{
    petals_.clear();
    petals_.resize(kMax);
    for (Petal& p : petals_) {
        p.sprite = std::make_unique<Sprite>();
        p.sprite->Initialize(common, "Engine/resources/startup/rei_petal.png", { kCell, kCell });
        p.alive = false;
    }
    rng_.seed(20261111);
    emitRate_ = 0.0f;
    giantRate_ = 0.0f;
    giantAccum_ = 0.0f;
    sizeScale_ = 1.0f;
    emitAccum_ = 0.0f;
    time_ = 0.0f;
}

void SakuraStorm::Finalize()
{
    petals_.clear();
}

void SakuraStorm::Spawn(bool anywhere, bool giant)
{
    // 空いている枠を探す（なければ何もしない）
    Petal* slot = nullptr;
    for (Petal& p : petals_) {
        if (!p.alive) {
            slot = &p;
            break;
        }
    }
    if (!slot) {
        return;
    }
    auto rand = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng_); };

    Petal& p = *slot;
    p.alive = true;
    p.giant = giant;
    // 大きい花びら（手前）はゆっくり多く、小さい花びら（奥）は少し速く。大きさに偏りをつける
    const float r = rand(0.0f, 1.0f);
    const float depth = r * r;                       // 0 = 小さい、1 = 大きい
    p.size = (14.0f + 34.0f * depth) * sizeScale_;
    float speed = 1.15f - 0.35f * depth;             // 大きいほど少しゆっくり見える
    p.vx = -rand(650.0f, 1150.0f) * speed;           // 右から左へ吹き抜ける
    p.vy = rand(60.0f, 260.0f) * speed;              // 少し下へ
    p.rot = rand(0.0f, 6.2832f);
    p.spin = rand(-6.0f, 6.0f);
    p.tumble = rand(0.0f, 6.2832f);
    p.tumbleSpeed = rand(4.0f, 10.0f);
    if (giant) {
        // 手前をものすごい速さで横切る、画面の何分の一もある花びら
        p.size = rand(170.0f, 440.0f);
        p.vx = -rand(1500.0f, 2400.0f);
        p.vy = rand(150.0f, 450.0f);
        p.spin = rand(-2.5f, 2.5f);
        p.tumbleSpeed = rand(2.0f, 5.0f);
        p.flutter = rand(40.0f, 120.0f);
        speed = 1.0f;
    }
    if (!giant) {
        p.flutter = rand(60.0f, 180.0f);
    }
    p.phase = rand(0.0f, 6.2832f);
    p.tone = rand(0.9f, 1.0f);
    p.variant = static_cast<int>(rand(0.0f, 3.999f));

    if (anywhere) {
        p.x = rand(0.0f, kScreenW);
        p.y = rand(0.0f, kScreenH * 0.85f);
    }
    else if (rand(0.0f, 1.0f) < 0.7f) {
        p.x = kScreenW + p.size;                     // 右の外から
        p.y = rand(-60.0f, kScreenH * 0.7f);
    }
    else {
        p.x = rand(kScreenW * 0.3f, kScreenW + 100.0f);   // 上の外から
        p.y = -p.size;
    }
}

void SakuraStorm::Prefill(int count)
{
    for (int i = 0; i < count; ++i) {
        Spawn(true);
    }
}

void SakuraStorm::Update(float dt)
{
    time_ += dt;

    // 吹き込む
    emitAccum_ += emitRate_ * dt;
    while (emitAccum_ >= 1.0f) {
        Spawn(false);
        emitAccum_ -= 1.0f;
    }
    giantAccum_ += giantRate_ * dt;
    while (giantAccum_ >= 1.0f) {
        Spawn(false, true);
        giantAccum_ -= 1.0f;
    }

    for (Petal& p : petals_) {
        if (!p.alive) {
            continue;
        }
        // 風の強弱（突風）と、ひらひらした上下のゆれ
        const float gust = 1.0f + 0.25f * std::sin(time_ * 3.0f + p.phase * 0.3f);
        p.x += p.vx * gust * dt;
        p.y += (p.vy + std::sin(time_ * 5.0f + p.phase) * p.flutter) * dt;
        p.rot += p.spin * dt;
        p.tumble += p.tumbleSpeed * dt;

        // 画面の外へ抜けたら消す
        if (p.x < -p.size * 2.0f || p.y > kScreenH + p.size * 2.0f) {
            p.alive = false;
            continue;
        }

        // 見た目：裏返るとき（tumble）は横に細くなる。回転は中心まわりになるよう位置をずらす
        const float flip = (std::max)(0.2f, std::fabs(std::cos(p.tumble)));
        const float s = p.size / kCell;
        const float sx = s * flip;
        const float sy = s;
        const float hw = kCell * 0.5f * sx;
        const float hh = kCell * 0.5f * sy;
        const float c = std::cos(p.rot);
        const float sn = std::sin(p.rot);
        Transform& tr = p.sprite->GetTransform();
        tr.Scale = { sx, sy, 1.0f };
        tr.Rotate = { 0.0f, 0.0f, p.rot };
        // 左上を原点に回転する。中心が (x, y) に来るように、回転した「中心への向き」を引く
        tr.Translate = { p.x - (hw * c - hh * sn), p.y - (hw * sn + hh * c), 0.0f };
        p.sprite->SetTextureRect({ (p.variant % 2) * kCell, (p.variant / 2) * kCell }, { kCell, kCell });
        const float shade = (0.86f + 0.14f * flip) * p.tone;   // 裏返って細いときは、少し暗く
        p.sprite->GetMaterial()->color = { shade, shade, shade, 1.0f };
        p.sprite->Update();
    }
}

void SakuraStorm::Draw()
{
    // 小さい花びらを先に、巨大な花びらを手前に重ねる
    for (Petal& p : petals_) {
        if (p.alive && !p.giant) {
            p.sprite->Draw();
        }
    }
    for (Petal& p : petals_) {
        if (p.alive && p.giant) {
            p.sprite->Draw();
        }
    }
}

int SakuraStorm::AliveCount() const
{
    int n = 0;
    for (const Petal& p : petals_) {
        if (p.alive) {
            ++n;
        }
    }
    return n;
}
