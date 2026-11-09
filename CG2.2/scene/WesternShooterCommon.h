#pragma once
// 西部シューティングの中で、WesternShooterScene.cpp と WesternShooterEnemyAI.cpp が共有する数値と小さな関数
#include <cmath>
#include <cstddef>

#include "../game/GameObject.h"
#include "math/Vector2.h"

namespace WesternShooter
{
    inline constexpr float kHeight = 0.5f;           // 見た目の高さ(y)

    // --- 自機 ---
    inline constexpr float kPlayerRadius = 0.6f;

    // --- 敵：ごろつき ---
    inline constexpr float kBruteSpeed = 3.0f;       // ふだんの歩き
    inline constexpr float kBruteRushSpeed = 4.2f;   // 突っ込むとき
    inline constexpr float kBruteHoldSpeed = 2.2f;   // 自機を取り巻いて待つとき
    inline constexpr float kBruteRadius = 0.7f;
    inline constexpr int kBruteHp = 2;
    inline constexpr size_t kBruteMax = 16;
    inline constexpr float kRushDistance = 6.0f;     // この距離まで近づいたら、突っ込んでよいか確かめる
    inline constexpr int kMaxRushers = 3;            // 同時に突っ込める数（残りは取り巻いて待つ）
    inline constexpr float kHoldRing = 5.5f;         // 取り巻いて待つときの、自機からの距離

    // --- 敵：ガンマン ---
    inline constexpr float kGunmanSpeed = 2.0f;
    inline constexpr float kGunmanBackSpeed = 2.5f;      // 近づかれたときに下がる
    inline constexpr float kGunmanStrafeSpeed = 1.2f;    // 撃ちながらの横移動
    inline constexpr float kGunmanRadius = 0.6f;
    inline constexpr int kGunmanHp = 1;
    inline constexpr size_t kGunmanMax = 16;
    inline constexpr float kGunmanKeepDistance = 8.0f;   // この距離まで近づいたら止まって撃つ
    inline constexpr float kGunmanTooClose = 4.0f;       // これより近いと下がる
    inline constexpr float kGunmanShotInterval = 0.45f;  // 続けて撃つ間隔
    inline constexpr int kGunmanBurst = 3;               // 1回の顔出しで撃つ数
    inline constexpr int kMaxShooters = 3;               // 同時に顔を出して撃っていい数（残りは待つ）
    inline constexpr float kCoverSearchRadius = 8.0f;    // 隠れ場所を探す範囲
    inline constexpr float kCoverMinThreatDist = 4.0f;   // 隠れ場所は自機からこれ以上離れている
    inline constexpr float kCoverMaxThreatDist = 13.0f;  // 隠れ場所は自機からこれ以内
    inline constexpr float kReloadMin = 0.6f;            // 陰で待ってから顔を出すまでの時間
    inline constexpr float kReloadRange = 0.8f;
    inline constexpr float kCoverEngageDistance = 12.0f; // 自機がこの距離まで入ったら、まず隠れ場所へ向かう
    inline constexpr float kPeekMaxDist = 3.5f;          // 隠れ場所から顔を出すために動く距離の上限
    inline constexpr float kPeekSpeed = 3.2f;            // 顔を出すときの速さ
    inline constexpr float kCoverCheckInterval = 0.25f;  // 隠れ場所が自機から見えていないか確かめる間隔
    inline constexpr float kCoverRetry = 1.0f;           // 隠れ場所が見つからなかったとき、探し直すまでの時間

    // --- 敵の共通 ---
    inline constexpr float kSightRange = 16.0f;      // これより遠い自機は見えない
    inline constexpr float kSearchTime = 3.5f;       // 見失ってから、最後に見た場所を探す時間
    inline constexpr float kRepathInterval = 0.4f;   // 道を引き直す間隔
    inline constexpr float kWaypointReach = 0.5f;    // この距離まで近づいたら、次の曲がり角へ

    inline float Length2D(const Vector2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }

    // 長さ1にして返す（長さ0のときは fallback をそのまま返す）
    inline Vector2 Normalized(const Vector2& v, const Vector2& fallback)
    {
        const float len = Length2D(v);
        if (len < 0.0001f) {
            return fallback;
        }
        return { v.x / len, v.y / len };
    }

    inline void SetTint(GameObject& obj, float r, float g, float b)
    {
        if (Material* m = obj.GetObject3d().GetMaterial()) {
            m->color = Vector4{ r, g, b, 1.0f };
        }
    }

    inline void Place(GameObject& obj, const Vector2& pos)
    {
        obj.GetObject3d().GetTransform().Translate = { pos.x, kHeight, pos.y };
    }
}
