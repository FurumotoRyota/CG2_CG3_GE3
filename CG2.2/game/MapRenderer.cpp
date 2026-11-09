#include "MapRenderer.h"

#include <algorithm>
#include <cassert>

#include "../game2d/CubeMesh.h"
#include "2d/ColorBatch.h"
#include "3d/ModelManager.h"
#include "3d/Object3dCommon.h"

namespace {
    void SetTint(GameObject& obj, float r, float g, float b)
    {
        if (Material* m = obj.GetObject3d().GetMaterial()) {
            m->color = Vector4{ r, g, b, 1.0f };
        }
    }

    struct Color { float r, g, b; };

    // 建物の種類ごとの色
    struct BuildingStyle
    {
        float height;      // 壁の高さ
        Color body;        // 壁
        Color roof;        // 屋根・庇
        Color sign;        // 看板
        Color window;      // 窓
    };
    const BuildingStyle kStyles[MapData::KindCount] = {
        { 3.2f, { 0.55f, 0.33f, 0.18f }, { 0.33f, 0.19f, 0.10f }, { 0.80f, 0.20f, 0.15f }, { 0.95f, 0.78f, 0.35f } }, // 酒場：木の壁・赤い看板・灯りの窓
        { 2.8f, { 0.72f, 0.63f, 0.45f }, { 0.40f, 0.30f, 0.20f }, { 0.90f, 0.75f, 0.20f }, { 0.20f, 0.17f, 0.14f } }, // 保安官事務所：土の壁・金の看板
        { 3.0f, { 0.36f, 0.50f, 0.56f }, { 0.25f, 0.30f, 0.35f }, { 0.88f, 0.86f, 0.78f }, { 0.20f, 0.17f, 0.14f } }, // 雑貨店：青灰色の壁・白っぽい看板
    };

    constexpr float kPorchHeight = 2.15f;    // 正面の庇の高さ
}

void MapRenderer::Initialize(Object3dCommon* common, ModelManager* models)
{
    assert(common && models);

    // 単色の立方体モデルを登録する（palette.png の白のマスだけを使う）。同じ名前はすでにあればそれを使う
    float u = 0.0f;
    float v = 0.0f;
    ColorBatch::ColorToUv(ColorRGB{ 1.0f, 1.0f, 1.0f }, u, v);
    models->Register(kCubeName, CubeMesh::MakeCube("resources/palette.png", u, v));

    blockPool_.Initialize(common, kCubeName, kMaxBlocks);
    buildingPool_.Initialize(common, kCubeName, kMaxBuildingBlocks);
    floor_.Initialize(common, kCubeName, { 0.0f, 0.0f, 0.0f });
    for (GameObject& g : gates_) {
        g.Initialize(common, kCubeName, { 0.0f, 0.0f, 0.0f });
        SetTint(g, 1.0f, 0.85f, 0.2f);
    }
}

bool MapRenderer::Build(const MapData& map)
{
    bool complete = true;

    // 地面：マップの大きさの平たい立方体
    {
        Transform& t = floor_.GetObject3d().GetTransform();
        t.Scale = { static_cast<float>(map.width), 0.1f, static_cast<float>(map.height) };
        t.Translate = { map.width * 0.5f, -0.05f, map.height * 0.5f };
        SetTint(floor_, 0.78f, 0.68f, 0.48f);
    }

    // 壁・樽。横に続く壁は、1本の細長い立方体にまとめる（広い壁でも立方体の数が少なくてすむ）
    blockPool_.ReleaseAll();
    for (int z = 0; z < map.height; ++z) {
        int x = 0;
        while (x < map.width) {
            const std::uint8_t cell = map.Get(x, z);
            if (cell == MapData::Empty || cell == MapData::Building) {
                ++x; // 建物は下でまとめて作る
                continue;
            }

            if (cell == MapData::Wall) {
                int run = 1; // 同じ行で壁が続くマス数
                while (x + run < map.width && map.Get(x + run, z) == MapData::Wall) {
                    ++run;
                }
                GameObject* obj = blockPool_.Acquire({ x + run * 0.5f, kWallHeight * 0.5f, z + 0.5f });
                if (obj) {
                    obj->GetObject3d().GetTransform().Scale = { static_cast<float>(run), kWallHeight, 1.0f };
                    SetTint(*obj, 0.55f, 0.5f, 0.45f);
                }
                else {
                    complete = false; // 足りない
                }
                x += run;
                continue;
            }

            // 樽（1マスずつ）
            GameObject* obj = blockPool_.Acquire({ x + 0.5f, kBarrelHeight * 0.5f, z + 0.5f });
            if (obj) {
                obj->GetObject3d().GetTransform().Scale = { 0.7f, kBarrelHeight, 0.7f };
                SetTint(*obj, 0.6f, 0.35f, 0.15f);
            }
            else {
                complete = false;
            }
            ++x;
        }
    }

    // 建物
    buildingPool_.ReleaseAll();
    for (const MapData::BuildingRect& b : map.buildings) {
        if (!AddBuilding(b)) {
            complete = false;
        }
    }

    // 区切りの柵：範囲いっぱいの低い箱
    gateCount_ = map.gates.size();
    for (size_t i = 0; i < gateCount_ && i < MapData::kMaxGates; ++i) {
        const MapData::Gate& g = map.gates[i];
        Transform& t = gates_[i].GetObject3d().GetTransform();
        const float w = static_cast<float>(g.x1 - g.x0 + 1);
        const float d = static_cast<float>(g.z1 - g.z0 + 1);
        t.Scale = { w, 1.0f, d };
        t.Translate = { g.x0 + w * 0.5f, 0.5f, g.z0 + d * 0.5f };
        gateVisible_[i] = true;
    }
    return complete;
}

bool MapRenderer::AddBuilding(const MapData::BuildingRect& b)
{
    const BuildingStyle& st = kStyles[b.kind >= 0 && b.kind < MapData::KindCount ? b.kind : 0];
    const float sizeX = static_cast<float>(b.x1 - b.x0 + 1);
    const float sizeZ = static_cast<float>(b.z1 - b.z0 + 1);
    const bool alongX = b.facing == MapData::FaceSouth || b.facing == MapData::FaceNorth; // 正面の幅が x 方向か
    const float W = alongX ? sizeX : sizeZ;   // 正面の幅
    const float D = alongX ? sizeZ : sizeX;   // 奥行き
    const float centerX = b.x0 + sizeX * 0.5f;
    const float centerZ = b.z0 + sizeZ * 0.5f;
    bool ok = true;

    // 立方体を1つ置く。u=正面の幅方向（中心が0）、v=正面の面から建物の中へ測った距離（外側は負）。
    // 向きごとにワールド座標へ直す（正面が南なら u=+x、v=+z。北は逆向き。西・東は x と z を入れ替える）
    auto block = [&](float u, float py, float v, float su, float sy, float sv, const Color& c) {
        float px = 0.0f;
        float pz = 0.0f;
        float sx = su;
        float sz = sv;
        switch (b.facing) {
        case MapData::FaceNorth: px = centerX - u;  pz = (b.z1 + 1.0f) - v; break;
        case MapData::FaceWest:  px = b.x0 + v;     pz = centerZ - u;       sx = sv; sz = su; break;
        case MapData::FaceEast:  px = (b.x1 + 1.0f) - v; pz = centerZ + u;  sx = sv; sz = su; break;
        default:                 px = centerX + u;  pz = b.z0 + v;          break;
        }
        GameObject* obj = buildingPool_.Acquire({ px, py, pz });
        if (!obj) {
            ok = false;
            return;
        }
        obj->GetObject3d().GetTransform().Scale = { sx, sy, sz };
        SetTint(*obj, c.r, c.g, c.b);
    };

    // 壁（箱）と屋根
    block(0.0f, st.height * 0.5f, D * 0.5f, W, st.height, D, st.body);
    block(0.0f, st.height + 0.1f, D * 0.5f, W + 0.3f, 0.2f, D + 0.3f, st.roof);

    // 酒場は、屋根の前に背の高い飾り板（西部劇の建物の正面）を立てる
    if (b.kind == MapData::Saloon) {
        block(0.0f, st.height + 0.2f + 0.45f, 0.15f, W, 0.9f, 0.3f, st.body);
        block(0.0f, st.height + 0.2f + 0.9f + 0.05f, 0.15f, W + 0.2f, 0.1f, 0.4f, st.roof);
    }

    // 正面の庇（1マスぶん張り出す）と、両端の柱
    block(0.0f, kPorchHeight, -0.5f, W, 0.12f, 1.0f, st.roof);
    block(-(W * 0.5f - 0.12f), kPorchHeight * 0.5f, -0.92f, 0.14f, kPorchHeight, 0.14f, st.roof);
    block(W * 0.5f - 0.12f, kPorchHeight * 0.5f, -0.92f, 0.14f, kPorchHeight, 0.14f, st.roof);

    // 看板（庇の上）
    const float signW = (std::min)((std::max)(W - 1.0f, 1.0f), 5.0f);
    block(0.0f, kPorchHeight + 0.45f, -0.06f, signW, 0.5f, 0.12f, st.sign);

    // 窓（3マスごと。幅が4マス以上のとき）
    if (W >= 4.0f) {
        for (float u = -W * 0.5f + 1.5f; u < W * 0.5f; u += 3.0f) {
            block(u, 1.3f, -0.03f, 0.8f, 0.7f, 0.06f, st.window);
        }
    }
    return ok;
}

void MapRenderer::SetGateVisible(size_t index, bool visible)
{
    if (index < gateCount_ && index < MapData::kMaxGates) {
        gateVisible_[index] = visible;
    }
}

void MapRenderer::Update(const Matrix4x4& view, const Matrix4x4& proj)
{
    floor_.GetObject3d().Update(view, proj);
    blockPool_.Update(view, proj);
    buildingPool_.Update(view, proj);
    for (size_t i = 0; i < gateCount_ && i < MapData::kMaxGates; ++i) {
        if (gateVisible_[i]) {
            gates_[i].GetObject3d().Update(view, proj);
        }
    }
}

void MapRenderer::Draw()
{
    floor_.Draw();
    blockPool_.Draw();
    buildingPool_.Draw();
    for (size_t i = 0; i < gateCount_ && i < MapData::kMaxGates; ++i) {
        if (gateVisible_[i]) {
            gates_[i].Draw();
        }
    }
}
