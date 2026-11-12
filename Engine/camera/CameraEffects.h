#pragma once
// 画面の演出の道具（ヘッダーだけで使える）：画面の揺れ と ヒットストップ
//   Shake(大きさ, 秒)   : 画面を揺らす（大きさはワールド単位。0.1〜0.5くらい）
//   HitStop(フレーム数) : 当たった瞬間に、ゲームの時間を一瞬止める（2〜6フレームくらい）
//   毎フレーム Update(実時間の1フレーム) を1回呼ぶ。
//   揺れは ShakeOffset()（x:右 y:上）をカメラの位置に足して使う。
//   ヒットストップは IsHitStop() が true の間、ゲームの時間を進めない。
#include <algorithm>
#include <cmath>

#include "../math/Vector2.h"

class CameraEffects
{
public:
    // 画面を揺らす。いま揺れているより弱い揺れは無視する。frequency は揺れの速さ（1秒あたりの往復）
    void Shake(float amplitude, float duration, float frequency = 28.0f)
    {
        const float now = shakeDuration_ > 0.0f ? shakeAmp_ * (shakeTime_ / shakeDuration_) : 0.0f;
        if (amplitude >= now) {
            shakeAmp_ = amplitude;
            shakeDuration_ = (std::max)(0.01f, duration);
            shakeTime_ = shakeDuration_;
            shakeFreq_ = frequency;
        }
    }

    // ゲームの時間を frames フレームだけ止める
    void HitStop(int frames)
    {
        hitStopFrames_ = (std::max)(hitStopFrames_, frames);
    }

    // 毎フレーム1回（実時間で進める。ヒットストップ中でも呼ぶこと）
    void Update(float realDt)
    {
        if (shakeTime_ > 0.0f) {
            shakeTime_ = (std::max)(0.0f, shakeTime_ - realDt);
            phase_ += realDt * shakeFreq_ * 6.2831853f;
        }
        if (hitStopFrames_ > 0) {
            --hitStopFrames_;
        }
    }

    // 揺れのずれ（x:右 y:上）。だんだん弱くなる
    Vector2 ShakeOffset() const
    {
        if (shakeTime_ <= 0.0f || shakeDuration_ <= 0.0f) {
            return { 0.0f, 0.0f };
        }
        const float k = shakeAmp_ * (shakeTime_ / shakeDuration_);
        return { k * std::sin(phase_ * 1.0f + 0.3f) * std::cos(phase_ * 0.37f), k * std::sin(phase_ * 1.31f + 1.7f) * std::cos(phase_ * 0.53f + 0.8f) };
    }

    bool IsShaking() const { return shakeTime_ > 0.0f; }
    bool IsHitStop() const { return hitStopFrames_ > 0; }

    void Reset()
    {
        shakeAmp_ = 0.0f;
        shakeDuration_ = 0.0f;
        shakeTime_ = 0.0f;
        hitStopFrames_ = 0;
        phase_ = 0.0f;
    }

private:
    float shakeAmp_ = 0.0f;
    float shakeDuration_ = 0.0f;
    float shakeTime_ = 0.0f;
    float shakeFreq_ = 28.0f;
    float phase_ = 0.0f;
    int hitStopFrames_ = 0;
};
