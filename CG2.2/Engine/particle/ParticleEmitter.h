#pragma once
#include "../math/Vector3.h"
#include "../math/Vector4.h"

/// <summary>
/// パーティクルの演出プリセット（ApplyPreset で各パラメータがまとめて設定される）
/// </summary>
enum class ParticlePreset
{
    Fire,       // 炎：上に昇りながら小さくなる（加算合成）
    Explosion,  // 爆発：全方向にバーストして減速する
    Fireworks,  // 花火：打ち上がって、最後に色とりどりの火花に弾ける
    Snow,       // 雪：上からゆらゆら降る
    Sparkle,    // キラキラ：その場でふわっと光って縮む
    Rainbow,    // 虹色：噴水のように吹き上がる四角い粒（従来のパーティー演出）
    Count
};
inline constexpr int kParticlePresetCount = static_cast<int>(ParticlePreset::Count);
inline constexpr const char* kParticlePresetNames[kParticlePresetCount] = {
    "Fire", "Explosion", "Fireworks", "Snow", "Sparkle", "Rainbow"
};

// 発生のしかた
enum class EmitMode
{
    Continuous, // Rate(個/秒)で出し続ける
    Burst       // Interval秒ごとに Burst Count 個を一度に出す（Interval=0なら Play ボタンのときだけ）
};
inline constexpr const char* kEmitModeNames[2] = { "Continuous", "Burst" };

// 粒の形（ポリゴンは四角のまま、ピクセルシェーダーで形を作る）
enum class ParticleShape
{
    SoftCircle, // ふちがぼやけた丸（光・炎向き）
    Circle,     // くっきりした丸
    Square      // 四角
};
inline constexpr const char* kParticleShapeNames[3] = { "Soft Circle", "Circle", "Square" };

/// <summary>
/// パーティクルエミッター1つぶんの設定（シーンに置く項目）。描画・粒の更新は ParticleSystem が行う
/// </summary>
struct ParticleEmitter
{
    ParticlePreset preset = ParticlePreset::Fire;
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    bool enabled = true;

    // 発生
    EmitMode mode = EmitMode::Continuous;
    float rate = 60.0f;          // Continuous：1秒あたりの発生数
    int burstCount = 100;        // Burst：1回の発生数
    float burstInterval = 2.0f;  // Burst：発生の間隔(秒)。0なら自動では出さない
    float radius = 0.3f;         // 発生位置のばらつき（球の半径）
    bool emitFlat = false;       // true なら水平の円盤(XZ)の上から発生させる

    // 初速
    Vector3 direction{ 0.0f, 1.0f, 0.0f };
    float spreadDeg = 30.0f;     // 方向のばらつき(度)。180で全方向
    float speedMin = 2.0f;
    float speedMax = 4.0f;

    // 動き
    float lifeMin = 0.6f;
    float lifeMax = 1.2f;
    float gravity = 0.0f;        // 下向きの加速度（マイナスだと上に昇る）
    float drag = 0.0f;           // 空気抵抗（大きいほど早く減速する）
    float wobble = 0.0f;         // 横ゆれの強さ

    // 見た目（生まれてから消えるまでの変化）
    float sizeStart = 1.0f;
    float sizeEnd = 0.1f;
    float sizeRandom = 0.3f;     // 大きさのばらつき(0～1)
    Vector4 colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
    bool rainbow = false;        // true なら粒ごとにランダムな色相（RGBは無視。アルファだけ使う）
    ParticleShape shape = ParticleShape::SoftCircle;
    bool additive = true;        // true:加算合成(光る) false:通常のαブレンド

    // 花火：発生した粒(ロケット)が消えるとき、その場で火花に弾ける
    bool fireworks = false;
    int sparkCount = 90;

    // ---- 実行時の状態（エディタでは触らない） ----
    float emitAccumulator = 0.0f;
    float burstTimer = 0.0f;
    bool playRequested = false;

    // プリセットの値を全て設定する（位置とenabledは変えない）
    void ApplyPreset(ParticlePreset newPreset);
};
