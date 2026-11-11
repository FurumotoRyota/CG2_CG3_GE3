#pragma once
// 西部シューティングの中で、WesternShooterScene.cpp と WesternShooterEnemyAI.cpp が共有する数値と小さな関数
#include <cmath>
#include <cstddef>

#include "../game/GameObject.h"
#include "math/Vector2.h"

namespace WesternShooter
{
    inline constexpr float kHeight = 0.5f;           // 見た目の高さ(y)

    // --- 見た目の大きさ ---
    //   モデル（suzanne / teapot）は横幅が約 2.7〜3.2 と大きい。見た目の幅が当たり判定の直径（半径×2）に近くなるように縮める。
    inline constexpr float kPlayerScale = 0.42f;
    inline constexpr float kBruteScale = 0.40f;
    inline constexpr float kGunmanScale = 0.40f;
    inline constexpr float kBulletScale = 0.10f;      // 弾の見た目（モデルは teapot）

    // --- 自機 ---
    inline constexpr float kPlayerRadius = 0.6f;

    // --- 敵：ごろつき ---
    inline constexpr float kBruteSpeed = 3.8f;        // ふだんの歩き
    inline constexpr float kBruteRushSpeed = 5.4f;   // 突っ込むとき
    inline constexpr float kBruteAimTime = 0.6f;     // 突っ込む前の構え（この間、突進の線が見える）
    inline constexpr float kBruteChargeSpeed = 9.5f; // 突進の速さ（向きは構えの終わりに固定）
    inline constexpr float kBruteChargeTime = 0.7f;  // 突進の長さ
    inline constexpr float kBruteRecoverTime = 0.7f; // 突進のあとの息切れ（この間は無防備）
    inline constexpr float kBruteHoldSpeed = 2.8f;   // 自機を取り巻いて待つとき
    inline constexpr float kBruteRadius = 0.7f;
    inline constexpr int kBruteHp = 1;
    inline constexpr size_t kBruteMax = 16;
    inline constexpr float kRushDistance = 6.0f;     // この距離まで近づいたら、突っ込んでよいか確かめる
    inline constexpr int kMaxRushers = 3;            // 同時に突っ込める数（残りは取り巻いて待つ）
    inline constexpr float kHoldRing = 5.5f;         // 取り巻いて待つときの、自機からの距離

    // --- 敵：ガンマン ---
    inline constexpr float kGunmanSpeed = 2.7f;
    inline constexpr float kGunmanBackSpeed = 2.5f;      // 近づかれたときに下がる
    inline constexpr float kGunmanStrafeSpeed = 1.2f;    // 撃ちながらの横移動
    inline constexpr float kGunmanRadius = 0.6f;
    inline constexpr int kGunmanHp = 1;
    inline constexpr size_t kGunmanMax = 16;
    inline constexpr float kGunmanKeepDistance = 8.0f;   // この距離まで近づいたら止まって撃つ
    inline constexpr float kGunmanTooClose = 4.0f;       // これより近いと下がる
    inline constexpr float kGunmanShotInterval = 0.34f;  // 続けて撃つ間隔
    inline constexpr int kGunmanBurst = 3;               // 1回の顔出しで撃つ数
    inline constexpr int kMaxShooters = 3;               // 同時に顔を出して撃っていい数（残りは待つ）
    inline constexpr float kCoverSearchRadius = 8.0f;    // 隠れ場所を探す範囲
    inline constexpr float kCoverMinThreatDist = 4.0f;   // 隠れ場所は自機からこれ以上離れている
    inline constexpr float kCoverMaxThreatDist = 13.0f;  // 隠れ場所は自機からこれ以内
    inline constexpr float kReloadMin = 0.6f;            // 陰で待ってから顔を出すまでの時間
    inline constexpr float kReloadRange = 0.8f;
    inline constexpr float kCoverEngageDistance = 12.0f; // 自機がこの距離まで入ったら、まず隠れ場所へ向かう
    inline constexpr float kPeekMaxDist = 3.5f;          // 隠れ場所から顔を出すために動く距離の上限
    inline constexpr float kPeekSpeed = 4.2f;            // 顔を出すときの速さ
    inline constexpr float kCoverCheckInterval = 0.25f;  // 隠れ場所が自機から見えていないか確かめる間隔
    inline constexpr float kCoverApproachWeight = 0.8f;  // 隠れ場所を選ぶとき、自機に近い場所をどれだけ好むか（隠れながら前へ詰める）
    inline constexpr float kCoverSwitchCooldown = 1.5f;  // 隠れ場所を選び直したあと、次に選び直せるようになるまでの時間
    inline constexpr float kCoverRetry = 1.0f;           // 隠れ場所が見つからなかったとき、探し直すまでの時間

    // --- 敵の登場演出：酒場のドアから大きく飛び出し、着地で衝撃波 ---
    //   飛んでいる間は弾が当たらない。着地点には赤い輪が出て、着地の瞬間に輪の中にいるとダメージ（転がれば避けられる）
    inline constexpr float kSpawnFlightTime = 0.8f;
    inline constexpr float kSpawnLeapDistance = 6.5f;   // 自機の方へ、これだけ飛ぶ（自機のすぐ手前までで止まる）
    inline constexpr float kSpawnPeak = 7.0f;           // 飛ぶ高さ（建物の屋根より高く跳んで、手前のカメラからも見えるように）
    inline constexpr float kSpawnStompRadius = 1.3f;    // 着地で踏みつぶす範囲
    inline constexpr float kBossFlightTime = 1.3f;
    inline constexpr float kBossLeapDistance = 9.0f;
    inline constexpr float kBossPeak = 9.0f;
    inline constexpr float kBossStompRadius = 2.6f;

    // --- フェーズ登場：5人が同時に飛び出して整列し、順番にポーズを決める（ギニュー特戦隊風） ---
    inline constexpr size_t kFormationCount = 5;        // 一斉に出る数（残りが少なければ、その数）
    inline constexpr float kFormationSpacing = 2.6f;    // 並んだときの横の間隔
    inline constexpr float kFormationDistance = 7.0f;   // ドアから、並ぶ列までの距離（自機の側へ）
    inline constexpr float kFormationFlight = 1.0f;     // 全員が同時に着地するよう、飛ぶ時間をそろえる
    inline constexpr float kPoseStart = 0.3f;           // 着地してから、最初のポーズまで
    inline constexpr float kPoseInterval = 0.4f;        // 次のポーズまでの間隔（左から順に）
    inline constexpr float kPoseHold = 0.9f;            // 最後のポーズのあと、カメラが戻り始めるまで

    // --- ボス：賞金首（早撃ちの決闘型） ---
    //   歩きながら間合いを保ち、狙いの線で予告してから「扇状の連射」「まっすぐな連射」を撃つ。撃ったあとの隙（Recover）に当てるとダメージ2倍
    inline constexpr float kBossScale = 0.95f;
    inline constexpr float kBossRadius = 1.3f;
    inline constexpr int kBossHp = 10;
    inline constexpr float kBossSpeed = 3.0f;
    inline constexpr float kBossKeepDistance = 9.0f;   // 自機との間合い
    inline constexpr float kBossIntroTime = 0.8f;      // 登場の演出が終わってから、撃ち始めるまでの間（撃たない）
    inline constexpr float kBossPaceMin = 1.6f;        // 歩いてから次の構えまで
    inline constexpr float kBossPaceRange = 1.2f;
    inline constexpr float kBossTelegraph = 1.0f;      // 構え（狙いの線が見える）
    inline constexpr float kBossRecover = 1.5f;        // 撃ったあとの隙。この間は2倍のダメージ
    inline constexpr float kBossBulletSpeed = 12.0f;
    inline constexpr int kBossFanCount = 5;            // 扇の弾の数
    inline constexpr float kBossFanSpread = 0.30f;     // 扇の弾と弾の角度（ラジアン）
    inline constexpr int kBossBurstCount = 6;          // まっすぐ連射の数
    inline constexpr float kBossBurstInterval = 0.12f;
    inline constexpr float kBossEnrageScale = 0.7f;    // HP半分以下：構え・隙の時間がこの倍率に縮む（弾は増える）

    // --- 狙いの線（撃つ前の予告） ---
    inline constexpr float kGunmanAimTime = 0.7f;     // 構えてから最初の1発まで（この間、線が見える）
    inline constexpr float kAimLockTime = 0.25f;      // 撃つ直前のこの時間は、狙いの向きが固定される（横へ動けば避けられる）
    inline constexpr float kAimLineLength = 18.0f;    // 線の最大の長さ

    // --- 敵の連携 ---
    inline constexpr int kMaxSuppressors = 2;        // 陰から撃って抑える役の数（残りのガンマンは回り込む）
    inline constexpr float kFlankAngle = 1.3f;       // 回り込み役が、抑え役のいる向きからどれだけ横へ回るか（ラジアン、約75度）
    inline constexpr float kCallRadius = 22.0f;      // 自機を見つけた敵が、居場所を教える範囲
    inline constexpr float kCallInterval = 0.4f;     // 教える間隔
    inline constexpr float kBurstStagger = 0.8f;     // 誰かが撃ち始めてから、次の誰かが撃ち始めるまでの最短の間（交互に撃たせる）
    inline constexpr int kRushGroup = 2;             // ごろつきがこの数そろって取り巻いたら、まとめて突っ込む
    inline constexpr float kRushMaxWait = 3.0f;      // ガンマンが撃たなくても、この時間待ったら突っ込む

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
