#include "MapEditorScene.h"

#include <algorithm>
#include <cmath>

#include "base/WinApp.h"
#include "3d/ModelManager.h"
#include "3d/Object3dCommon.h"
#include "../game2d/CubeMesh.h"
#include "2d/ColorBatch.h"
#include "input/Input.h"
#include "math/ScreenProjection.h"
#include "editor/EditorLayout.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

namespace {
    constexpr float kDt = 1.0f / 60.0f;
    constexpr float kPitch = 0.95f;         // カメラの傾き（ラジアン。大きいほど真上に近い）
    constexpr float kMinDistance = 6.0f;
    constexpr float kMaxDistance = 90.0f;
    const char* const kBuildingNames[MapData::KindCount] = { "酒場", "保安官事務所", "雑貨店" };
    const char* const kFacingNames[MapData::FacingCount] = { "下", "上", "左", "右" }; // 画面での正面の向き

    float ScreenW() { return static_cast<float>(WinApp::kGameWidth); }
    float ScreenH() { return static_cast<float>(WinApp::kGameHeight); }

    void SetTint(GameObject& obj, float r, float g, float b, float a = 1.0f)
    {
        if (Material* m = obj.GetObject3d().GetMaterial()) {
            m->color = Vector4{ r, g, b, a };
        }
    }
}

// ---------------------------------------------------------------- 初期化

void MapEditorScene::Initialize()
{
    camera_.SetPerspective(0.8f, ScreenW() / ScreenH(), 0.1f, 400.0f);

    mapView_.Initialize(ctx_->object3dCommon, ctx_->modelManager); // 単色の立方体もここで登録される
    cursor_.Initialize(ctx_->object3dCommon, MapRenderer::kCubeName, { 0.0f, 0.0f, 0.0f });

    map_.Reset(newWidth_, newHeight_);
    focus_ = { map_.width * 0.5f, map_.height * 0.5f };
    dirty_ = true;
    ApplyCamera();
}

// ---------------------------------------------------------------- 毎フレーム

void MapEditorScene::Update()
{
    UpdateCamera();
    ApplyCamera();   // 先にカメラを決める（マウスの位置を求めるため）
    UpdateHover();
    UpdateEditing();
    if (dirty_) {
        RebuildWalls();
    }
    UpdateVisuals();
}

void MapEditorScene::ApplyCamera()
{
    const Vector3 target{ focus_.x, 0.0f, focus_.y };
    const Vector3 eye{ focus_.x, std::sin(kPitch) * distance_, focus_.y - std::cos(kPitch) * distance_ };
    camera_.SetLookAt(eye, target, { 0.0f, 1.0f, 0.0f });
}

void MapEditorScene::UpdateCamera()
{
#ifdef USE_IMGUI
    const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();

    // ホイールで拡大・縮小
    if (vp.valid && vp.wheel != 0.0f) {
        distance_ = std::clamp(distance_ * std::pow(0.9f, vp.wheel), kMinDistance, kMaxDistance);
        ApplyCamera();
    }

    // 右ボタン・ホイールボタンのドラッグ：つかんだ地面の点が、マウスについてくるように動かす
    if ((vp.rightDown || vp.middleDown) && (vp.mouseDelta.x != 0.0f || vp.mouseDelta.y != 0.0f)) {
        Vector3 now{};
        Vector3 before{};
        const Vector2 prev{ vp.mouse.x - vp.mouseDelta.x, vp.mouse.y - vp.mouseDelta.y };
        if (ScreenProjection::ScreenToGround(vp.mouse, camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), ScreenW(), ScreenH(), 0.0f, now)
            && ScreenProjection::ScreenToGround(prev, camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), ScreenW(), ScreenH(), 0.0f, before)) {
            focus_.x += before.x - now.x;
            focus_.y += before.z - now.z;
        }
    }

    // WASD（文字を入力しているときは動かさない）
    if (!ImGui::GetIO().WantTextInput) {
        const float speed = distance_ * 0.8f * kDt;
        if (ctx_->input->PushKey(DIK_W)) { focus_.y += speed; }
        if (ctx_->input->PushKey(DIK_S)) { focus_.y -= speed; }
        if (ctx_->input->PushKey(DIK_D)) { focus_.x += speed; }
        if (ctx_->input->PushKey(DIK_A)) { focus_.x -= speed; }
    }
#endif // USE_IMGUI
    focus_.x = std::clamp(focus_.x, 0.0f, static_cast<float>(map_.width));
    focus_.y = std::clamp(focus_.y, 0.0f, static_cast<float>(map_.height));
}

void MapEditorScene::UpdateHover()
{
    hover_ = false;
#ifdef USE_IMGUI
    const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
    if (!vp.valid || vp.mouse.x < 0.0f || vp.mouse.y < 0.0f || vp.mouse.x >= ScreenW() || vp.mouse.y >= ScreenH()) {
        return;
    }
    Vector3 ground{};
    if (!ScreenProjection::ScreenToGround(vp.mouse, camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), ScreenW(), ScreenH(), 0.0f, ground)) {
        return;
    }
    const int x = static_cast<int>(std::floor(ground.x));
    const int z = static_cast<int>(std::floor(ground.z));
    if (map_.InRange(x, z)) {
        hover_ = true;
        hoverX_ = x;
        hoverZ_ = z;
    }
#endif // USE_IMGUI
}

void MapEditorScene::UpdateEditing()
{
#ifdef USE_IMGUI
    const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
    if (!vp.down) {
        lastPaintX_ = -1;
        lastPaintZ_ = -1;
    }
    if (!hover_) {
        return;
    }

    const bool paintTool = tool_ == Tool::Wall || tool_ == Tool::Barrel || tool_ == Tool::Erase;
    if (paintTool) {
        // 押している間、通ったマスを塗る（同じマスは1回だけ）
        if (vp.down && (hoverX_ != lastPaintX_ || hoverZ_ != lastPaintZ_)) {
            ApplyPaint(hoverX_, hoverZ_);
            lastPaintX_ = hoverX_;
            lastPaintZ_ = hoverZ_;
        }
    }
    else if (vp.clicked) {
        ApplyClick(hoverX_, hoverZ_);
    }
#endif // USE_IMGUI
}

void MapEditorScene::ApplyPaint(int x, int z)
{
    if (tool_ == Tool::Erase) {
        if (map_.RemoveBuildingAt(x, z)) {   // 建物はまるごと消す
            dirty_ = true;
        }
        else if (map_.Get(x, z) != MapData::Empty) {
            map_.Set(x, z, MapData::Empty);
            dirty_ = true;
        }
        // そのマスにある出現点も消す
        auto& s = map_.spawns;
        s.erase(std::remove_if(s.begin(), s.end(), [&](const MapData::Spawn& sp) { return sp.x == x && sp.z == z; }), s.end());
        if (map_.hasPlayerStart && map_.startX == x && map_.startZ == z) {
            map_.hasPlayerStart = false;
        }
        return;
    }

    const std::uint8_t kind = (tool_ == Tool::Wall) ? MapData::Wall : MapData::Barrel;
    if (map_.Get(x, z) == kind) {
        return;
    }
    if (map_.Get(x, z) == MapData::Building) {
        message_ = "建物の上には塗れません（先に「消す」で建物を消してください）";
        return;
    }
    map_.Set(x, z, kind);
    dirty_ = true;
}

void MapEditorScene::ApplyClick(int x, int z)
{
    switch (tool_) {
    case Tool::PlayerStart:
        map_.hasPlayerStart = true;
        map_.startX = x;
        map_.startZ = z;
        break;
    case Tool::Spawn: {
        // すでにあるマスなら取り除き、なければ追加する
        auto& s = map_.spawns;
        const auto it = std::find_if(s.begin(), s.end(), [&](const MapData::Spawn& sp) { return sp.x == x && sp.z == z; });
        if (it != s.end()) {
            s.erase(it);
        }
        else if (static_cast<int>(s.size()) < MapData::kMaxSpawns) {
            MapData::Spawn sp;
            sp.x = x;
            sp.z = z;
            sp.area = currentArea_;
            sp.count = spawnCount_;
            sp.interval = spawnInterval_;
            s.push_back(sp);
        }
        else {
            message_ = "出現点が多すぎます";
        }
        break;
    }
    case Tool::Gate:
        if (!gatePending_) {
            gatePending_ = true;
            gateX_ = x;
            gateZ_ = z;
        }
        else {
            gatePending_ = false;
            if (static_cast<int>(map_.gates.size()) < MapData::kMaxGates) {
                map_.gates.push_back(MapData::MakeGate(gateX_, gateZ_, x, z, currentArea_));
            }
            else {
                message_ = "区切りが多すぎます";
            }
        }
        break;
    case Tool::Building:
        if (!buildingPending_) {
            buildingPending_ = true;
            buildingX_ = x;
            buildingZ_ = z;
        }
        else {
            buildingPending_ = false;
            const MapData::BuildingRect rect = MapData::MakeBuilding(buildingX_, buildingZ_, x, z, buildingKind_, buildingFacing_);
            std::string why;
            if (!map_.PlaceBuilding(rect, why)) {
                message_ = "建物を置けません: " + why;
                break;
            }
            dirty_ = true;
            message_ = std::string(kBuildingNames[rect.kind]) + "を置きました";

            // 酒場は、正面（手前の1マス）の真ん中に出現点（ドア）も置く
            if (autoDoor_ && rect.kind == MapData::Saloon) {
                int dx = 0;
                int dz = 0;
                MapData::FrontCell(rect, dx, dz);
                bool exists = false;
                for (const MapData::Spawn& sp : map_.spawns) {
                    exists = exists || (sp.x == dx && sp.z == dz);
                }
                if (!exists && map_.InRange(dx, dz) && !map_.IsSolid(dx, dz) && static_cast<int>(map_.spawns.size()) < MapData::kMaxSpawns) {
                    MapData::Spawn sp;
                    sp.x = dx;
                    sp.z = dz;
                    sp.area = currentArea_;
                    sp.count = spawnCount_;
                    sp.interval = spawnInterval_;
                    map_.spawns.push_back(sp);
                    message_ += "（正面にドアも置きました）";
                }
            }
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- 見た目

void MapEditorScene::RebuildWalls()
{
    dirty_ = false;
    poolFull_ = !mapView_.Build(map_); // 立方体が足りなければ、足りない分は描かれない
}

void MapEditorScene::UpdateVisuals()
{
    // 指しているマスの目印（道具ごとに色を変える）
    static const float colors[7][3] = {
        { 0.9f, 0.9f, 0.9f }, { 0.9f, 0.6f, 0.3f }, { 1.0f, 0.3f, 0.3f }, { 0.3f, 1.0f, 0.3f }, { 0.3f, 0.6f, 1.0f }, { 1.0f, 0.9f, 0.2f }, { 0.8f, 0.4f, 1.0f } };
    const int t = static_cast<int>(tool_);
    SetTint(cursor_, colors[t][0], colors[t][1], colors[t][2]);
    cursor_.GetObject3d().GetTransform().Scale = { 0.96f, 0.12f, 0.96f };
    cursor_.GetObject3d().GetTransform().Translate = CellCenter(hoverX_, hoverZ_, 0.08f);

    const Matrix4x4& view = camera_.GetViewMatrix();
    const Matrix4x4& proj = camera_.GetProjectionMatrix();
    mapView_.Update(view, proj);
    cursor_.GetObject3d().Update(view, proj);
}

void MapEditorScene::Draw()
{
    ctx_->object3dCommon->PreDraw();
    mapView_.Draw();
    if (hover_) {
        cursor_.Draw();
    }
}

bool MapEditorScene::CellToScreen(int x, int z, Vector2& out) const
{
    return ScreenProjection::WorldToScreen(CellCenter(x, z, 0.3f), camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), ScreenW(), ScreenH(), out);
}

// ---------------------------------------------------------------- ファイル

void MapEditorScene::NewMap()
{
    map_.Reset(newWidth_, newHeight_);
    focus_ = { map_.width * 0.5f, map_.height * 0.5f };
    gatePending_ = false;
    buildingPending_ = false;
    dirty_ = true;
    message_ = "新しいマップを作りました";
}

void MapEditorScene::SaveMap()
{
    std::string error;
    if (map_.Save(path_, error)) {
        message_ = std::string("保存しました: ") + path_;
    }
    else {
        message_ = "保存に失敗しました: " + error;
    }
}

void MapEditorScene::LoadMap()
{
    std::string error;
    if (map_.Load(path_, error)) {
        newWidth_ = map_.width;
        newHeight_ = map_.height;
        focus_ = { map_.width * 0.5f, map_.height * 0.5f };
        gatePending_ = false;
        buildingPending_ = false;
        dirty_ = true;
        message_ = std::string("読み込みました: ") + path_;
    }
    else {
        message_ = "読み込みに失敗しました: " + error;
    }
}

// ---------------------------------------------------------------- ImGui

void MapEditorScene::DrawOverlay()
{
#ifdef USE_IMGUI
    // 自機の開始位置
    Vector2 p{};
    if (map_.hasPlayerStart && CellToScreen(map_.startX, map_.startZ, p)) {
        EditorLayout::DrawViewportMarker(p, "自機", IM_COL32(80, 255, 80, 255), false);
    }
    // 出現点
    for (size_t i = 0; i < map_.spawns.size(); ++i) {
        const MapData::Spawn& s = map_.spawns[i];
        if (CellToScreen(s.x, s.z, p)) {
            const std::string label = "ドア " + std::to_string(i) + " (区切り" + std::to_string(s.area) + ")";
            EditorLayout::DrawViewportMarker(p, label.c_str(), IM_COL32(80, 160, 255, 255), false);
        }
    }
    // 区切り：四角い枠
    auto drawRect = [&](int x0, int z0, int x1, int z1) {
        // マスの外側の角（x1, z1 を含めるため +1）
        const Vector3 corners[4] = { { static_cast<float>(x0), 0.05f, static_cast<float>(z0) }, { static_cast<float>(x1 + 1), 0.05f, static_cast<float>(z0) },
                                     { static_cast<float>(x1 + 1), 0.05f, static_cast<float>(z1 + 1) }, { static_cast<float>(x0), 0.05f, static_cast<float>(z1 + 1) } };
        Vector2 pts[4];
        for (int i = 0; i < 4; ++i) {
            if (!ScreenProjection::WorldToScreen(corners[i], camera_.GetViewMatrix(), camera_.GetProjectionMatrix(), ScreenW(), ScreenH(), pts[i])) {
                return;
            }
        }
        EditorLayout::DrawViewportOverlay(pts, 4, true);
    };
    for (const MapData::Gate& g : map_.gates) {
        drawRect(g.x0, g.z0, g.x1, g.z1);
    }
    // 建物：種類の名前
    for (const MapData::BuildingRect& b : map_.buildings) {
        if (CellToScreen((b.x0 + b.x1) / 2, (b.z0 + b.z1) / 2, p)) {
            const std::string label = std::string(kBuildingNames[b.kind]) + "(正面:" + kFacingNames[b.facing] + ")";
            EditorLayout::DrawViewportMarker(p, label.c_str(), IM_COL32(220, 150, 255, 255), false);
        }
    }
    if (buildingPending_ && hover_) {
        const MapData::BuildingRect b = MapData::MakeBuilding(buildingX_, buildingZ_, hoverX_, hoverZ_, buildingKind_, buildingFacing_);
        drawRect(b.x0, b.z0, b.x1, b.z1);
    }
    if (gatePending_ && hover_) {
        const MapData::Gate g = MapData::MakeGate(gateX_, gateZ_, hoverX_, hoverZ_, currentArea_);
        drawRect(g.x0, g.z0, g.x1, g.z1);
    }
#endif // USE_IMGUI
}

void MapEditorScene::DrawImGui()
{
#ifdef USE_IMGUI
    DrawOverlay();

    // ---- 左：道具・ファイル ----
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: マップエディタ");
    ImGui::Separator();

    ImGui::Text("道具");
    int tool = static_cast<int>(tool_);
    ImGui::RadioButton("壁", &tool, 0);
    ImGui::RadioButton("樽", &tool, 1);
    ImGui::RadioButton("消す", &tool, 2);
    ImGui::RadioButton("自機の開始位置", &tool, 3);
    ImGui::RadioButton("敵の出現点（ドア）", &tool, 4);
    ImGui::RadioButton("区切り（2回クリック）", &tool, 5);
    ImGui::RadioButton("建物（2回クリック）", &tool, 6);
    if (tool != static_cast<int>(tool_)) {
        tool_ = static_cast<Tool>(tool);
        gatePending_ = false;
        buildingPending_ = false;
    }
    if (tool_ == Tool::Building) {
        for (int k = 0; k < MapData::KindCount; ++k) {
            ImGui::RadioButton(kBuildingNames[k], &buildingKind_, k);
        }
        ImGui::Text("正面の向き（画面で）");
        for (int f = 0; f < MapData::FacingCount; ++f) {
            ImGui::RadioButton(kFacingNames[f], &buildingFacing_, f);
            if (f + 1 < MapData::FacingCount) {
                ImGui::SameLine();
            }
        }
        ImGui::Checkbox("酒場は正面にドアも置く", &autoDoor_);
        ImGui::TextDisabled("消すときは「消す」で建物をクリック");
        if (buildingPending_) {
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "もう1つの角をクリック");
        }
    }
    if (tool_ == Tool::Spawn || tool_ == Tool::Gate || (tool_ == Tool::Building && autoDoor_ && buildingKind_ == MapData::Saloon)) {
        ImGui::InputInt("区切り番号", &currentArea_);
        currentArea_ = std::clamp(currentArea_, 0, 99);
    }
    if (tool_ == Tool::Spawn || (tool_ == Tool::Building && autoDoor_ && buildingKind_ == MapData::Saloon)) {
        ImGui::InputInt("出す数", &spawnCount_);
        spawnCount_ = std::clamp(spawnCount_, 1, 99);
        ImGui::InputFloat("出す間隔(秒)", &spawnInterval_);
        spawnInterval_ = std::clamp(spawnInterval_, 0.1f, 10.0f);
        if (tool_ == Tool::Spawn) {
            ImGui::TextDisabled("置いたドアをもう一度クリックすると消える");
        }
    }
    if (tool_ == Tool::Gate && gatePending_) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "もう1つの角をクリック");
    }

    ImGui::Separator();
    ImGui::Text("ファイル");
    ImGui::InputText("パス", path_, sizeof(path_));
    if (ImGui::Button("保存")) {
        SaveMap();
    }
    ImGui::SameLine();
    if (ImGui::Button("読み込み")) {
        LoadMap();
    }
    ImGui::InputInt("横", &newWidth_);
    ImGui::InputInt("縦", &newHeight_);
    newWidth_ = std::clamp(newWidth_, 1, MapData::kMaxSize);
    newHeight_ = std::clamp(newHeight_, 1, MapData::kMaxSize);
    if (ImGui::Button("新しいマップ（いまのマップは消える）")) {
        NewMap();
    }
    if (!message_.empty()) {
        ImGui::TextWrapped("%s", message_.c_str());
    }

    ImGui::Separator();
    ImGui::Text("マップ: %d x %d  壁 %d  樽 %d  建物 %d", map_.width, map_.height, map_.CountCells(MapData::Wall), map_.CountCells(MapData::Barrel), static_cast<int>(map_.buildings.size()));
    if (poolFull_) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "壁・建物が多すぎて、一部が描かれません");
    }
    if (hover_) {
        ImGui::Text("指しているマス: (%d, %d)", hoverX_, hoverZ_);
    }
    ImGui::TextDisabled("右ドラッグ: 動かす  ホイール: 拡大縮小  WASD: 動かす");
    ImGui::End();

    // ---- 右：出現点・区切りの一覧と確認 ----
    EditorLayout::Begin("インスペクター", EditorLayout::Panel::Inspector);
    ImGui::Text("出現点（ドア） %d", static_cast<int>(map_.spawns.size()));
    for (size_t i = 0; i < map_.spawns.size(); ++i) {
        MapData::Spawn& s = map_.spawns[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("ドア %d  (%d, %d)", static_cast<int>(i), s.x, s.z);
        ImGui::InputInt("区切り番号", &s.area);
        ImGui::InputInt("出す数", &s.count);
        ImGui::InputFloat("間隔(秒)", &s.interval);
        s.area = std::clamp(s.area, 0, 99);
        s.count = std::clamp(s.count, 1, 99);
        s.interval = std::clamp(s.interval, 0.1f, 10.0f);
        if (ImGui::Button("削除")) {
            map_.spawns.erase(map_.spawns.begin() + static_cast<std::ptrdiff_t>(i));
            ImGui::PopID();
            break; // 配列が変わったので、この回はここまで
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    ImGui::Text("区切り %d", static_cast<int>(map_.gates.size()));
    for (size_t i = 0; i < map_.gates.size(); ++i) {
        MapData::Gate& g = map_.gates[i];
        ImGui::PushID(1000 + static_cast<int>(i));
        ImGui::Text("区切り %d  (%d,%d)-(%d,%d)", static_cast<int>(i), g.x0, g.z0, g.x1, g.z1);
        ImGui::InputInt("区切り番号", &g.area);
        g.area = std::clamp(g.area, 0, 99);
        if (ImGui::Button("削除")) {
            map_.gates.erase(map_.gates.begin() + static_cast<std::ptrdiff_t>(i));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    ImGui::Text("建物 %d", static_cast<int>(map_.buildings.size()));
    for (size_t i = 0; i < map_.buildings.size(); ++i) {
        MapData::BuildingRect& b = map_.buildings[i];
        ImGui::PushID(2000 + static_cast<int>(i));
        ImGui::Text("%s %d  (%d,%d)-(%d,%d)  正面:%s", kBuildingNames[b.kind], static_cast<int>(i), b.x0, b.z0, b.x1, b.z1, kFacingNames[b.facing]);
        int kind = b.kind;
        for (int k = 0; k < MapData::KindCount; ++k) {
            ImGui::RadioButton(kBuildingNames[k], &kind, k);
            if (k + 1 < MapData::KindCount) {
                ImGui::SameLine();
            }
        }
        if (kind != b.kind) {
            b.kind = kind;
            dirty_ = true;
        }
        ImGui::Text("正面");
        ImGui::SameLine();
        int facing = b.facing;
        for (int f = 0; f < MapData::FacingCount; ++f) {
            ImGui::RadioButton((std::string(kFacingNames[f]) + "##f").c_str(), &facing, f);
            if (f + 1 < MapData::FacingCount) {
                ImGui::SameLine();
            }
        }
        if (facing != b.facing) {
            b.facing = facing;
            dirty_ = true;
        }
        if (ImGui::Button("削除")) {
            map_.RemoveBuilding(i);
            dirty_ = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    ImGui::Text("確認");
    const std::vector<std::string> problems = map_.Validate();
    if (problems.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "問題なし");
    }
    for (const std::string& p : problems) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", p.c_str());
    }
    ImGui::End();
#endif // USE_IMGUI
}
