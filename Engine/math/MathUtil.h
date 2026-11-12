#pragma once
// 算数の小道具（ヘッダーだけで使える）
//   MathUtil::Clamp / Lerp / Remap / MoveToward / Damp / WrapAngle など
//   Vector2 / Vector3 向けの Lerp・距離・正規化もここにある
#include <algorithm>
#include <cmath>

#include "Vector2.h"
#include "Vector3.h"

namespace MathUtil
{
    constexpr float kPi = 3.14159265358979f;

    inline float ToRadians(float degrees) { return degrees * kPi / 180.0f; }
    inline float ToDegrees(float radians) { return radians * 180.0f / kPi; }

    // 値を min〜max の範囲に収める
    inline float Clamp(float v, float lo, float hi) { return (std::max)(lo, (std::min)(v, hi)); }
    inline int Clamp(int v, int lo, int hi) { return (std::max)(lo, (std::min)(v, hi)); }
    inline float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }

    // a から b の間を t（0〜1）で混ぜる
    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    // v が a〜b のどのあたりか（0〜1）。Lerp の逆
    inline float InverseLerp(float a, float b, float v)
    {
        return (a == b) ? 0.0f : Clamp01((v - a) / (b - a));
    }

    // v を [inLo, inHi] の範囲から [outLo, outHi] の範囲へ写す（例: 体力0〜100 を ゲージ幅0〜300 に）
    inline float Remap(float v, float inLo, float inHi, float outLo, float outHi)
    {
        return Lerp(outLo, outHi, InverseLerp(inLo, inHi, v));
    }

    // current を target に向けて、最大 maxDelta だけ近づける（行き過ぎない）
    inline float MoveToward(float current, float target, float maxDelta)
    {
        if (std::fabs(target - current) <= maxDelta) {
            return target;
        }
        return current + (target > current ? maxDelta : -maxDelta);
    }

    // フレームレートに左右されない「なめらかな追従」。lambda が大きいほど速く追いつく（5〜15くらい）
    //   例: camX = MathUtil::Damp(camX, playerX, 8.0f, dt);
    inline float Damp(float current, float target, float lambda, float dt)
    {
        return Lerp(current, target, 1.0f - std::exp(-lambda * dt));
    }

    // 0〜1 の間をなめらかにつなぐ（端でゆっくり）
    inline float Smoothstep(float t)
    {
        t = Clamp01(t);
        return t * t * (3.0f - 2.0f * t);
    }

    // 角度（ラジアン）を -π〜π に収める
    inline float WrapAngle(float radians)
    {
        radians = std::fmod(radians + kPi, 2.0f * kPi);
        if (radians < 0.0f) {
            radians += 2.0f * kPi;
        }
        return radians - kPi;
    }

    // 角度 current を target に向けて、近い回り方で最大 maxDelta だけ回す
    inline float MoveTowardAngle(float current, float target, float maxDelta)
    {
        const float diff = WrapAngle(target - current);
        if (std::fabs(diff) <= maxDelta) {
            return target;
        }
        return current + (diff > 0.0f ? maxDelta : -maxDelta);
    }

    inline bool Approx(float a, float b, float eps = 1.0e-5f) { return std::fabs(a - b) <= eps; }

    // ---- Vector2 ----
    inline Vector2 Lerp(const Vector2& a, const Vector2& b, float t)
    {
        return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t) };
    }
    inline float Length(const Vector2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
    inline float Distance(const Vector2& a, const Vector2& b) { return Length(Vector2{ b.x - a.x, b.y - a.y }); }
    inline Vector2 Normalize(const Vector2& v)
    {
        const float len = Length(v);
        return len > 0.0f ? Vector2{ v.x / len, v.y / len } : Vector2{ 0.0f, 0.0f };
    }

    // ---- Vector3 ----
    inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t)
    {
        return { Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t) };
    }
    inline float Distance(const Vector3& a, const Vector3& b) { return b.Subtract(a).Length(); }
}
