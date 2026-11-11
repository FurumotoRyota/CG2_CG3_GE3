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

    // 3Dモデルの表。file=resources のファイル名、範囲=モデル自身の座標での x,z の広がり（置く・消す判定の大きさに使う）、capacity=同時に置ける数
    //   モデルは原点が足元の真ん中。建物の正面はモデルの +z 側
    struct ModelInfo
    {
        const char* file;
        float minX, maxX, minZ, maxZ;
        size_t capacity;
    };
    const ModelInfo kModels[] = {
        { "ws_saloon_building.obj", -2.7f, 2.7f, -2.1f, 3.45f, 48 },
        { "ws_general_store.obj", -2.2f, 2.2f, -1.7f, 2.5f, 48 },
        { "ws_jail_cell.obj", -1.5f, 1.5f, -1.27f, 1.22f, 64 },
        { "ws_wooden_barrel.obj", -0.41f, 0.41f, -0.43f, 0.43f, 64 },
        { "ws_wooden_crate.obj", -0.42f, 0.42f, -0.4f, 0.43f, 320 },
        { "ws_tnt_crate.obj", -0.45f, 0.45f, -0.3f, 0.32f, 80 },
        { "ws_wagon.obj", -1.3f, 2.7f, -0.9f, 0.9f, 40 },
        { "ws_covered_wagon.obj", -2.05f, 1.3f, -0.76f, 0.76f, 40 },
        { "ws_saguaro_cactus.obj", -0.93f, 1.04f, -0.25f, 0.25f, 100 },
        { "ws_barrel_cactus.obj", -0.44f, 0.42f, -0.45f, 0.44f, 60 },
        { "ws_prickly_pear.obj", -0.57f, 0.58f, -0.07f, 0.11f, 80 },
        { "ws_dry_bush.obj", -0.44f, 0.5f, -0.49f, 0.49f, 160 },
        { "ws_bull_skull.obj", -0.75f, 0.75f, -0.28f, 0.45f, 40 },
        { "ws_dead_tree.obj", -0.75f, 0.88f, -0.2f, 0.2f, 40 },
        { "ws_hitching_post.obj", -0.95f, 0.95f, -0.06f, 0.06f, 24 },
        { "ws_wanted_board.obj", -0.5f, 0.5f, -0.05f, 0.05f, 12 },
        { "ws_water_tower.obj", -1.24f, 1.24f, -1.3f, 1.3f, 12 },
        { "ws_windmill_pump.obj", -0.85f, 1.3f, -0.51f, 0.41f, 24 },
        { "ws_mine_cart.obj", -1.0f, 1.0f, -0.46f, 0.46f, 24 },
        { "ws_cattle_fence.obj", -1.5f, 1.5f, -0.07f, 0.08f, 60 },
        { "ws_desert_rock_arch.obj", -1.91f, 1.65f, -0.59f, 0.68f, 24 },
        { "ws_mesa_rock.obj", -1.8f, 1.8f, -1.8f, 1.8f, 60 },
        { "ws_tumbleweed.obj", -0.38f, 0.33f, -0.33f, 0.38f, 100 },
        { "ws_ribcage_bones.obj", -0.8f, 0.8f, -0.44f, 0.44f, 20 },
        { "ws_campfire_pot.obj", -0.5f, 0.75f, -0.65f, 0.65f, 16 },
        { "ws_cowboy_hat.obj", -0.5f, 0.5f, -0.5f, 0.5f, 16 },
        { "ws_gold_nugget_pile.obj", -0.35f, 0.44f, -0.28f, 0.31f, 16 },
        { "ws_horse_saddle.obj", -0.3f, 0.3f, -0.47f, 0.45f, 16 },
    };
    static_assert(sizeof(kModels) / sizeof(kModels[0]) == 28, "ModelId と kModels の数が合わない");

    // 建物の種類ごとに使うモデル。width=モデルの正面の幅、backZ/frontZ=建物の本体の奥と正面（モデルの z。庇はその先）
    struct BuildingModel
    {
        int model;
        float width, backZ, frontZ, height;
    };
    constexpr float kPi = 3.14159265f;

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
    for (int i = 0; i < kModelCount; ++i) {
        modelPools_[i].Initialize(common, kModels[i].file, kModels[i].capacity);
    }
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
    for (GameObjectPool& p : modelPools_) {
        p.ReleaseAll();
    }
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
                            complete &= AddModel(kMSaguaro, cx, h, cz, Rand01(x + i, z, 31) * kPi * 2.0f, 0.45f, 0.45f, 0.45f);
                        }
                        else if (Hash(x + i, z, 4) % 70u == 0) {
                            AddModel(kMMesa, x + i + 0.5f, h - 0.1f, z + 0.5f, Rand01(x + i, z, 32) * kPi * 2.0f, 0.5f, 0.5f, 0.5f); // 岩の上の小さな岩山
                        }
                        else if (Hash(x + i, z, 5) % 110u == 0) {
                            AddModel(kMArch, x + i + 0.5f, h - 0.05f, z + 0.5f, (Hash(x + i, z, 6) % 2u) * kPi * 0.5f, 0.7f, 0.7f, 0.7f); // 岩のアーチ
                        }
                        else if (Hash(x + i, z, 7) % 160u == 0) {
                            AddModel(kMWindmill, x + i + 0.5f, h, z + 0.5f, Rand01(x + i, z, 33) * kPi * 2.0f, 0.7f, 0.7f, 0.7f); // 風車
                        }
                    }
                }
                x += run;
                continue;
            }

            // 樽（1マスずつ）
            complete &= AddModel(kMBarrel, x + 0.5f, 0.0f, z + 0.5f, Rand01(x, z, 30) * kPi * 2.0f, 1.1f, 1.1f, 1.1f);
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
                // 草のかたまりの代わりに、低木・ウチワサボテン・たる型サボテン・頭骨・枯れ木を散らす（足りなければ置かない）
                const float gx = x + 0.3f + 0.4f * Rand01(x, z, 15);
                const float gz = z + 0.3f + 0.4f * Rand01(x, z, 16);
                const unsigned pick = (r >> 10) % 20u;
                const float yaw = Rand01(x, z, 17) * kPi * 2.0f;
                const float sc = 0.8f + 0.5f * Rand01(x, z, 18);
                if (pick < 11u) {
                    AddModel(kMDryBush, gx, 0.0f, gz, yaw, sc, sc, sc);
                }
                else if (pick < 15u) {
                    AddModel(kMPricklyPear, gx, 0.0f, gz, yaw, sc, sc, sc);
                }
                else if (pick < 18u) {
                    AddModel(kMBarrelCactus, gx, 0.0f, gz, yaw, sc * 0.8f, sc * 0.8f, sc * 0.8f);
                }
                else if (pick < 19u) {
                    AddModel(kMSkull, gx, 0.0f, gz, yaw, 0.9f, 0.9f, 0.9f);
                }
                else {
                    AddModel(kMDeadTree, gx, 0.0f, gz, yaw, 0.7f, 0.7f, 0.7f);
                }
            }
            else {
                // まれに置く大きめの飾り。通り道をふさがないよう、まわり2マスが空いている場所だけ
                auto clear = [&](int radius) {
                    for (int dz = -radius; dz <= radius; ++dz) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            if (map.Get(x + dx, z + dz) != MapData::Empty) {
                                return false;
                            }
                        }
                    }
                    return true;
                };
                const float gx = x + 0.5f;
                const float gz = z + 0.5f;
                const float yaw = Rand01(x, z, 40) * kPi * 2.0f;
                if (r % 97u == 2u) {
                    const float sc = 0.9f + 0.4f * Rand01(x, z, 41);
                    AddModel(kMTumbleweed, gx, 0.0f, gz, yaw, sc, sc, sc);
                }
                else if (r % 409u == 5u && clear(2)) {
                    AddModel(kMCampfire, gx, 0.0f, gz, yaw, 1.0f, 1.0f, 1.0f);
                }
                else if (r % 331u == 7u && clear(2)) {
                    AddModel(kMMineCart, gx, 0.0f, gz, (Hash(x, z, 42) % 2u) * kPi * 0.5f, 1.0f, 1.0f, 1.0f);
                }
                else if (r % 463u == 11u) {
                    AddModel(kMRibcage, gx, 0.0f, gz, yaw, 1.0f, 1.0f, 1.0f);
                }
                else if (r % 521u == 13u) {
                    AddModel(kMGold, gx, 0.0f, gz, yaw, 1.0f, 1.0f, 1.0f);
                }
                else if (r % 577u == 17u) {
                    AddModel(kMSaddle, gx, 0.0f, gz, yaw, 1.0f, 1.0f, 1.0f);
                }
                else if (r % 83u == 19u && (map.Get(x, z + 1) != MapData::Empty || map.Get(x, z - 1) != MapData::Empty) && map.Get(x + 1, z) == MapData::Empty && map.Get(x - 1, z) == MapData::Empty) {
                    AddModel(kMFence, gx, 0.0f, gz, 0.0f, 1.0f, 1.0f, 1.0f); // 壁ぎわの柵
                }
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

    // 3Dモデルの建物。正面の幅に合わせて横に並べ、奥行きにも並べる（建物の正面は、モデルの +z 側）
    {
        static const BuildingModel kBuildingModels[MapData::KindCount] = {
            { kMSaloon, 5.4f, -2.1f, 2.12f, 4.4f },   // 酒場
            { kMJail, 3.0f, -1.27f, 1.22f, 2.48f },   // 保安官事務所
            { kMStore, 4.4f, -1.7f, 1.7f, 4.0f },     // 雑貨店
        };
        const BuildingModel& bm = kBuildingModels[b.kind >= 0 && b.kind < MapData::KindCount ? b.kind : 0];

        // 正面の向き（外へ向かう向き f）と、正面に沿った横の向き uv、正面の辺の真ん中 edge
        float fx = 0.0f, fz = -1.0f, ux = 1.0f, uz = 0.0f, ex = centerX, ez = static_cast<float>(b.z0);
        switch (b.facing) {
        case MapData::FaceNorth: fx = 0.0f;  fz = 1.0f;  ux = -1.0f; uz = 0.0f;  ex = centerX; ez = b.z1 + 1.0f; break;
        case MapData::FaceWest:  fx = -1.0f; fz = 0.0f;  ux = 0.0f;  uz = -1.0f; ex = static_cast<float>(b.x0); ez = centerZ; break;
        case MapData::FaceEast:  fx = 1.0f;  fz = 0.0f;  ux = 0.0f;  uz = 1.0f;  ex = b.x1 + 1.0f; ez = centerZ; break;
        default: break;
        }
        const float yaw = std::atan2(fx, fz); // モデルの +z を、外へ向かう向きに回す

        // 1棟につきモデル1つ。幅は建物の正面いっぱいに伸ばし、奥行きは庇が通りへ出すぎない程度まで伸ばす。
        // 奥の残りは、箱で埋める（つなげて並べると、建物が連結して見えるため、並べない）
        const float bodyDepth = bm.frontZ - bm.backZ;
        const float sx = W / bm.width;
        const float sz = (std::min)(D / bodyDepth, 1.7f);
        const float sy = 1.35f;
        ok &= AddModel(static_cast<ModelId>(bm.model), ex - fx * (bm.frontZ * sz), 0.0f, ez - fz * (bm.frontZ * sz), yaw, sx, sy, sz);
        const float modelBack = bodyDepth * sz; // 正面から、モデルの本体の奥までの距離
        if (D - modelBack > 0.2f) {
            const float h = bm.height * sy * 0.97f;
            const float vMid = (modelBack - 0.1f + D) * 0.5f;
            block(0.0f, h * 0.5f, vMid, W, h, D - modelBack + 0.1f, st.body);
            block(0.0f, h + 0.08f, vMid, W + 0.2f, 0.16f, D - modelBack + 0.3f, st.roof);
        }

        // 酒場の前に、馬をつなぐ柵。保安官事務所の前に、お尋ね者の掲示板
        auto front = [&](float u, float out, ModelId id, float scale, float py = 0.0f) {
            ok &= AddModel(id, ex + ux * u + fx * out, py, ez + uz * u + fz * out, yaw, scale, scale, scale);
        };
        // 屋根の上（建物の奥寄り）に置く。v=正面から奥への距離
        auto roof = [&](float u, float v, ModelId id, float scale, float roofHeight) {
            ok &= AddModel(id, ex + ux * u - fx * v, roofHeight, ez + uz * u - fz * v, yaw, scale, scale, scale);
        };
        const float roofH = bm.height * sy;
        if (b.kind == MapData::Saloon && W >= 7.0f) {
            front(-W * 0.3f, 2.6f, kMHitchingPost, 1.0f);
            front(W * 0.3f, 2.6f, kMHitchingPost, 1.0f);
            front(-W * 0.3f, 2.6f, kMSaddle, 0.7f, 0.5f);          // 柵に掛けた鞍
            front(W * 0.5f - 1.3f, 1.0f, kMBarrel, 1.1f);          // 入口の両わきの樽と木箱
            front(-(W * 0.5f - 1.3f), 1.0f, kMBarrel, 1.1f);
            front(-(W * 0.5f - 1.3f), 1.0f, kMHat, 0.6f, 1.0f);    // 樽の上のカウボーイハット
            front(W * 0.5f - 2.4f, 0.9f, kMCrate, 1.1f);
            front(W * 0.5f - 2.4f, 0.9f, kMCrate, 0.9f, 0.95f);
        }
        else if (b.kind == MapData::Sheriff && W >= 7.0f) {
            front(W * 0.3f, 1.4f, kMWantedBoard, 1.0f);
            front(W * 0.3f - 1.4f, 1.3f, kMGold, 1.0f);            // 押収した金塊
            front(-W * 0.3f, 1.6f, kMHitchingPost, 1.0f);
            front(-W * 0.3f - 1.6f, 1.3f, kMTnt, 1.1f);
            roof(W * 0.28f, D * 0.7f, kMWindmill, 0.7f, roofH);   // 屋根の上の風車
        }
        else if (b.kind == MapData::Store && W >= 7.0f) {
            front(-W * 0.25f, 1.3f, kMCrate, 1.1f);
            front(-W * 0.25f, 1.3f, kMCrate, 0.95f, 0.92f);
            front(W * 0.25f, 1.2f, kMBarrel, 1.1f);
            front(W * 0.25f + 1.2f, 1.2f, kMBarrel, 1.1f);
            roof(-W * 0.25f, D * 0.7f, kMWaterTower, 0.7f, roofH); // 屋根の上の給水塔
        }
    }
    return ok;
}

bool MapRenderer::AddModel(ModelId id, float px, float py, float pz, float yaw, float sx, float sy, float sz)
{
    GameObject* obj = modelPools_[id].Acquire({ px, py, pz });
    if (!obj) {
        return false;
    }
    Transform& t = obj->GetObject3d().GetTransform();
    t.Scale = { sx, sy, sz };
    t.Rotate = { 0.0f, yaw, 0.0f };
    const ModelInfo& m = kModels[id];
    // 画面の外かどうかの判定用の大きさ。原点が真ん中とは限らないので、余裕を足す
    Track(propItems_, obj, px, pz, (m.maxX - m.minX) * sx + 2.0f, (m.maxZ - m.minZ) * sz + 2.0f);
    return true;
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

    if (L >= 3.0f && S >= 2.0f) {
        // 荷馬車（幌つきか、ふつうのもの）。長い辺に沿って並べる
        const bool covered = (Hash(x0, z0, 21) % 2u) == 0u;
        const ModelId id = covered ? kMCoveredWagon : kMWagon;
        const float len = covered ? 3.35f : 4.0f;
        const float center = covered ? -0.375f : 0.7f; // モデルの x の真ん中（原点とのずれ）
        const int n = (std::max)(1, static_cast<int>(std::lround(L / (len + 0.3f))));
        const float sLen = (std::min)((std::max)(L / (static_cast<float>(n) * len), 0.6f), 1.25f);
        const float sWid = (std::min)((std::max)(S / 1.8f, 0.8f), 1.3f);
        const float sy = (sLen + sWid) * 0.5f;
        for (int i = 0; i < n; ++i) {
            const float a = -L * 0.5f + (static_cast<float>(i) + 0.5f) * L / static_cast<float>(n);
            if (alongX) {
                ok &= AddModel(id, cx + a - center * sLen, 0.0f, cz, 0.0f, sLen, sy, sWid);
            }
            else {
                // yaw=90度のとき、モデルの +x はワールドの -z を向く
                ok &= AddModel(id, cx, 0.0f, cz + a + center * sLen, kPi * 0.5f, sLen, sy, sWid);
            }
        }
    }
    else {
        // 木箱の山：1マスに1つ。ときどき上にもう1つ積む。火薬箱が混ざる
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const bool tnt = Hash(x, z, 22) % 6u == 0u;
                const float yaw = Rand01(x, z, 23) * kPi * 2.0f;
                ok &= AddModel(tnt ? kMTnt : kMCrate, x + 0.5f, 0.0f, z + 0.5f, yaw, 1.15f, 1.15f, 1.15f);
                if (Hash(x, z, 24) % 3u == 0u) {
                    ok &= AddModel(kMCrate, x + 0.5f, 0.92f, z + 0.5f, Rand01(x, z, 25) * kPi * 2.0f, 1.0f, 1.0f, 1.0f);
                }
            }
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
