#include "DustBurst.h"

#include <cmath>
#include <cstdlib>

namespace {
    constexpr float kPi = 3.14159265f;
    float Random01() { return static_cast<float>(std::rand() % 1000) * 0.001f; }
}

void DustBurst::Initialize(Object3dCommon* common, const char* modelName)
{
    pool_.Initialize(common, modelName, kMax);
    particles_.clear();
    particles_.reserve(kMax);
}

void DustBurst::Emit(const Vector3& position, int count, float speed, float size, float life, float r, float g, float b)
{
    for (int i = 0; i < count; ++i) {
        GameObject* obj = pool_.Acquire(position);
        if (!obj) {
            return; // 満杯：出さない
        }
        if (Material* m = obj->GetObject3d().GetMaterial()) {
            m->color = Vector4{ r, g, b, 1.0f };
        }
        const float angle = Random01() * 2.0f * kPi;
        const float s = speed * (0.4f + 0.6f * Random01());

        Particle p;
        p.obj = obj;
        p.pos = position;
        p.vel = { std::cos(angle) * s, speed * 0.5f * (0.3f + Random01()), std::sin(angle) * s };
        p.life = life * (0.7f + 0.3f * Random01());
        p.size = size * (0.6f + 0.4f * Random01());
        p.spin = (Random01() - 0.5f) * 6.0f;
        particles_.push_back(p);
    }
}

void DustBurst::Step(float dt)
{
    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.age += dt;
        if (p.age >= p.life) {
            pool_.Release(p.obj);
            particles_[i] = particles_.back(); // 末尾と入れ替えて取り除く
            particles_.pop_back();
            continue;
        }
        // ふわっと広がって、だんだん勢いが落ちる
        const float drag = std::exp(-3.0f * dt);
        p.vel.x *= drag;
        p.vel.y *= drag;
        p.vel.z *= drag;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.pos.z += p.vel.z * dt;

        // 大きさ：出た直後にふくらみ、後半で縮んで消える
        const float t = p.age / p.life;
        const float grow = (t < 0.25f) ? (t / 0.25f) : 1.0f;
        const float fade = (t < 0.5f) ? 1.0f : (1.0f - (t - 0.5f) / 0.5f);
        const float scale = p.size * (0.4f + 0.6f * grow) * fade;

        Transform& tr = p.obj->GetObject3d().GetTransform();
        tr.Translate = p.pos;
        tr.Scale = { scale, scale, scale };
        tr.Rotate.y += p.spin * dt;
        ++i;
    }
}

void DustBurst::Update(const Matrix4x4& view, const Matrix4x4& proj)
{
    pool_.Update(view, proj);
}

void DustBurst::Draw()
{
    pool_.Draw();
}

void DustBurst::Clear()
{
    pool_.ReleaseAll();
    particles_.clear();
}
