#include "MapRenderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

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
    constexpr int kMaxCoverCells = 80;       // これ以下の壁の塊は「遮蔽物」（荷馬車・木箱）として描く

    // 場所から決まる擬似乱数（毎回同じ見た目になる）
    unsigned Hash(int x, int z, int salt)
    {
        unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(z) * 19349663u ^ static_cast<unsigned>(salt) * 83492791u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        return h;
    }
    float Rand01(int x, int z, int salt) { return static_cast<float>(Hash(x, z, salt) & 0xFFFFu) / 65535.0f; }

    // 岩山の色（夕日に照らされた赤茶色）
    const Color kRock[3] = { { 0.64f, 0.43f, 0.31f }, { 0.72f, 0.51f, 0.37f }, { 0.56f, 0.38f, 0.29f } };
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
    propPool_.Initialize(common, kCubeName, kMaxPropBlocks);
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
        SetTint(floor_, 0.82f, 0.64f, 0.42f); // 夕方の乾いた土の色
    }

    // 壁・樽。横に続く壁は、1本の細長い立方体にまとめる（広い壁でも立方体の数が少なくてすむ）
    blockPool_.ReleaseAll();
    propPool_.ReleaseAll();
    buildingPool_.ReleaseAll();
    blockItems_.clear();
    buildingItems_.clear();
    propItems_.clear();
    const int W = map.width;
    const int H = map.height;

    // 小さな壁の塊（四角で、外周に接していない）は遮蔽物。荷馬車・木箱の山として描く
    std::vector<char> isCover(static_cast<size_t>(W) * H, 0);
    {
        std::vector<char> seen(static_cast<size_t>(W) * H, 0);
        for (int z0 = 0; z0 < H; ++z0) {
            for (int x0 = 0; x0 < W; ++x0) {
                if (map.Get(x0, z0) != MapData::Wall || seen[static_cast<size_t>(z0) * W + x0]) {
                    continue;
                }
                // 4方向に広げて、この塊に含まれるマスを集める
                std::vector<std::pair<int, int>> stack{ { x0, z0 } };
                std::vector<std::pair<int, int>> cells;
                seen[static_cast<size_t>(z0) * W + x0] = 1;
                int minX = x0, maxX = x0, minZ = z0, maxZ = z0;
                bool border = false;
                while (!stack.empty()) {
                    const auto [cx, cz] = stack.back();
                    stack.pop_back();
                    cells.push_back({ cx, cz });
                    minX = (std::min)(minX, cx); maxX = (std::max)(maxX, cx);
                    minZ = (std::min)(minZ, cz); maxZ = (std::max)(maxZ, cz);
                    if (cx == 0 || cz == 0 || cx == W - 1 || cz == H - 1) {
                        border = true;
                    }
                    const int dx[4] = { 1, -1, 0, 0 };
                    const int dz[4] = { 0, 0, 1, -1 };
                    for (int k = 0; k < 4; ++k) {
                        const int nx = cx + dx[k];
                        const int nz = cz + dz[k];
                        if (map.InRange(nx, nz) && map.Get(nx, nz) == MapData::Wall && !seen[static_cast<size_t>(nz) * W + nx]) {
                            seen[static_cast<size_t>(nz) * W + nx] = 1;
                            stack.push_back({ nx, nz });
                        }
                    }
                }
                const int w = maxX - minX + 1;
                const int d = maxZ - minZ + 1;
                const bool rect = static_cast<int>(cells.size()) == w * d;
                if (!border && rect && static_cast<int>(cells.size()) <= kMaxCoverCells && w <= 12 && d <= 12) {
                    for (const auto& c : cells) {
                        isCover[static_cast<size_t>(c.second) * W + c.first] = 1;
                    }
                    if (!AddCover(minX, minZ, maxX, maxZ)) {
                        complete = false;
                    }
                }
            }
        }
    }

    // 壁の4方向がすべて壁なら「奥の岩」（高く、サボテンも生える）。縁は低いまま
    auto isWallLike = [&](int x, int z) { return map.Get(x, z) == MapData::Wall && !(map.InRange(x, z) && isCover[static_cast<size_t>(z) * W + x]); };
    auto interior = [&](int x, int z) { return isWallLike(x + 1, z) && isWallLike(x - 1, z) && isWallLike(x, z + 1) && isWallLike(x, z - 1); };
    auto shadeOf = [&](int x, int z) { return static_cast<int>(Hash(x >> 3, z >> 2, 1) % 3u); };

    for (int z = 0; z < H; ++z) {
        int x = 0;
        while (x < W) {
            const std::uint8_t cell = map.Get(x, z);
            if (cell == MapData::Empty || cell == MapData::Building) {
                ++x; // 建物は下でまとめて作る
                continue;
            }

            if (cell == MapData::Wall) {
                if (isCover[static_cast<size_t>(z) * W + x]) {
                    ++x; // 遮蔽物は上で作った
                    continue;
                }
                const bool deep = interior(x, z);
                const int shade = shadeOf(x, z);
                int run = 1; // 同じ行・同じ種類・同じ色で壁が続くマス数
                while (x + run < W && map.Get(x + run, z) == MapData::Wall && !isCover[static_cast<size_t>(z) * W + x + run]
                    && interior(x + run, z) == deep && shadeOf(x + run, z) == shade) {
                    ++run;
                }
                const float h = deep ? 1.9f + 1.0f * Rand01(x >> 3, z >> 2, 2) : kWallHeight;
                GameObject* obj = blockPool_.Acquire({ x + run * 0.5f, h * 0.5f, z + 0.5f });
                if (obj) {
                    obj->GetObject3d().GetTransform().Scale = { static_cast<float>(run), h, 1.0f };
                    SetTint(*obj, kRock[shade].r, kRock[shade].g, kRock[shade].b);
                    Track(blockItems_, obj, x + run * 0.5f, z + 0.5f, static_cast<float>(run), 1.0f);
                }
                else {
                    complete = false; // 足りない
                }
                // 奥の岩の上にサボテンを少し生やす
                if (deep) {
                    for (int i = 0; i < run; ++i) {
                        if (Hash(x + i, z, 3) % 60u == 0) {
                            const float cx = x + i + 0.5f;
                            const float cz = z + 0.5f;
                            complete &= AddProp(cx, h + 0.6f, cz, 0.3f, 1.2f, 0.3f, 0.28f, 0.46f, 0.22f);
                            complete &= AddProp(cx + 0.28f, h + 0.75f, cz, 0.26f, 0.12f, 0.2f, 0.28f, 0.46f, 0.22f);
                            complete &= AddProp(cx + 0.38f, h + 0.95f, cz, 0.12f, 0.4f, 0.2f, 0.28f, 0.46f, 0.22f);
                        }
                    }
                }
                x += run;
                continue;
            }

            // 樽（1マスずつ）。たがを巻く
            GameObject* obj = blockPool_.Acquire({ x + 0.5f, kBarrelHeight * 0.5f, z + 0.5f });
            if (obj) {
                obj->GetObject3d().GetTransform().Scale = { 0.7f, kBarrelHeight, 0.7f };
                SetTint(*obj, 0.6f, 0.35f, 0.15f);
                Track(blockItems_, obj, x + 0.5f, z + 0.5f, 0.7f, 0.7f);
            }
            else {
                complete = false;
            }
            complete &= AddProp(x + 0.5f, kBarrelHeight * 0.3f, z + 0.5f, 0.76f, 0.08f, 0.76f, 0.25f, 0.16f, 0.1f);
            complete &= AddProp(x + 0.5f, kBarrelHeight * 0.72f, z + 0.5f, 0.76f, 0.08f, 0.76f, 0.25f, 0.16f, 0.1f);
            ++x;
        }
    }

    // 地面の飾り：土のまだら・草のかたまり（通れる、当たり判定なし）
    for (int z = 0; z < H; ++z) {
        for (int x = 0; x < W; ++x) {
            if (map.Get(x, z) != MapData::Empty) {
                continue;
            }
            const unsigned r = Hash(x, z, 10);
            if (r % 14u == 0u) {
                const bool dark = (r >> 8) % 2u == 0u;
                const float sx = 1.0f + 1.8f * Rand01(x, z, 11);
                const float sz = 0.7f + 1.1f * Rand01(x, z, 12);
                complete &= AddProp(x + Rand01(x, z, 13), 0.015f, z + Rand01(x, z, 14), sx, 0.03f, sz,
                    dark ? 0.72f : 0.90f, dark ? 0.55f : 0.74f, dark ? 0.36f : 0.5f);
            }
            else if (r % 29u == 1u) {
                const float gx = x + 0.3f + 0.4f * Rand01(x, z, 15);
                const float gz = z + 0.3f + 0.4f * Rand01(x, z, 16);
                complete &= AddProp(gx, 0.17f, gz, 0.08f, 0.34f, 0.08f, 0.62f, 0.56f, 0.25f);
                complete &= AddProp(gx + 0.12f, 0.12f, gz + 0.06f, 0.07f, 0.24f, 0.07f, 0.55f, 0.5f, 0.22f);
            }
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
        Track(buildingItems_, obj, px, pz, sx, sz);
    };

    // 地面の影（建物の高さに応じて右側へ伸びる）
    ok &= AddShadow(static_cast<float>(b.x0), b.z0 + 0.3f, static_cast<float>(b.x1 + 1), static_cast<float>(b.z1 + 1), st.height * 1.1f);

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

    // 柱の上のランタン（夕暮れの灯り）
    block(-(W * 0.5f - 0.12f), kPorchHeight + 0.2f, -0.92f, 0.2f, 0.26f, 0.2f, Color{ 1.0f, 0.82f, 0.35f });
    block(W * 0.5f - 0.12f, kPorchHeight + 0.2f, -0.92f, 0.2f, 0.26f, 0.2f, Color{ 1.0f, 0.82f, 0.35f });

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

bool MapRenderer::AddProp(float px, float py, float pz, float sx, float sy, float sz, float r, float g, float b)
{
    GameObject* obj = propPool_.Acquire({ px, py, pz });
    if (!obj) {
        return false;
    }
    obj->GetObject3d().GetTransform().Scale = { sx, sy, sz };
    SetTint(*obj, r, g, b);
    Track(propItems_, obj, px, pz, sx, sz);
    return true;
}

bool MapRenderer::AddShadow(float xa, float za, float xb, float zb, float len)
{
    // 地面すれすれの平たい箱（半透明にはできないので、地面より少し暗い色）
    return AddProp((xa + xb + len) * 0.5f, 0.03f, (za + zb) * 0.5f, (xb - xa) + len, 0.06f, zb - za, 0.52f, 0.40f, 0.28f);
}

bool MapRenderer::AddCover(int x0, int z0, int x1, int z1)
{
    const float w = static_cast<float>(x1 - x0 + 1);
    const float d = static_cast<float>(z1 - z0 + 1);
    const float cx = x0 + w * 0.5f;
    const float cz = z0 + d * 0.5f;
    const bool alongX = w >= d;                    // 長い辺が x 方向か
    const float L = alongX ? w : d;                // 長い辺
    const float S = alongX ? d : w;                // 短い辺
    bool ok = AddShadow(static_cast<float>(x0), z0 + 0.15f, static_cast<float>(x1 + 1), static_cast<float>(z1 + 1), 1.0f);

    // 長い辺に沿った (a: 長辺方向, b: 短辺方向) をワールドの箱へ直す
    auto box = [&](float a, float y, float b, float la, float sy, float lb, float r, float g, float bl) {
        const float px = cx + (alongX ? a : b);
        const float pz = cz + (alongX ? b : a);
        return AddProp(px, y, pz, alongX ? la : lb, sy, alongX ? lb : la, r, g, bl);
    };

    if (L >= 3.0f && S >= 2.0f) {
        // 荷馬車：荷台・車輪・幌
        ok &= box(0.0f, 0.85f, 0.0f, L, 0.5f, S, 0.50f, 0.32f, 0.17f);
        for (int side = -1; side <= 1; side += 2) {
            for (int end = -1; end <= 1; end += 2) {
                ok &= box(end * (L * 0.5f - 0.7f), 0.55f, side * (S * 0.5f + 0.02f), 1.1f, 1.1f, 0.14f, 0.24f, 0.16f, 0.10f);
            }
        }
        if (L >= 4.0f) {
            ok &= box(0.0f, 1.65f, 0.0f, L - 0.6f, 1.0f, S - 0.4f, 0.86f, 0.80f, 0.64f); // 幌
        }
        else {
            ok &= box(-L * 0.2f, 1.45f, 0.0f, 0.9f, 0.7f, (std::min)(0.9f, S - 0.3f), 0.64f, 0.46f, 0.26f); // 荷物
            ok &= box(L * 0.2f, 1.4f, 0.0f, 0.8f, 0.6f, (std::min)(0.8f, S - 0.3f), 0.55f, 0.38f, 0.20f);
        }
    }
    else {
        // 木箱の山
        ok &= box(0.0f, 0.6f, 0.0f, L, 1.2f, S, 0.63f, 0.46f, 0.26f);
        ok &= box(0.0f, 1.25f, 0.0f, L + 0.1f, 0.1f, S + 0.1f, 0.50f, 0.35f, 0.18f);   // ふち
        if (L >= 2.0f && S >= 2.0f) {
            ok &= box(0.0f, 1.65f, 0.0f, L * 0.5f, 0.7f, S * 0.5f, 0.72f, 0.54f, 0.32f); // 上に小さな箱
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

void MapRenderer::SetVisibleRect(float minX, float minZ, float maxX, float maxZ)
{
    visMinX_ = minX;
    visMinZ_ = minZ;
    visMaxX_ = maxX;
    visMaxZ_ = maxZ;
}

void MapRenderer::Track(std::vector<Item>& items, GameObject* obj, float x, float z, float sx, float sz)
{
    Item it;
    it.obj = obj;
    it.x = x;
    it.z = z;
    it.radius = 0.5f * std::sqrt(sx * sx + sz * sz); // 回っていなくても足りる大きさ
    items.push_back(it);
}

void MapRenderer::UpdateItems(std::vector<Item>& items, const Matrix4x4& view, const Matrix4x4& proj)
{
    for (Item& it : items) {
        // 映る範囲の外は何もしない（行列計算も描画もしない）
        it.visible = it.x + it.radius >= visMinX_ && it.x - it.radius <= visMaxX_
            && it.z + it.radius >= visMinZ_ && it.z - it.radius <= visMaxZ_;
        if (it.visible) {
            it.obj->GetObject3d().Update(view, proj);
            ++visibleCount_;
        }
    }
}

void MapRenderer::DrawItems(std::vector<Item>& items)
{
    for (Item& it : items) {
        if (it.visible) {
            it.obj->Draw();
        }
    }
}

void MapRenderer::Update(const Matrix4x4& view, const Matrix4x4& proj)
{
    visibleCount_ = 0;
    floor_.GetObject3d().Update(view, proj);
    UpdateItems(blockItems_, view, proj);
    UpdateItems(buildingItems_, view, proj);
    UpdateItems(propItems_, view, proj);
    for (size_t i = 0; i < gateCount_ && i < MapData::kMaxGates; ++i) {
        if (gateVisible_[i]) {
            gates_[i].GetObject3d().Update(view, proj);
        }
    }
}

void MapRenderer::Draw()
{
    floor_.Draw();
    DrawItems(blockItems_);
    DrawItems(buildingItems_);
    DrawItems(propItems_);
    for (size_t i = 0; i < gateCount_ && i < MapData::kMaxGates; ++i) {
        if (gateVisible_[i]) {
            gates_[i].Draw();
        }
    }
}
