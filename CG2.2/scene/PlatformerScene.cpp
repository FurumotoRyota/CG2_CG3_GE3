#include "PlatformerScene.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "base/DirectXCommon.h"
#include "base/WinApp.h"
#include "2d/SpriteCommon.h"
#include "3d/EnvironmentMap.h"
#include "3d/Model.h"
#include "3d/ModelManager.h"
#include "3d/Object3dCommon.h"
#include "3d/ShadowMap.h"
#include "editor/EditorLayout.h"
#include "input/Input.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

namespace
{
    constexpr const char* kStagePath = "resources/stage.csv";
    constexpr const char* kSettingPath = "resources/PlatformerSetting.bin";
    constexpr float kPi = 3.14159265f;
    constexpr float kDegToRad = kPi / 180.0f;
    constexpr float kFovY = 0.6f;          // カメラの縦の視野角[rad]
    constexpr float kCharacterZ = -0.1f;   // キャラクターの奥行き位置（地形より手前）
    constexpr int kParticleCubes = 96;

    // 地形メッシュのモデル名（作り直すたびに新しい名前にして、GPUが使っている古いモデルを消さない）
    std::string NextTerrainName(const char* tag)
    {
        static int counter = 0;
        return std::string("pf_terrain_") + tag + "_" + std::to_string(counter++);
    }

    // 立方体の1面（外から見て時計回りの2三角形）。center は面の中心、n は法線、up は面の上方向
    void AddFace(MeshData& mesh, const Vector3& center, const Vector3& n, const Vector3& up, float u, float v)
    {
        const Vector3 fwd{ -n.x, -n.y, -n.z };
        // right = up x forward（左手系）
        const Vector3 right{ up.y * fwd.z - up.z * fwd.y, up.z * fwd.x - up.x * fwd.z, up.x * fwd.y - up.y * fwd.x };
        auto corner = [&](float a, float b) {
            return Vector3{ center.x + right.x * a * 0.5f + up.x * b * 0.5f,
                            center.y + right.y * a * 0.5f + up.y * b * 0.5f,
                            center.z + right.z * a * 0.5f + up.z * b * 0.5f };
            };
        const Vector3 tl = corner(-1.0f, 1.0f);
        const Vector3 tr = corner(1.0f, 1.0f);
        const Vector3 br = corner(1.0f, -1.0f);
        const Vector3 bl = corner(-1.0f, -1.0f);
        auto push = [&](const Vector3& p) {
            VertexData vd{};
            vd.position = { p.x, p.y, p.z, 1.0f };
            vd.texcoord = { u, v }; // 1つの色に固定（パレットの1マス）
            vd.normal = n;
            mesh.vertices.push_back(vd);
            };
        push(tl); push(tr); push(br);
        push(tl); push(br); push(bl);
    }

    ModelData MakeCubeModel()
    {
        ModelData data;
        data.materials["m"].textureFilePath = "resources/palette.png";
        MeshData mesh;
        mesh.materialName = "m";
        const Vector3 zero{ 0, 0, 0 };
        AddFace(mesh, { 0.5f, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, 0.5f, 0.5f);
        AddFace(mesh, { -0.5f, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, 0.5f, 0.5f);
        AddFace(mesh, { 0, 0.5f, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, 0.5f, 0.5f);
        AddFace(mesh, { 0, -0.5f, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, 0.5f, 0.5f);
        AddFace(mesh, { 0, 0, 0.5f }, { 0, 0, 1 }, { 0, 1, 0 }, 0.5f, 0.5f);
        AddFace(mesh, { 0, 0, -0.5f }, { 0, 0, -1 }, { 0, 1, 0 }, 0.5f, 0.5f);
        (void)zero;
        data.meshes.push_back(std::move(mesh));
        return data;
    }
}

std::unique_ptr<Object3d> PlatformerScene::MakeObject(const std::string& modelName)
{
    auto object = std::make_unique<Object3d>();
    object->Initialize(ctx_->object3dCommon, modelName);
    object->GetMaterial()->alphaCutoff = 0.0f;
    object->SetExternalTexture(ctx_->textureManager->GetSrvHandleGPU(paletteHandle_)); // パレットで単色にする
    return object;
}

// UVを0倍にして、パレットの1マスだけを読ませる（どのモデルでも単色になる）
void PlatformerScene::SetFlatColor(Object3d& object, float r, float g, float b)
{
    float u = 0.0f;
    float v = 0.0f;
    ColorBatch::ColorToUv(ColorRGB{ r, g, b }, u, v);
    Transform& uv = object.GetUvTransform();
    uv.Scale = { 0.0f, 0.0f, 1.0f };
    uv.Rotate = { 0.0f, 0.0f, 0.0f };
    uv.Translate = { u, v, 0.0f };
}

void PlatformerScene::PlaceFit(Object3d& object, pf::Vec2 pos, float z, float size, float yaw, float extraScale)
{
    const Model* model = object.GetModel();
    Vector3 bmin{ -0.5f, -0.5f, -0.5f };
    Vector3 bmax{ 0.5f, 0.5f, 0.5f };
    if (model) {
        bmin = model->GetBoundsMin();
        bmax = model->GetBoundsMax();
    }
    const float extent = (std::max)({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 0.0001f });
    const float s = size / extent * extraScale;
    // モデルの中心が (pos, z) に来るよう、回転後の中心のずれを引く
    const Vector3 c{ (bmin.x + bmax.x) * 0.5f * s, (bmin.y + bmax.y) * 0.5f * s, (bmin.z + bmax.z) * 0.5f * s };
    const float cs = std::cos(yaw);
    const float sn = std::sin(yaw);
    const Vector3 rc{ c.x * cs + c.z * sn, c.y, -c.x * sn + c.z * cs };
    Transform& t = object.GetTransform();
    t.Scale = { s, s, s };
    t.Rotate = { 0.0f, yaw, 0.0f };
    t.Translate = { pos.x - rc.x, pos.y - rc.y, z - rc.z };
}

// 立方体モデル（中心原点・1辺1）を箱として置く
void PlatformerScene::PlaceBox(Object3d& object, pf::Vec2 pos, float z, float sx, float sy, float sz, float yaw)
{
    Transform& t = object.GetTransform();
    t.Scale = { sx, sy, sz };
    t.Rotate = { 0.0f, yaw, 0.0f };
    t.Translate = { pos.x, pos.y, z };
}

//==================================================
// 初期化・終了
//==================================================
void PlatformerScene::Initialize()
{
    hud_.Initialize(ctx_->spriteCommon);
    paletteHandle_ = ctx_->textureManager->Load("resources/palette.png");

    // 共有の立方体モデル（初回だけ登録される）
    ctx_->modelManager->Register("pf_cube", MakeCubeModel());

    // 設定とステージ（CSVがあればそれを、無ければ組み込みのステージ）
    {
        std::ifstream file(kSettingPath, std::ios::binary);
        pf::Settings loaded;
        if (file.read(reinterpret_cast<char*>(&loaded), sizeof(loaded)) && loaded.version == 2) {
            game_.settings = loaded;
        }
    }
    if (!game_.LoadStageCsv(kStagePath)) {
        game_.LoadDefaultStage();
    }

    // キャラクター・ゴール・火花
    player_ = MakeObject("suzanne.obj");
    goalPole_ = MakeObject("pf_cube");
    goalFlag_ = MakeObject("pf_cube");
    SetFlatColor(*goalPole_, 0.85f, 0.85f, 0.85f);
    SetFlatColor(*goalFlag_, 1.0f, 0.8f, 0.1f);
    for (int i = 0; i < kParticleCubes; ++i) {
        auto cube = MakeObject("pf_cube");
        cube->SetCastShadow(false);
        particleCubes_.push_back(std::move(cube));
    }

    // 光（平行光源）。ほかのシーンで置かれたポイントライトなどは消しておく
    light_ = ctx_->object3dCommon->GetDirectionalLight();
    light_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    light_->direction = { -0.35f, -0.85f, 0.45f };
    light_->intensity = 1.0f;
    light_->lightCount = 0;
    light_->shadowEnabled = shadowEnabled_ ? 1 : 0;
    light_->shadowBias = 0.002f;
    light_->shadowStrength = 0.6f;

    camera_.SetPerspective(kFovY, static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight, 0.1f, 300.0f);

    RebuildTerrain();
    builtMapVersion_ = game_.mapVersion;
    builtResetCount_ = game_.resetCount;
    game_.SetView(ppu_, static_cast<float>(WinApp::kGameWidth), static_cast<float>(WinApp::kGameHeight));
    game_.Reset();
}

void PlatformerScene::Finalize()
{
    drawList_.clear();
    enemyViews_.clear();
    particleCubes_.clear();
    terrainTop_.reset();
    terrainSide_.reset();
    player_.reset();
    goalPole_.reset();
    goalFlag_.reset();
    hud_.Finalize();
    light_ = nullptr;
}

//==================================================
// 地形メッシュ
//==================================================
void PlatformerScene::RebuildTerrain()
{
    ModelData top;
    ModelData side;
    for (ModelData* d : { &top, &side }) {
        d->materials["m"].textureFilePath = "resources/palette.png";
        d->meshes.emplace_back();
        d->meshes.back().materialName = "m";
    }
    MeshData& topMesh = top.meshes[0];
    MeshData& sideMesh = side.meshes[0];

    auto flat = [](float r, float g, float b, float& u, float& v) { ColorBatch::ColorToUv(ColorRGB{ r, g, b }, u, v); };

    for (int iy = 0; iy < pf::Game::kH; ++iy) {
        for (int ix = 0; ix < pf::Game::kW; ++ix) {
            if (!game_.IsBlockIdx(ix, iy)) {
                continue;
            }
            const float cx = static_cast<float>(ix);
            const float cy = static_cast<float>(pf::Game::kH - 1 - iy);
            const float cz = 0.5f; // 地形は奥行き 0..1（キャラクターより奥）
            const float shade = ((ix + iy) & 1) ? 0.0f : 0.07f; // 市松模様

            float u = 0.0f;
            float v = 0.0f;
            if (!game_.IsBlockIdx(ix, iy - 1)) {
                flat(0.28f + shade, 0.66f + shade, 0.26f + shade, u, v);
                AddFace(topMesh, { cx, cy + 0.5f, cz }, { 0, 1, 0 }, { 0, 0, 1 }, u, v);
            }
            flat(0.50f + shade, 0.34f + shade, 0.20f + shade, u, v);
            if (!game_.IsBlockIdx(ix, iy + 1)) {
                AddFace(sideMesh, { cx, cy - 0.5f, cz }, { 0, -1, 0 }, { 0, 0, 1 }, u, v);
            }
            if (!game_.IsBlockIdx(ix - 1, iy)) {
                AddFace(sideMesh, { cx - 0.5f, cy, cz }, { -1, 0, 0 }, { 0, 1, 0 }, u, v);
            }
            if (!game_.IsBlockIdx(ix + 1, iy)) {
                AddFace(sideMesh, { cx + 0.5f, cy, cz }, { 1, 0, 0 }, { 0, 1, 0 }, u, v);
            }
            AddFace(sideMesh, { cx, cy, cz - 0.5f }, { 0, 0, -1 }, { 0, 1, 0 }, u, v); // 手前の面
        }
    }

    const std::string topName = NextTerrainName("top");
    const std::string sideName = NextTerrainName("side");
    ctx_->modelManager->Register(topName, top);
    ctx_->modelManager->Register(sideName, side);
    if (!terrainTop_) {
        terrainTop_ = MakeObject(topName);
        terrainSide_ = MakeObject(sideName);
    }
    else {
        terrainTop_->SetModel(topName);
        terrainSide_->SetModel(sideName);
    }
    // 頂点の色（UV）をそのまま使うので、UV変換は等倍
    for (Object3d* o : { terrainTop_.get(), terrainSide_.get() }) {
        o->GetUvTransform().Scale = { 1.0f, 1.0f, 1.0f };
        o->GetUvTransform().Translate = { 0.0f, 0.0f, 0.0f };
        o->GetTransform().Scale = { 1.0f, 1.0f, 1.0f };
        o->GetTransform().Rotate = { 0.0f, 0.0f, 0.0f };
        o->GetTransform().Translate = { 0.0f, 0.0f, 0.0f };
    }
}

//==================================================
// 更新
//==================================================
void PlatformerScene::ResetGame()
{
    game_.Reset();
}

void PlatformerScene::UpdateCamera()
{
    // 2Dの注視点を中心に、画面の高さに ppu のぶんのマスが映る距離から見る
    float ppu = ppu_;
    if (game_.killZoom > 0.0f) {
        const float u = (std::min)(game_.killZoom / 0.3f, 1.0f);
        ppu *= 1.0f + 0.12f * std::sin(u * kPi); // 撃破で一瞬だけ寄る
    }
    game_.SetView(ppu, static_cast<float>(WinApp::kGameWidth), static_cast<float>(WinApp::kGameHeight));

    const float visibleH = static_cast<float>(WinApp::kGameHeight) / ppu;
    const float dist = visibleH / (2.0f * std::tan(kFovY * 0.5f));
    const float yaw = yawDeg_ * kDegToRad;
    const float pitch = pitchDeg_ * kDegToRad;
    const Vector3 target{ game_.cam.x, game_.cam.y, 0.0f };
    const Vector3 dir{ std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch) };
    Transform& t = camera_.GetTransform();
    t.Translate = { target.x + dir.x * dist, target.y + dir.y * dist, target.z + dir.z * dist };
    t.Rotate = { pitch, -yaw, 0.0f };
    camera_.Update(*ctx_->input);
}

void PlatformerScene::SyncObjects()
{
    drawList_.clear();
    const Matrix4x4& view = camera_.GetViewMatrix();
    const Matrix4x4& proj = camera_.GetProjectionMatrix();
    const float yawOffset = modelYawOffsetDeg_ * kDegToRad;

    // 地形
    drawList_.push_back(terrainTop_.get());
    drawList_.push_back(terrainSide_.get());

    // プレイヤー（無敵中は点滅。向いている側へ少しカメラ寄りに回す）
    const pf::Player& p = game_.player;
    const bool blink = p.invincible > 0.0f && (static_cast<int>(game_.time * 20.0f) & 1);
    if (!blink && game_.phase != pf::Phase::Death) {
        const float yaw = static_cast<float>(p.lr) * (kPi * 0.5f + 0.5f) + yawOffset;
        PlaceFit(*player_, p.pos, kCharacterZ, pf::Game::kPlayerW * 1.15f, yaw);
        SetFlatColor(*player_, 0.95f, 0.55f, 0.15f);
        drawList_.push_back(player_.get());
    }

    // ゴール
    if (game_.hasGoal) {
        PlaceBox(*goalPole_, { game_.goal.x, game_.goal.y }, 0.2f, 0.12f, 2.0f, 0.12f, 0.0f);
        const float wave = std::sin(game_.time * 4.0f) * 0.15f;
        PlaceBox(*goalFlag_, { game_.goal.x + 0.5f, game_.goal.y + 0.6f }, 0.2f, 0.9f, 0.5f, 0.05f, wave);
        drawList_.push_back(goalPole_.get());
        drawList_.push_back(goalFlag_.get());
    }

    // 敵：ゲーム側の敵に合わせてオブジェクトを作り、居なくなったものは消す
    std::vector<int> alive;
    for (const pf::Enemy& e : game_.enemies) {
        if (e.dead) {
            continue;
        }
        alive.push_back(e.id);
        auto it = enemyViews_.find(e.id);
        if (it == enemyViews_.end()) {
            EnemyView view2;
            view2.body = MakeObject("teapot.obj");
            if (e.shield) {
                view2.shield = MakeObject("pf_cube");
            }
            it = enemyViews_.emplace(e.id, std::move(view2)).first;
        }
        EnemyView& ev = it->second;

        float scale = 1.0f;
        float spin = 0.0f;
        if (e.dying) {
            const float u = (std::min)(e.dyingTimer / 0.4f, 1.0f);
            scale = 1.0f - u; // 回りながら縮む
            spin = u * 6.0f;
        }
        const float bounce = e.dying ? 0.0f : std::fabs(std::sin(e.walkTime * 8.0f)) * 0.06f;
        const pf::Vec2 pos{ e.pos.x, e.pos.y - 0.05f + bounce };
        const float yaw = static_cast<float>(e.dir) * (kPi * 0.5f + 0.4f) + yawOffset + spin;
        PlaceFit(*ev.body, pos, kCharacterZ, 0.95f, yaw, scale);
        if (e.guardFlash > 0.0f) {
            SetFlatColor(*ev.body, 1.0f, 1.0f, 1.0f);
        }
        else if (e.shield) {
            SetFlatColor(*ev.body, 0.55f, 0.30f, 0.75f);
        }
        else {
            SetFlatColor(*ev.body, 0.85f, 0.22f, 0.22f);
        }
        drawList_.push_back(ev.body.get());

        if (ev.shield && !e.dying) {
            // 盾（向いている側に立てる板）
            const pf::Vec2 sp{ e.pos.x + static_cast<float>(e.dir) * 0.55f, e.pos.y };
            PlaceBox(*ev.shield, sp, kCharacterZ, 0.12f, 1.0f, 0.8f, 0.0f);
            SetFlatColor(*ev.shield, 0.45f, 0.75f, 1.0f);
            drawList_.push_back(ev.shield.get());
        }
    }
    for (auto it = enemyViews_.begin(); it != enemyViews_.end();) {
        if (std::find(alive.begin(), alive.end(), it->first) == alive.end()) {
            it = enemyViews_.erase(it);
        }
        else {
            ++it;
        }
    }

    // 火花（小さな立方体）
    size_t pi = 0;
    for (const pf::Particle& pt : game_.particles) {
        if (pi >= particleCubes_.size()) {
            break;
        }
        Object3d& cube = *particleCubes_[pi++];
        const float t = pt.life / pt.maxLife;
        const float s = pt.size * (0.4f + 0.6f * t);
        PlaceBox(cube, pt.pos, kCharacterZ, s, s, s, game_.time * 8.0f);
        SetFlatColor(cube, pt.r, pt.g, pt.b);
        drawList_.push_back(&cube);
    }

    for (Object3d* o : drawList_) {
        o->Update(view, proj);
    }
}

void PlatformerScene::RenderShadowPass()
{
    if (!light_ || light_->shadowEnabled == 0) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = ctx_->dxCommon->GetCommandList();
    ShadowMap::Instance().Begin(commandList);
    for (Object3d* o : drawList_) {
        o->DrawShadow(commandList);
    }
    ShadowMap::Instance().End(commandList);
}

void PlatformerScene::Update()
{
    // マップ・敵の作り直しの合図
    if (builtMapVersion_ != game_.mapVersion) {
        RebuildTerrain();
        builtMapVersion_ = game_.mapVersion;
    }
    if (builtResetCount_ != game_.resetCount) {
        enemyViews_.clear();
        builtResetCount_ = game_.resetCount;
    }

    if (ctx_->input->TriggerKey(DIK_R)) {
        ResetGame();
    }

    const Input& input = *ctx_->input;
    pf::InputState in;
    in.left = input.PushKey(DIK_A) || input.PushKey(DIK_LEFT);
    in.right = input.PushKey(DIK_D) || input.PushKey(DIK_RIGHT);
    in.jumpPressed = input.TriggerKey(DIK_SPACE) || input.TriggerGamepadButton(0);
    in.jumpHeld = input.PushKey(DIK_SPACE) || input.PushGamepadButton(0);
    game_.Update(in);

    UpdateCamera();

    // 光・影の設定（プレイヤーの周りだけに影を落とす）
    if (light_) {
        light_->shadowEnabled = shadowEnabled_ ? 1 : 0;
        light_->lightCount = 0;
        ctx_->object3dCommon->SetLightViewProjection(ShadowMap::ComputeLightViewProjection(
            light_->direction, { game_.cam.x, game_.cam.y, 0.0f }, 22.0f));
    }

    SyncObjects();
    RenderShadowPass(); // 影はここで描いて、本描画で使う
}

//==================================================
// 描画
//==================================================
void PlatformerScene::Draw()
{
    ID3D12GraphicsCommandList* commandList = ctx_->dxCommon->GetCommandList();
    EnvironmentMap::Instance().DrawSkybox(commandList, camera_.GetViewMatrix(), camera_.GetProjectionMatrix());

    ctx_->object3dCommon->PreDraw();
    for (Object3d* o : drawList_) {
        o->Draw();
    }

    DrawHud();
}

// 画面に重ねる2D（HP・残りの敵・クリア/ゲームオーバーの色かぶり）
void PlatformerScene::DrawHud()
{
    ctx_->spriteCommon->PreDraw();
    hud_.Begin();

    for (int i = 0; i < 3; ++i) {
        const bool alive = i < game_.player.hp;
        const float x = 20.0f + static_cast<float>(i) * 34.0f;
        hud_.AddRect(x - 2.0f, 18.0f, 30.0f, 30.0f, ColorRGB{ 0.1f, 0.1f, 0.1f });
        hud_.AddRect(x, 20.0f, 26.0f, 26.0f, alive ? ColorRGB{ 0.95f, 0.2f, 0.25f } : ColorRGB{ 0.3f, 0.3f, 0.3f });
    }
    int remain = 0;
    for (const pf::Enemy& e : game_.enemies) {
        if (!e.dying && !e.dead) {
            ++remain;
        }
    }
    for (int i = 0; i < remain && i < 40; ++i) {
        hud_.AddRect(20.0f + static_cast<float>(i) * 12.0f, 58.0f, 9.0f, 9.0f, ColorRGB{ 0.85f, 0.22f, 0.22f });
    }
    hud_.Flush(BlendMode::Normal);

    const float w = static_cast<float>(WinApp::kGameWidth);
    const float h = static_cast<float>(WinApp::kGameHeight);
    if (game_.phase == pf::Phase::Death) {
        hud_.AddRect(0.0f, 0.0f, w, h, ColorRGB{ 0.30f, 0.02f, 0.02f });
    }
    else if (game_.phase == pf::Phase::Clear) {
        hud_.AddRect(0.0f, 0.0f, w, h, ColorRGB{ 0.28f, 0.24f, 0.02f });
    }
    if (game_.player.invincible > 1.3f) {
        hud_.AddRect(0.0f, 0.0f, w, 12.0f, ColorRGB{ 0.6f, 0.05f, 0.05f });
        hud_.AddRect(0.0f, h - 12.0f, w, 12.0f, ColorRGB{ 0.6f, 0.05f, 0.05f });
    }
    hud_.Flush(BlendMode::Add);
}

//==================================================
// ImGui
//==================================================
void PlatformerScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("Hierarchy", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: 2Dアクション（3D描画）");
    ImGui::Separator();
    const char* phaseName = game_.phase == pf::Phase::Play ? "プレイ中" : (game_.phase == pf::Phase::Clear ? "クリア！" : "ゲームオーバー");
    ImGui::Text("状態: %s", phaseName);
    ImGui::Text("HP: %d / 3", game_.player.hp);
    int remain = 0;
    for (const pf::Enemy& e : game_.enemies) {
        if (!e.dying && !e.dead) {
            ++remain;
        }
    }
    ImGui::Text("残りの敵: %d  倒した数: %d", remain, game_.killCount);
    ImGui::Separator();
    ImGui::Text("A/D: 移動  Space: ジャンプ");
    ImGui::Text("敵は上から踏んで倒す（盾の敵は2回）");
    ImGui::Text("R: やり直し");
    ImGui::Separator();
    if (ImGui::Button("やり直し (R)")) {
        ResetGame();
    }
    if (ImGui::Button("ステージCSVを読み込む")) {
        message_ = game_.LoadStageCsv(kStagePath) ? "stage.csv loaded" : "stage.csv not found";
    }
    if (ImGui::Button("組み込みステージに戻す")) {
        game_.LoadDefaultStage();
        message_ = "default stage";
    }
    if (ImGui::Button("今のステージをCSVに書き出す")) {
        message_ = game_.SaveStageCsv(kStagePath) ? "stage.csv saved" : "save failed";
    }
    if (!message_.empty()) {
        ImGui::TextDisabled("%s", message_.c_str());
    }
    ImGui::TextWrapped("CSV: 0=空 1=ブロック P=開始 E=敵 S=盾の敵 G=ゴール（200列x20行）");
    ImGui::End();

    EditorLayout::Begin("Inspector", EditorLayout::Panel::Inspector);
    if (ImGui::CollapsingHeader("カメラ・見た目", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("拡大率 (px/マス)", &ppu_, 24.0f, 96.0f);
        ImGui::SliderFloat("横ずらし角度", &yawDeg_, -40.0f, 40.0f);
        ImGui::SliderFloat("見下ろし角度", &pitchDeg_, 0.0f, 50.0f);
        ImGui::SliderFloat("キャラの向き補正", &modelYawOffsetDeg_, -180.0f, 180.0f);
        ImGui::Checkbox("影を落とす", &shadowEnabled_);
    }
    pf::JumpParams& jp = game_.settings.jump;
    if (ImGui::CollapsingHeader("移動・ジャンプ", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("走る速さ", &jp.runSpeed, 0.05f, 0.6f);
        ImGui::SliderFloat("ジャンプ初速", &jp.jumpSpeed, 0.2f, 1.0f);
        ImGui::SliderFloat("上昇中の重力", &jp.gravityScaleUp, 0.2f, 2.0f);
        ImGui::SliderFloat("落下中の重力", &jp.gravityScaleDown, 0.2f, 2.0f);
        ImGui::SliderFloat("最大落下速度", &jp.maxFallSpeed, 0.1f, 0.8f);
        ImGui::SliderFloat("頂点のふわっと", &jp.apexFloat, 0.1f, 1.0f);
        ImGui::SliderFloat("地上の加速", &jp.groundAccel, 0.01f, 0.2f);
        ImGui::SliderFloat("地上の減速", &jp.groundFriction, 0.5f, 1.0f);
        ImGui::SliderFloat("空中の加速", &jp.airAccel, 0.005f, 0.2f);
        ImGui::SliderFloat("空中の減速", &jp.airDrag, 0.9f, 1.0f);
        ImGui::SliderFloat("小ジャンプの抑え", &jp.jumpCut, 0.1f, 1.0f);
        ImGui::SliderFloat("踏んだときの跳ね", &jp.stompBounce, 0.1f, 0.8f);
    }
    if (ImGui::Button("設定を保存")) {
        std::ofstream file(kSettingPath, std::ios::binary);
        message_ = file.write(reinterpret_cast<const char*>(&game_.settings), sizeof(game_.settings)) ? "settings saved" : "settings save failed";
    }
    ImGui::SameLine();
    if (ImGui::Button("設定を初期値に戻す")) {
        game_.settings = pf::Settings{};
    }
    ImGui::End();
#endif // USE_IMGUI
}
