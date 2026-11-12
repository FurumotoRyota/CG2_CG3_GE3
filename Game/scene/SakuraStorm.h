#pragma once
// 【部品】桜吹雪（右上から左下へ吹き抜ける花びら）
//   花びらの画像 resources/rei_petal.png（64x64 を 2x2 に並べた4種類）をスプライトで舞わせる。
//   使い方:
//     storm.Initialize(ctx_->spriteCommon);          // 最初に1回
//     storm.SetEmitRate(100.0f);                     // 1秒に何枚ずつ吹き込むか（0 で止める。すでに舞っている花びらは画面の外へ抜けるまで舞う）
//     storm.Update(1.0f / 60.0f);                    // 毎フレーム
//     ctx_->spriteCommon->PreDraw();  storm.Draw();  // 描画
#include <memory>
#include <random>
#include <vector>

#include "2d/Sprite.h"

class SpriteCommon;

class SakuraStorm
{
public:
    static constexpr int kMax = 420;   // 同時に舞える花びらの数

    void Initialize(SpriteCommon* common);
    void Finalize();

    // 1秒に吹き込む枚数
    void SetEmitRate(float perSecond) { emitRate_ = perSecond; }
    // 手前を横切る巨大な花びらを、1秒に何枚ずつ吹き込むか（画面を覆いつくすために使う。0 で止める）
    void SetGiantRate(float perSecond) { giantRate_ = perSecond; }
    // 花びらの大きさの倍率（1 が普通。大きくすると画面を覆いやすい。これから吹き込むものに効く）
    void SetSizeScale(float scale) { sizeScale_ = scale; }
    // 画面のあちこちに、あらかじめ舞っている状態の花びらを n 枚つくる（前の場面から続いているように見せる）
    void Prefill(int count);

    void Update(float dt);
    void Draw();

    int AliveCount() const;

private:
    struct Petal
    {
        std::unique_ptr<Sprite> sprite;
        bool alive = false;
        float x = 0.0f, y = 0.0f;          // 中心の位置（画面のピクセル）
        float vx = 0.0f, vy = 0.0f;        // 風に乗る速さ
        float size = 24.0f;                // 見た目の大きさ（ピクセル）
        float rot = 0.0f, spin = 0.0f;     // 回転
        float tumble = 0.0f, tumbleSpeed = 0.0f;   // ひらひら裏返る動き
        float flutter = 0.0f, phase = 0.0f;        // 上下のゆれ
        float tone = 1.0f;                 // 色の濃さの個体差
        int variant = 0;
        bool giant = false;                // 手前を横切る巨大な花びら
    };
    void Spawn(bool anywhere, bool giant = false);

    std::vector<Petal> petals_;
    std::mt19937 rng_;
    float emitRate_ = 0.0f;
    float giantRate_ = 0.0f;
    float giantAccum_ = 0.0f;
    float sizeScale_ = 1.0f;
    float emitAccum_ = 0.0f;
    float time_ = 0.0f;
};
