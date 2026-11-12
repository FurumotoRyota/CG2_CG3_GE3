#pragma once
// 乱数（ヘッダーだけで使える）
//   Random rng;                      // 毎回違う乱数
//   Random rng(1234);                // 同じ番号なら毎回同じ並び（テストや再現に便利）
//   int   n = rng.Int(1, 6);         // 1〜6（両端を含む）
//   float f = rng.Float(0.0f, 1.0f); // 0〜1
//   if (rng.Chance(0.3f)) { ... }    // 30%の確率で true
//   Random::Shared().Int(0, 9);      // どこからでも使える共有の乱数
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "Vector2.h"

class Random
{
public:
    Random() : engine_(std::random_device{}()) {}
    explicit Random(unsigned int seed) : engine_(seed) {}

    // 番号を決め直す
    void Seed(unsigned int seed) { engine_.seed(seed); }

    // lo〜hi の整数（両端を含む）
    int Int(int lo, int hi)
    {
        if (lo > hi) { std::swap(lo, hi); }
        return std::uniform_int_distribution<int>(lo, hi)(engine_);
    }

    // lo〜hi の小数
    float Float(float lo = 0.0f, float hi = 1.0f)
    {
        if (lo > hi) { std::swap(lo, hi); }
        return lo + (hi - lo) * std::uniform_real_distribution<float>(0.0f, 1.0f)(engine_);
    }

    // probability（0〜1）の確率で true
    bool Chance(float probability) { return Float() < probability; }

    // 円の内側のランダムな点（中心 0,0・半径 radius）
    Vector2 InCircle(float radius)
    {
        const float angle = Float(0.0f, 6.2831853f);
        const float r = radius * std::sqrt(Float());
        return { std::cos(angle) * r, std::sin(angle) * r };
    }

    // 長さ1のランダムな向き
    Vector2 Direction()
    {
        const float angle = Float(0.0f, 6.2831853f);
        return { std::cos(angle), std::sin(angle) };
    }

    // 配列からランダムに1つ選ぶ（空のときは使わないこと）
    template <typename T>
    const T& Pick(const std::vector<T>& items)
    {
        return items[static_cast<size_t>(Int(0, static_cast<int>(items.size()) - 1))];
    }

    // 配列をシャッフルする
    template <typename T>
    void Shuffle(std::vector<T>& items)
    {
        std::shuffle(items.begin(), items.end(), engine_);
    }

    // どこからでも使える共有の乱数
    static Random& Shared()
    {
        static Random instance;
        return instance;
    }

private:
    std::mt19937 engine_;
};
