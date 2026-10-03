#include "ParticleEmitter.h"

void ParticleEmitter::ApplyPreset(ParticlePreset newPreset)
{
    // 位置・有効/無効は残して、他は初期値に戻してから上書きする
    const Vector3 keepPosition = position;
    const bool keepEnabled = enabled;
    *this = ParticleEmitter{};
    position = keepPosition;
    enabled = keepEnabled;
    preset = newPreset;

    switch (newPreset)
    {
    case ParticlePreset::Fire:
        mode = EmitMode::Continuous;
        rate = 80.0f;
        radius = 0.5f;
        direction = { 0.0f, 1.0f, 0.0f };
        spreadDeg = 15.0f;
        speedMin = 1.5f;
        speedMax = 3.0f;
        lifeMin = 0.6f;
        lifeMax = 1.2f;
        gravity = -3.0f; // 上向きに加速
        drag = 0.5f;
        wobble = 0.5f;
        sizeStart = 1.4f;
        sizeEnd = 0.1f;
        sizeRandom = 0.3f;
        colorStart = { 1.0f, 0.75f, 0.2f, 1.0f };
        colorEnd = { 1.0f, 0.1f, 0.0f, 0.0f };
        shape = ParticleShape::SoftCircle;
        additive = true;
        break;

    case ParticlePreset::Explosion:
        mode = EmitMode::Burst;
        burstCount = 120;
        burstInterval = 2.0f;
        radius = 0.2f;
        direction = { 0.0f, 1.0f, 0.0f };
        spreadDeg = 180.0f;
        speedMin = 4.0f;
        speedMax = 10.0f;
        lifeMin = 0.5f;
        lifeMax = 1.0f;
        gravity = 1.0f;
        drag = 2.5f;
        sizeStart = 1.8f;
        sizeEnd = 0.2f;
        sizeRandom = 0.4f;
        colorStart = { 1.0f, 0.9f, 0.5f, 1.0f };
        colorEnd = { 0.8f, 0.1f, 0.0f, 0.0f };
        shape = ParticleShape::SoftCircle;
        additive = true;
        break;

    case ParticlePreset::Fireworks:
        // 打ち上げ(ロケット)を Interval ごとに1発。消える瞬間に sparkCount 個の火花に弾ける
        mode = EmitMode::Burst;
        burstCount = 1;
        burstInterval = 1.6f;
        radius = 3.0f;
        emitFlat = true;
        direction = { 0.0f, 1.0f, 0.0f };
        spreadDeg = 8.0f;
        speedMin = 10.0f;
        speedMax = 12.0f;
        lifeMin = 1.2f;
        lifeMax = 1.5f;
        gravity = 6.0f;
        drag = 0.0f;
        sizeStart = 0.5f;
        sizeEnd = 0.4f;
        sizeRandom = 0.0f;
        colorStart = { 1.0f, 0.9f, 0.6f, 1.0f };
        colorEnd = { 1.0f, 0.6f, 0.2f, 1.0f };
        shape = ParticleShape::SoftCircle;
        additive = true;
        fireworks = true;
        sparkCount = 90;
        break;

    case ParticlePreset::Snow:
        mode = EmitMode::Continuous;
        rate = 40.0f;
        radius = 10.0f;
        emitFlat = true;
        direction = { 0.0f, -1.0f, 0.0f };
        spreadDeg = 20.0f;
        speedMin = 1.0f;
        speedMax = 2.0f;
        lifeMin = 6.0f;
        lifeMax = 9.0f;
        gravity = 0.0f;
        wobble = 1.5f;
        sizeStart = 0.3f;
        sizeEnd = 0.3f;
        sizeRandom = 0.5f;
        colorStart = { 1.0f, 1.0f, 1.0f, 0.9f };
        colorEnd = { 1.0f, 1.0f, 1.0f, 0.9f };
        shape = ParticleShape::Circle;
        additive = false;
        break;

    case ParticlePreset::Sparkle:
        mode = EmitMode::Continuous;
        rate = 30.0f;
        radius = 2.0f;
        direction = { 0.0f, 1.0f, 0.0f };
        spreadDeg = 180.0f;
        speedMin = 0.5f;
        speedMax = 2.0f;
        lifeMin = 0.8f;
        lifeMax = 1.5f;
        drag = 1.0f;
        sizeStart = 0.8f;
        sizeEnd = 0.0f;
        sizeRandom = 0.5f;
        colorStart = { 1.0f, 1.0f, 1.0f, 1.0f };
        colorEnd = { 1.0f, 1.0f, 1.0f, 0.0f };
        rainbow = true;
        shape = ParticleShape::SoftCircle;
        additive = true;
        break;

    case ParticlePreset::Rainbow:
    default:
        mode = EmitMode::Continuous;
        rate = 60.0f;
        radius = 0.3f;
        direction = { 0.0f, 1.0f, 0.0f };
        spreadDeg = 60.0f;
        speedMin = 2.0f;
        speedMax = 5.0f;
        lifeMin = 0.6f;
        lifeMax = 1.4f;
        gravity = 6.0f;
        sizeStart = 0.5f;
        sizeEnd = 0.5f;
        sizeRandom = 0.4f;
        colorStart = { 1.0f, 1.0f, 1.0f, 1.0f };
        colorEnd = { 1.0f, 1.0f, 1.0f, 0.0f };
        rainbow = true;
        shape = ParticleShape::Square;
        additive = false;
        break;
    }
}
