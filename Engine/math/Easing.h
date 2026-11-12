#pragma once
// イージング（動きに「ゆっくり始まる」「最後に弾む」などの緩急をつける関数集）
//   どれも t（0〜1：どれだけ進んだか）を渡すと、緩急をつけた値（だいたい0〜1）が返る。
//   使い方の例:
//     float t = Easing::OutCubic(timer.Progress());   // だんだん減速する
//     float x = Easing::Lerp(startX, endX, Easing::OutBack(progress)); // 行き過ぎて戻る
//   In = ゆっくり始まる / Out = ゆっくり終わる / InOut = 両方
#include <cmath>

namespace Easing
{
    constexpr float kPi = 3.14159265358979f;

    inline float Linear(float t) { return t; }

    // 2乗（弱め）
    inline float InQuad(float t) { return t * t; }
    inline float OutQuad(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
    inline float InOutQuad(float t)
    {
        return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    }

    // 3乗（普通）
    inline float InCubic(float t) { return t * t * t; }
    inline float OutCubic(float t) { return 1.0f - std::pow(1.0f - t, 3.0f); }
    inline float InOutCubic(float t)
    {
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }

    // サイン波（なめらか）
    inline float InSine(float t) { return 1.0f - std::cos(t * kPi / 2.0f); }
    inline float OutSine(float t) { return std::sin(t * kPi / 2.0f); }
    inline float InOutSine(float t) { return -(std::cos(kPi * t) - 1.0f) / 2.0f; }

    // 指数（急に動いて、ゆっくり止まる）
    inline float OutExpo(float t) { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }

    // 行き過ぎてから戻る
    inline float OutBack(float t)
    {
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
    }

    // ゴムのようにビヨンビヨンして止まる
    inline float OutElastic(float t)
    {
        if (t <= 0.0f) { return 0.0f; }
        if (t >= 1.0f) { return 1.0f; }
        const float c4 = (2.0f * kPi) / 3.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }

    // ボールのように何度かバウンドして止まる
    inline float OutBounce(float t)
    {
        const float n1 = 7.5625f;
        const float d1 = 2.75f;
        if (t < 1.0f / d1) {
            return n1 * t * t;
        }
        else if (t < 2.0f / d1) {
            t -= 1.5f / d1;
            return n1 * t * t + 0.75f;
        }
        else if (t < 2.5f / d1) {
            t -= 2.25f / d1;
            return n1 * t * t + 0.9375f;
        }
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }

    // ImGuiのコンボボックスなどで種類を選ぶとき用
    enum class Type
    {
        Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic,
        InSine, OutSine, InOutSine, OutExpo, OutBack, OutElastic, OutBounce,
        Count
    };

    inline const char* Name(Type type)
    {
        static const char* kNames[] = {
            "Linear", "InQuad", "OutQuad", "InOutQuad", "InCubic", "OutCubic", "InOutCubic",
            "InSine", "OutSine", "InOutSine", "OutExpo", "OutBack", "OutElastic", "OutBounce"
        };
        const int i = static_cast<int>(type);
        return (i >= 0 && i < static_cast<int>(Type::Count)) ? kNames[i] : "?";
    }

    // 種類を指定して計算する（t は0〜1に丸められる）
    inline float Apply(Type type, float t)
    {
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        switch (type) {
        case Type::Linear:     return Linear(t);
        case Type::InQuad:     return InQuad(t);
        case Type::OutQuad:    return OutQuad(t);
        case Type::InOutQuad:  return InOutQuad(t);
        case Type::InCubic:    return InCubic(t);
        case Type::OutCubic:   return OutCubic(t);
        case Type::InOutCubic: return InOutCubic(t);
        case Type::InSine:     return InSine(t);
        case Type::OutSine:    return OutSine(t);
        case Type::InOutSine:  return InOutSine(t);
        case Type::OutExpo:    return OutExpo(t);
        case Type::OutBack:    return OutBack(t);
        case Type::OutElastic: return OutElastic(t);
        case Type::OutBounce:  return OutBounce(t);
        default:               return t;
        }
    }

    // a から b へ、緩急をつけて動かす（t は0〜1）
    inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    inline float Ease(Type type, float a, float b, float t) { return Lerp(a, b, Apply(type, t)); }
}
