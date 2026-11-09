#include "SaloonDoors.h"

#include <cmath>
#include <cstdlib>

namespace {
    constexpr float kLeafHalfWidth = 0.25f;  // 扉1枚の横幅の半分（セルの半分に2枚で閉じる）
    constexpr float kLeafHeight = 1.0f;
    constexpr float kLeafThickness = 0.08f;
    constexpr float kLeafCenterY = 0.85f;
    constexpr float kSpring = 60.0f;         // 閉じようとする強さ
    constexpr float kDamping = 5.0f;         // 揺れが収まる強さ
    constexpr float kKick = 6.0f;            // 誰かが通ったときの勢い（角速度）
    constexpr float kMaxAngle = 1.4f;        // これ以上は開かない（約80度）
}

void SaloonDoors::Initialize(Object3dCommon* common, const char* modelName)
{
    leafPool_.Initialize(common, modelName, kMaxDoors * 2);
    doors_.clear();
    doors_.reserve(kMaxDoors);
}

void SaloonDoors::Build(const MapData& map)
{
    leafPool_.ReleaseAll();
    doors_.clear();

    for (size_t i = 0; i < map.spawns.size() && i < kMaxDoors; ++i) {
        Door d;
        d.cx = map.spawns[i].x + 0.5f;
        d.cz = map.spawns[i].z + 0.5f;
        d.left = leafPool_.Acquire({ d.cx, kLeafCenterY, d.cz });
        d.right = leafPool_.Acquire({ d.cx, kLeafCenterY, d.cz });

        // この出現点が建物の正面の前にあれば、扉の向きをその建物に合わせる
        for (const MapData::BuildingRect& b : map.buildings) {
            if (!MapData::IsInFront(b, map.spawns[i].x, map.spawns[i].z)) {
                continue;
            }
            switch (b.facing) {
            case MapData::FaceNorth: d.lx = 1.0f; d.lz = 0.0f; d.ox = 0.0f;  d.oz = 1.0f;  break;
            case MapData::FaceWest:  d.lx = 0.0f; d.lz = 1.0f; d.ox = -1.0f; d.oz = 0.0f;  break;
            case MapData::FaceEast:  d.lx = 0.0f; d.lz = 1.0f; d.ox = 1.0f;  d.oz = 0.0f;  break;
            default:                 break; // 手前向き（既定）
            }
            break;
        }
        if (!d.left || !d.right) {
            break; // 起こらないはず（枚数は最大の出現点ぶん用意してある）
        }
        for (GameObject* leaf : { d.left, d.right }) {
            leaf->GetObject3d().GetTransform().Scale = { kLeafHalfWidth * 2.0f, kLeafHeight, kLeafThickness };
            if (Material* m = leaf->GetObject3d().GetMaterial()) {
                m->color = Vector4{ 0.5f, 0.3f, 0.13f, 1.0f };
            }
        }
        Apply(d);
        doors_.push_back(d);
    }
}

void SaloonDoors::Open(size_t index)
{
    if (index >= doors_.size()) {
        return;
    }
    // 手前(-z)向きに勢いをつける。ときどき逆向きにも少し揺らす
    doors_[index].velocity += kKick * (0.8f + 0.4f * static_cast<float>(std::rand() % 100) * 0.01f);
}

void SaloonDoors::Step(float dt)
{
    for (Door& d : doors_) {
        // ばね：角度に比例して戻る力 + 速度に比例したブレーキ
        const float accel = -kSpring * d.angle - kDamping * d.velocity;
        d.velocity += accel * dt;
        d.angle += d.velocity * dt;
        if (d.angle > kMaxAngle) {
            d.angle = kMaxAngle;
            d.velocity = 0.0f;
        }
        else if (d.angle < -kMaxAngle) {
            d.angle = -kMaxAngle;
            d.velocity = 0.0f;
        }
        Apply(d);
    }
}

void SaloonDoors::Apply(const Door& d)
{
    // 蝶番はマスの両端 (中心 ∓ 0.5 * 幅方向)。扉は蝶番から中心へ伸び、角度 a だけ外側へ振れる
    //   1枚目の向き = 幅方向 * cos(a) + 外向き * sin(a)、2枚目 = -幅方向 * cos(a) + 外向き * sin(a)
    //   Y回転は、立方体の幅の軸 (cosθ, -sinθ) が扉の向きに重なる θ = atan2(-dz, dx)
    const float c = std::cos(d.angle);
    const float s = std::sin(d.angle);
    const float dir1x = d.lx * c + d.ox * s;
    const float dir1z = d.lz * c + d.oz * s;
    const float dir2x = -d.lx * c + d.ox * s;
    const float dir2z = -d.lz * c + d.oz * s;

    Transform& l = d.left->GetObject3d().GetTransform();
    l.Translate = { d.cx - 0.5f * d.lx + dir1x * kLeafHalfWidth, kLeafCenterY, d.cz - 0.5f * d.lz + dir1z * kLeafHalfWidth };
    l.Rotate = { 0.0f, std::atan2(-dir1z, dir1x), 0.0f };

    Transform& r = d.right->GetObject3d().GetTransform();
    r.Translate = { d.cx + 0.5f * d.lx + dir2x * kLeafHalfWidth, kLeafCenterY, d.cz + 0.5f * d.lz + dir2z * kLeafHalfWidth };
    r.Rotate = { 0.0f, std::atan2(-dir2z, dir2x), 0.0f };
}

void SaloonDoors::Update(const Matrix4x4& view, const Matrix4x4& proj)
{
    leafPool_.Update(view, proj);
}

void SaloonDoors::Draw()
{
    leafPool_.Draw();
}

void SaloonDoors::Reset()
{
    for (Door& d : doors_) {
        d.angle = 0.0f;
        d.velocity = 0.0f;
        Apply(d);
    }
}
