#include "GameScene.h"
#include "TitleScene.h"
#include "SceneManager.h"

#include "../base/Logger.h"
#include "../base/WinApp.h"
#include "../2d/SpriteCommon.h"
#include "../3d/ModelLoader.h"
#include "../3d/Object3dCommon.h"
#include "../camera/DebugCamera.h"
#include "../editor/EditorLayout.h"
#include "../editor/ViewportPicking.h"
#include "../3d/Model.h"
#include "../input/Input.h"
#include "../math/Color.h"
#include "../particle/ParticleSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>

#ifdef USE_IMGUI
#include "../externals/imgui/imgui.h"
#endif // USE_IMGUI

namespace
{
    // 拡張子（小文字）が一致するか
    bool HasExtension(const std::filesystem::path& path, std::initializer_list<const char*> extensions)
    {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const char* e : extensions) {
            if (ext == e) {
                return true;
            }
        }
        return false;
    }
}

//==================================================
// 初期化・終了
//==================================================
void GameScene::Initialize()
{
    // カメラ（DebugCamera：矢印キー/WASD等で操作）
    auto debugCamera = std::make_unique<DebugCamera>();
    debugCamera->Initialize(static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight);
    debugCamera_ = debugCamera.get();
    camera_ = std::move(debugCamera);
    cameraActive_ = false;

    ScanResources();

    objects_.clear();
    sprites_.clear();
    pendingCommands_.clear();

    // 最初は何も置かない（Hierarchyの右クリック / [+ Add] から追加する）

    directionalLight_ = ctx_->object3dCommon->GetDirectionalLight();

    konamiCommand_ = KonamiCommand{};
    konamiPartyMode_ = false;
    konamiTime_ = 0.0f;
    selection_ = {};
    dragMode_ = DragMode::None;

    ctx_->particleSystem->Clear();
}

void GameScene::Finalize()
{
    pendingCommands_.clear();
    objects_.clear();
    sprites_.clear();
    debugCamera_ = nullptr;
    camera_.reset();
    ctx_->particleSystem->Clear();
}

// resourcesフォルダのobj/画像を調べて、追加メニューの選択肢にする
void GameScene::ScanResources()
{
    // objは既定の並びを先頭に固定する（球はコードで生成するので特別なタグ名）
    objFileList_ = {
        "axis.obj", "plane.obj", ModelLoader::kSphereTag, "suzanne.obj", "bunny.obj", "teapot.obj",
        "multiMesh.obj", "multiMaterial.obj"
    };
    textureFileList_ = { "resources/uvChecker.png" };

    std::error_code ec;
    std::vector<std::string> foundObjs;
    std::vector<std::string> foundTextures;
    for (const auto& entry : std::filesystem::directory_iterator("resources", ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        if (HasExtension(path, { ".obj" })) {
            foundObjs.push_back(path.filename().string());
        }
        else if (HasExtension(path, { ".png", ".jpg", ".jpeg", ".bmp" })) {
            foundTextures.push_back("resources/" + path.filename().string());
        }
    }
    std::sort(foundObjs.begin(), foundObjs.end());
    std::sort(foundTextures.begin(), foundTextures.end());

    for (const auto& name : foundObjs) {
        if (std::find(objFileList_.begin(), objFileList_.end(), name) == objFileList_.end()) {
            objFileList_.push_back(name);
        }
    }
    for (const auto& name : foundTextures) {
        if (std::find(textureFileList_.begin(), textureFileList_.end(), name) == textureFileList_.end()) {
            textureFileList_.push_back(name);
        }
    }

    objFileListCStr_.clear();
    for (const auto& name : objFileList_) {
        objFileListCStr_.push_back(name.c_str());
    }
    textureFileListCStr_.clear();
    for (const auto& name : textureFileList_) {
        textureFileListCStr_.push_back(name.c_str());
    }
}

//==================================================
// 追加・複製
//==================================================
int GameScene::AddObject(int objIndex, const Vector3& translate)
{
    ObjectSlot slot;
    slot.object = std::make_unique<GameObject>();
    slot.object->Initialize(ctx_->object3dCommon, objFileList_[objIndex], translate);
    slot.objIndex = objIndex;
    slot.loadedObjIndex = objIndex;
    slot.konamiOriginalColor = slot.object->GetObject3d().GetMaterial()->color;
    objects_.push_back(std::move(slot));
    return static_cast<int>(objects_.size()) - 1;
}

int GameScene::AddSprite(int texIndex, const Vector2& size)
{
    SpriteSlot slot;
    slot.sprite = std::make_unique<Sprite>();
    slot.sprite->Initialize(ctx_->spriteCommon, textureFileList_[texIndex], size);
    slot.texIndex = texIndex;
    slot.loadedTexIndex = texIndex;
    slot.size = size;
    sprites_.push_back(std::move(slot));
    return static_cast<int>(sprites_.size()) - 1;
}

void GameScene::DuplicateObject(int index)
{
    if (index < 0 || index >= static_cast<int>(objects_.size())) {
        return;
    }
    // 参照を保持したままpush_backすると無効になるので、値をコピーしてから追加する
    const int srcModel = objects_[index].objIndex;
    Object3d& src = objects_[index].object->GetObject3d();
    const Transform transform = src.GetTransform();
    const Transform uvTransform = src.GetUvTransform();
    const int lightingMode = src.GetMaterial()->lightingMode;
    const BlendMode blendMode = src.GetBlendMode();
    const float alphaCutoff = src.GetMaterial()->alphaCutoff;
    const bool doubleSided = src.IsDoubleSided();
    // パーティーモード中は演出で色が変わっているので、退避しておいた元の色を引き継ぐ
    const Vector4 color = konamiPartyMode_ ? objects_[index].konamiOriginalColor : src.GetMaterial()->color;

    const int newIndex = AddObject(srcModel, transform.Translate);
    Object3d& dst = objects_[newIndex].object->GetObject3d();
    dst.GetTransform() = transform;
    dst.GetTransform().Translate.x += 0.5f; // 重ならないように少しずらす
    dst.GetUvTransform() = uvTransform;
    dst.GetMaterial()->color = color;
    dst.GetMaterial()->lightingMode = lightingMode;
    dst.SetBlendMode(blendMode);
    dst.SetDoubleSided(doubleSided);
    dst.GetMaterial()->alphaCutoff = alphaCutoff;
    objects_[newIndex].konamiOriginalColor = color;

    selection_ = { SelectKind::Object, newIndex };
}

void GameScene::DuplicateSprite(int index)
{
    if (index < 0 || index >= static_cast<int>(sprites_.size())) {
        return;
    }
    const int srcTex = sprites_[index].texIndex;
    const Vector2 srcSize = sprites_[index].size;
    Sprite& src = *sprites_[index].sprite;
    const Transform transform = src.GetTransform();
    const Transform uvTransform = src.GetUvTransform();
    const Vector4 color = src.GetMaterial()->color;
    const BlendMode blendMode = src.GetBlendMode();
    const float alphaCutoff = src.GetMaterial()->alphaCutoff;

    const int newIndex = AddSprite(srcTex, srcSize);
    Sprite& dst = *sprites_[newIndex].sprite;
    dst.GetTransform() = transform;
    dst.GetTransform().Translate.x += 20.0f;
    dst.GetTransform().Translate.y += 20.0f;
    dst.GetUvTransform() = uvTransform;
    dst.GetMaterial()->color = color;
    dst.SetBlendMode(blendMode);
    dst.GetMaterial()->alphaCutoff = alphaCutoff;

    selection_ = { SelectKind::Sprite, newIndex };
}

void GameScene::RunPendingCommands()
{
    if (pendingCommands_.empty()) {
        return;
    }
    // 実行中に新しいコマンドが積まれても安全なように、一度取り出してから実行する
    std::vector<std::function<void()>> commands;
    commands.swap(pendingCommands_);
    for (auto& command : commands) {
        command();
    }
}

//==================================================
// 更新
//==================================================
void GameScene::Update()
{
    Input& input = *ctx_->input;

    // タイトルへ戻る
    if (input.TriggerKey(DIK_BACK) || input.TriggerGamepadButton(9)) {
        ctx_->sceneManager->ChangeScene<TitleScene>();
        return;
    }

    // ImGuiで予約された 追加/複製/削除 をここで実行する（前フレームのGPU処理は完了済み）
    RunPendingCommands();

    // ImGuiでobj/テクスチャが切り替えられたものを差し替える
    // （読み込み済みのものはManagerのキャッシュから再利用される）
    for (auto& slot : objects_) {
        if (slot.objIndex != slot.loadedObjIndex) {
            slot.object->GetObject3d().SetModel(objFileList_[slot.objIndex]);
            slot.loadedObjIndex = slot.objIndex;
        }
    }
    for (auto& slot : sprites_) {
        if (slot.texIndex != slot.loadedTexIndex) {
            slot.sprite->SetTexture(textureFileList_[slot.texIndex]);
            slot.loadedTexIndex = slot.texIndex;
        }
    }

    UpdateCameraControl();
    camera_->Update(input);
    UpdateViewportInteraction();
    UpdateKonami();
    UpdateObjects();

    // パーティクル（パーティーモードOFFでも生存中のものは更新される）
    ctx_->particleSystem->Update(camera_->GetViewMatrix(), camera_->GetProjectionMatrix(), konamiPartyMode_);

    for (auto& slot : sprites_) {
        slot.sprite->Update();
    }
}

// カメラ操作：Viewport上で右クリックを押している間だけ有効（Unityと同じ）
//  ・マウス移動：視点回転 / W A S D：前後左右 / Space・LShift：上下
//  ・マウスホイール：ズーム（Viewportにマウスが乗っていれば右クリックなしでも有効）
// ImGuiなしのビルドでは常に有効
void GameScene::UpdateCameraControl()
{
#ifdef USE_IMGUI
    const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
    if (vp.rightClicked) {
        cameraActive_ = true;
    }
    if (!vp.rightDown) {
        cameraActive_ = false;
    }

    debugCamera_->SetControlEnabled(cameraActive_);
    debugCamera_->SetMouseLook(cameraActive_ ? vp.mouseDelta.x : 0.0f, cameraActive_ ? vp.mouseDelta.y : 0.0f);
    debugCamera_->SetZoom(vp.wheel); // ホイールはViewportにマウスが乗っていれば常に有効

    // 左クリックでオブジェクトをドラッグ中、カーソルが画面の端に来たらWASD(A/D・Space/Shift)と
    // 同じ速度でカメラを動かす。左端=A、右端=D、上端=Space(上)、下端=LShift(下)
    float panRight = 0.0f;
    float panUp = 0.0f;
    if (dragMode_ == DragMode::Move && selection_.kind == SelectKind::Object && vp.down) {
        constexpr float kEdgeMargin = 20.0f; // 端からこの距離(ゲーム画面px)以内で反応
        if (vp.mouse.x <= kEdgeMargin) panRight -= 1.0f;
        if (vp.mouse.x >= WinApp::kGameWidth - kEdgeMargin) panRight += 1.0f;
        if (vp.mouse.y <= kEdgeMargin) panUp += 1.0f;
        if (vp.mouse.y >= WinApp::kGameHeight - kEdgeMargin) panUp -= 1.0f;
    }
    // 背景を左クリックでつかんでドラッグ：マウスと逆方向にカメラを動かす（画面をつかんで引っ張る感覚）
    if (dragMode_ == DragMode::PanCamera && vp.down) {
        constexpr float kPanPerPixel = 0.06f; // 1pxあたりの移動量（SetPanはkMoveSpeedを掛けるので実際は0.03）
        panRight = -vp.mouseDelta.x * kPanPerPixel;
        panUp = vp.mouseDelta.y * kPanPerPixel;
    }
    debugCamera_->SetPan(panRight, panUp);
#else
    debugCamera_->SetControlEnabled(true);
#endif // USE_IMGUI
}

// Viewport上の操作
//  ・左クリック：手前の項目を選択 / 選択枠のハンドルをつかむと拡大縮小
//  ・ドラッグ：選択した項目を動かす
//     スプライト：画面上でそのまま動く
//     3Dオブジェクト：カメラに平行な平面上で動く（Shiftを押しながらつかむと地面(XZ平面)上）
//  ・ハンドルのドラッグ（反対側を固定して伸びる）
//     辺の中点：その方向にだけ伸びる（縦に引けば縦、横に引けば横）
//     四隅：縦横比を保って拡大縮小（Shiftを押しながらだと縦横を自由に）
//     3Dオブジェクトは、画面の縦/横に最も近いオブジェクトの軸(X/Y/Z)が伸びる。四隅は全軸を同じ倍率で拡大縮小
// 判定は前フレームのViewport入力を使うので、1フレーム遅れる
void GameScene::UpdateViewportInteraction()
{
#ifdef USE_IMGUI
    const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
    if (!vp.valid) {
        dragMode_ = DragMode::None;
        return;
    }

    const Matrix4x4& view = camera_->GetViewMatrix();
    const Matrix4x4& projection = camera_->GetProjectionMatrix();
    const Picking::Ray ray = Picking::ScreenToRay(vp.mouse, view, projection);

    //--------------------------------------------------
    // クリック
    //--------------------------------------------------
    if (vp.clicked && !cameraActive_) {
        dragMode_ = DragMode::None;

        // ① 選択中の項目のハンドルをつかんだか（四隅を先に判定する）
        bool grabbedHandle = false;
        Vector2 handles[8];
        if (GetSelectionHandles(handles)) {
            constexpr float kHandleRadius = 10.0f; // ゲーム画面ピクセル
            for (int k = 0; k < 8 && !grabbedHandle; ++k) {
                const float dx = vp.mouse.x - handles[k].x;
                const float dy = vp.mouse.y - handles[k].y;
                if (dx * dx + dy * dy > kHandleRadius * kHandleRadius) {
                    continue;
                }

                scaleHandle_ = k;
                const int opposite = (k < 4) ? (k + 2) % 4 : 4 + (k - 4 + 2) % 4;

                if (selection_.kind == SelectKind::Sprite) {
                    scaleStart_ = sprites_[selection_.index].sprite->GetTransform().Scale;
                    scaleAnchor_ = handles[opposite]; // 反対側は動かさない
                    dragMode_ = DragMode::ScaleSprite;
                    grabbedHandle = true;
                }
                else if (selection_.kind == SelectKind::Object) {
                    Object3d& object = objects_[selection_.index].object->GetObject3d();
                    const Transform& t = object.GetTransform();
                    Vector2 center{};
                    if (!Picking::WorldToScreen(t.Translate, view, projection, center)) {
                        continue;
                    }
                    scaleStart_ = t.Scale;
                    scaleCenter_ = center;

                    // 辺のハンドル：画面の横(右・左)/縦(上・下)に最も近い向きのオブジェクト軸を選ぶ
                    scaleAxis_ = -1;
                    if (k >= 4) {
                        const bool horizontal = (k == 5 || k == 7);
                        const Matrix4x4 world = Matrix4x4::MakeAffineMatrix(t.Scale, t.Rotate, t.Translate);
                        float best = -1.0f;
                        for (int axis = 0; axis < 3; ++axis) {
                            const Vector3 dir = Vector3{ world.m[axis][0], world.m[axis][1], world.m[axis][2] }.Normalize();
                            Vector2 tip{};
                            if (!Picking::WorldToScreen(t.Translate.Add(dir), view, projection, tip)) {
                                continue;
                            }
                            const float component = std::fabs(horizontal ? tip.x - center.x : tip.y - center.y);
                            if (component > best) {
                                best = component;
                                scaleAxis_ = axis;
                            }
                        }
                        if (scaleAxis_ < 0) {
                            continue;
                        }
                    }

                    // 開始時の「中心からマウスまでの距離」（辺は縦か横の成分だけ、四隅は直線距離）
                    const float cx = vp.mouse.x - center.x;
                    const float cy = vp.mouse.y - center.y;
                    float distance = std::sqrt(cx * cx + cy * cy);
                    if (k >= 4) {
                        distance = std::fabs((k == 5 || k == 7) ? cx : cy);
                    }
                    scaleStartDistance_ = (std::max)(distance, 5.0f);
                    dragMode_ = DragMode::ScaleObject;
                    grabbedHandle = true;
                }
            }
        }

        // ② ハンドルでなければ、手前の項目から順に判定して選択する
        if (!grabbedHandle) {
            selection_ = {}; // 何にも当たらなければ選択解除

            // スプライトは3Dより手前に描かれるので先に判定する（後ろのものほど手前）
            for (int i = static_cast<int>(sprites_.size()) - 1; i >= 0; --i) {
                const Transform& t = sprites_[i].sprite->GetTransform();
                if (Picking::PointInSprite(vp.mouse, t, sprites_[i].size)) {
                    selection_ = { SelectKind::Sprite, i };
                    dragMode_ = DragMode::Move;
                    dragSpriteOffset_ = { t.Translate.x - vp.mouse.x, t.Translate.y - vp.mouse.y };
                    break;
                }
            }

            // 3Dオブジェクトは一番カメラに近いものを選ぶ
            if (selection_.kind == SelectKind::None) {
                float bestT = (std::numeric_limits<float>::max)();
                int bestIndex = -1;
                for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                    Object3d& object = objects_[i].object->GetObject3d();
                    const Model* model = object.GetModel();
                    float t = 0.0f;
                    if (model && Picking::RayIntersectsBox(ray, object.GetTransform(), model->GetBoundsMin(), model->GetBoundsMax(), t) && t < bestT) {
                        bestT = t;
                        bestIndex = i;
                    }
                }

                if (bestIndex >= 0) {
                    selection_ = { SelectKind::Object, bestIndex };

                    // ドラッグ平面：通常はカメラに平行、Shiftなら水平
                    const Matrix4x4 cameraWorld = Inverse(view);
                    const Vector3 cameraForward = { cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2] };
                    const Vector3 translate = objects_[bestIndex].object->GetObject3d().GetTransform().Translate;
                    dragPlaneNormal_ = vp.shift ? Vector3{ 0.0f, 1.0f, 0.0f } : cameraForward;
                    dragPlanePoint_ = translate;

                    Vector3 hit{};
                    if (Picking::RayIntersectsPlane(ray, dragPlanePoint_, dragPlaneNormal_, hit)) {
                        dragObjectOffset_ = translate.Subtract(hit);
                        dragMode_ = DragMode::Move;
                    }
                }
            }

            // 何もない背景をつかんだら、ドラッグでカメラを平行移動する
            if (selection_.kind == SelectKind::None) {
                dragMode_ = DragMode::PanCamera;
            }
        }
    }

    //--------------------------------------------------
    // ドラッグ中
    //--------------------------------------------------
    if (dragMode_ == DragMode::None) {
        return;
    }
    if (!vp.down) {
        dragMode_ = DragMode::None;
        return;
    }

    // スプライトは画面座標で動くので、ゲーム画面の外には出さず端で止める
    const Vector2 spriteMouse = {
        (std::min)((std::max)(vp.mouse.x, 0.0f), static_cast<float>(WinApp::kGameWidth)),
        (std::min)((std::max)(vp.mouse.y, 0.0f), static_cast<float>(WinApp::kGameHeight))
    };

    if (dragMode_ == DragMode::Move) {
        if (selection_.kind == SelectKind::Sprite && selection_.index < static_cast<int>(sprites_.size())) {
            Transform& t = sprites_[selection_.index].sprite->GetTransform();
            t.Translate.x = spriteMouse.x + dragSpriteOffset_.x;
            t.Translate.y = spriteMouse.y + dragSpriteOffset_.y;
        }
        else if (selection_.kind == SelectKind::Object && selection_.index < static_cast<int>(objects_.size())) {
            Vector3 hit{};
            if (Picking::RayIntersectsPlane(ray, dragPlanePoint_, dragPlaneNormal_, hit)) {
                objects_[selection_.index].object->GetObject3d().GetTransform().Translate = hit.Add(dragObjectOffset_);
            }
        }
        else {
            dragMode_ = DragMode::None;
        }
    }
    else if (dragMode_ == DragMode::ScaleSprite && selection_.kind == SelectKind::Sprite && selection_.index < static_cast<int>(sprites_.size())) {
        SpriteSlot& slot = sprites_[selection_.index];
        Transform& t = slot.sprite->GetTransform();

        // スプライトのローカル(拡縮前)座標での各ハンドル位置
        const float w = slot.size.x;
        const float h = slot.size.y;
        const Vector2 local[8] = {
            {0.0f, 0.0f}, {w, 0.0f}, {w, h}, {0.0f, h},           // 四隅
            {w * 0.5f, 0.0f}, {w, h * 0.5f}, {w * 0.5f, h}, {0.0f, h * 0.5f} // 上・右・下・左の中点
        };
        const int opposite = (scaleHandle_ < 4) ? (scaleHandle_ + 2) % 4 : 4 + (scaleHandle_ - 4 + 2) % 4;
        const Vector2 pk = local[scaleHandle_];
        const Vector2 po = local[opposite];

        // 反対側の点からマウスへのベクトルを、スプライトの回転を打ち消した座標に直す
        const float c = std::cos(t.Rotate.z);
        const float s = std::sin(t.Rotate.z);
        const float rx = spriteMouse.x - scaleAnchor_.x;
        const float ry = spriteMouse.y - scaleAnchor_.y;
        const float dx = rx * c + ry * s;
        const float dy = -rx * s + ry * c;

        // 新しいScale。辺のハンドルは、動かさない軸のScaleはそのまま
        float newScaleX = (pk.x != po.x) ? dx / (pk.x - po.x) : scaleStart_.x;
        float newScaleY = (pk.y != po.y) ? dy / (pk.y - po.y) : scaleStart_.y;

        // 四隅は縦横比を保つ（Shiftで自由）
        if (scaleHandle_ < 4 && !vp.shift && scaleStart_.x != 0.0f && scaleStart_.y != 0.0f) {
            const float factor = (newScaleX / scaleStart_.x + newScaleY / scaleStart_.y) * 0.5f;
            newScaleX = scaleStart_.x * factor;
            newScaleY = scaleStart_.y * factor;
        }

        // 小さくなりすぎ・反転はスキップ
        if (newScaleX >= 0.01f && newScaleY >= 0.01f) {
            t.Scale.x = newScaleX;
            t.Scale.y = newScaleY;
            // 反対側の点が動かないようにTranslateを求め直す
            const float px = po.x * newScaleX;
            const float py = po.y * newScaleY;
            t.Translate.x = scaleAnchor_.x - (px * c - py * s);
            t.Translate.y = scaleAnchor_.y - (px * s + py * c);
        }
    }
    else if (dragMode_ == DragMode::ScaleObject && selection_.kind == SelectKind::Object && selection_.index < static_cast<int>(objects_.size())) {
        const float cx = vp.mouse.x - scaleCenter_.x;
        const float cy = vp.mouse.y - scaleCenter_.y;
        float distance = std::sqrt(cx * cx + cy * cy);
        if (scaleHandle_ >= 4) {
            distance = std::fabs((scaleHandle_ == 5 || scaleHandle_ == 7) ? cx : cy);
        }
        const float factor = distance / scaleStartDistance_;
        if (factor >= 0.01f) {
            Transform& t = objects_[selection_.index].object->GetObject3d().GetTransform();
            t.Scale = scaleStart_;
            if (scaleAxis_ < 0) {
                t.Scale = { scaleStart_.x * factor, scaleStart_.y * factor, scaleStart_.z * factor };
            }
            else if (scaleAxis_ == 0) {
                t.Scale.x = scaleStart_.x * factor;
            }
            else if (scaleAxis_ == 1) {
                t.Scale.y = scaleStart_.y * factor;
            }
            else {
                t.Scale.z = scaleStart_.z * factor;
            }
        }
    }
    else {
        dragMode_ = DragMode::None;
    }
#endif // USE_IMGUI
}

// 選択中の項目のハンドル位置を求める。選択なし・画面外なら false
// 0～3：四隅（左上・右上・右下・左下）、4～7：辺の中点（上・右・下・左）
bool GameScene::GetSelectionHandles(Vector2 outHandles[8]) const
{
    Vector2 corners[4];

    if (selection_.kind == SelectKind::Sprite && selection_.index < static_cast<int>(sprites_.size())) {
        Picking::GetSpriteCorners(sprites_[selection_.index].sprite->GetTransform(), sprites_[selection_.index].size, corners);
    }
    else if (selection_.kind == SelectKind::Object && selection_.index < static_cast<int>(objects_.size())) {
        Object3d& object = objects_[selection_.index].object->GetObject3d();
        const Model* model = object.GetModel();
        if (!model) {
            return false;
        }

        // 外接ボックスの8頂点を画面に投影して、それを囲む矩形にする
        const Transform& t = object.GetTransform();
        const Matrix4x4 world = Matrix4x4::MakeAffineMatrix(t.Scale, t.Rotate, t.Translate);
        const Vector3& bmin = model->GetBoundsMin();
        const Vector3& bmax = model->GetBoundsMax();

        float minX = (std::numeric_limits<float>::max)(), minY = minX;
        float maxX = -minX, maxY = -minX;
        for (int i = 0; i < 8; ++i) {
            const Vector3 local = { (i & 1) ? bmax.x : bmin.x, (i & 2) ? bmax.y : bmin.y, (i & 4) ? bmax.z : bmin.z };
            // 行ベクトル×ワールド行列
            const Vector3 p = {
                local.x * world.m[0][0] + local.y * world.m[1][0] + local.z * world.m[2][0] + world.m[3][0],
                local.x * world.m[0][1] + local.y * world.m[1][1] + local.z * world.m[2][1] + world.m[3][1],
                local.x * world.m[0][2] + local.y * world.m[1][2] + local.z * world.m[2][2] + world.m[3][2]
            };
            Vector2 screen{};
            if (!Picking::WorldToScreen(p, camera_->GetViewMatrix(), camera_->GetProjectionMatrix(), screen)) {
                return false; // カメラの後ろにはみ出す場合は枠を出さない
            }
            minX = (std::min)(minX, screen.x);
            maxX = (std::max)(maxX, screen.x);
            minY = (std::min)(minY, screen.y);
            maxY = (std::max)(maxY, screen.y);
        }
        corners[0] = { minX, minY };
        corners[1] = { maxX, minY };
        corners[2] = { maxX, maxY };
        corners[3] = { minX, maxY };
    }
    else {
        return false;
    }

    for (int i = 0; i < 4; ++i) {
        outHandles[i] = corners[i];
        const Vector2& next = corners[(i + 1) % 4];
        outHandles[4 + i] = { (corners[i].x + next.x) * 0.5f, (corners[i].y + next.y) * 0.5f }; // 上・右・下・左
    }
    return true;
}

// 選択中の項目を囲む枠とハンドルをViewportに重ねて描く
void GameScene::DrawSelectionOutline()
{
#ifdef USE_IMGUI
    Vector2 handles[8];
    if (GetSelectionHandles(handles)) {
        EditorLayout::DrawViewportOverlay(handles, 4, true);
        EditorLayout::DrawViewportHandles(handles, 8);
    }
#endif // USE_IMGUI
}

// コナミコマンド（↑↑↓↓←→←→BA）判定
// 成立するたびに全オブジェクトの「パーティーモード」演出をトグルする
void GameScene::UpdateKonami()
{
    if (!konamiCommand_.Update(*ctx_->input)) {
        return;
    }

    konamiPartyMode_ = !konamiPartyMode_;
    Logger::Log(konamiPartyMode_ ? "Konami Code accepted! Party mode ON" : "Konami Code accepted! Party mode OFF");

    if (konamiPartyMode_) {
        // ON: 現在の色を退避しておく（OFFにした時に戻すため）
        for (auto& slot : objects_) {
            slot.konamiOriginalColor = slot.object->GetObject3d().GetMaterial()->color;
        }
    }
    else {
        // OFF: 退避しておいた色に戻す
        for (auto& slot : objects_) {
            slot.object->GetObject3d().GetMaterial()->color = slot.konamiOriginalColor;
        }
    }
}

// 各オブジェクトのWVPを更新する
void GameScene::UpdateObjects()
{
    const Matrix4x4& viewMatrix = camera_->GetViewMatrix();
    const Matrix4x4& projectionMatrix = camera_->GetProjectionMatrix();

    if (konamiPartyMode_) {
        konamiTime_ += 0.05f;
    }

    for (auto& slot : objects_) {
        slot.object->Update();
        Object3d& object = slot.object->GetObject3d();

        // 描画用のTransform（パーティーモード中は演出を上乗せする。元のTransformは変えない）
        Transform renderTransform = object.GetTransform();

        if (konamiPartyMode_) {
            // パーティーモード：高速回転＋軸ごとに位相をずらした派手な拡縮パルス＋虹色サイクル
            renderTransform.Rotate.x += konamiTime_ * 4.0f;
            renderTransform.Rotate.y += konamiTime_ * 6.0f;

            const float kPulseAmplitude = 0.6f;
            const float kPulseSpeed = 5.0f;
            float pulseX = 1.0f + kPulseAmplitude * std::sinf(konamiTime_ * kPulseSpeed);
            float pulseY = 1.0f + kPulseAmplitude * std::sinf(konamiTime_ * kPulseSpeed + 2.094f); // +120度ずらす
            float pulseZ = 1.0f + kPulseAmplitude * std::sinf(konamiTime_ * kPulseSpeed + 4.188f); // +240度ずらす
            renderTransform.Scale = {
                object.GetTransform().Scale.x * pulseX,
                object.GetTransform().Scale.y * pulseY,
                object.GetTransform().Scale.z * pulseZ
            };

            float hue = std::fmodf(konamiTime_ * 90.0f, 360.0f);
            object.GetMaterial()->color = HueToColor(hue);
        }

        object.Update(renderTransform, viewMatrix, projectionMatrix);
    }
}

//==================================================
// 描画
//==================================================
void GameScene::Draw()
{
    // 3Dオブジェクト（MultiMesh/MultiMaterial対応はModel側で処理）
    ctx_->object3dCommon->PreDraw();

    // ① 不透明(None)を先に描く
    for (auto& slot : objects_) {
        if (slot.object->GetObject3d().GetBlendMode() == BlendMode::None) {
            slot.object->Draw();
        }
    }

    // ② ブレンドありは、カメラから遠い順に描く（近い物ほど後から重ねるため）
    {
        const Matrix4x4 cameraWorld = Inverse(camera_->GetViewMatrix());
        const Vector3 cameraPosition = { cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2] };

        std::vector<std::pair<float, GameObject*>> blended;
        for (auto& slot : objects_) {
            Object3d& object = slot.object->GetObject3d();
            if (object.GetBlendMode() == BlendMode::None) {
                continue;
            }
            const Vector3 diff = object.GetTransform().Translate.Subtract(cameraPosition);
            blended.emplace_back(diff.Dot(diff), slot.object.get());
        }
        std::sort(blended.begin(), blended.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
        for (auto& entry : blended) {
            entry.second->Draw();
        }
    }

    // Sprite（Hierarchyの上から順に重ねて描く）
    ctx_->spriteCommon->PreDraw();
    for (auto& slot : sprites_) {
        slot.sprite->Draw();
    }

    // パーティクル（専用のルートシグネチャ/PSOに切り替える。最後に描くこと）
    ctx_->particleSystem->Draw();
}

//==================================================
// エディタUI
//==================================================
void GameScene::DrawImGui()
{
#ifdef USE_IMGUI
    DrawSelectionOutline();

    const int objectCount = static_cast<int>(objects_.size());
    const int spriteCount = static_cast<int>(sprites_.size());

    // 「追加」メニューの中身（[+ Add]ボタンと、Hierarchyの空きスペースの右クリックで共通）
    auto drawAddMenu = [&]() {
        if (ImGui::BeginMenu("Add Object")) {
            for (int i = 0; i < static_cast<int>(objFileList_.size()); ++i) {
                if (ImGui::MenuItem(objFileList_[i].c_str())) {
                    pendingCommands_.push_back([this, i]() {
                        const int newIndex = AddObject(i, { 0.0f, 0.0f, 0.0f });
                        selection_ = { SelectKind::Object, newIndex };
                        });
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add Sprite")) {
            for (int i = 0; i < static_cast<int>(textureFileList_.size()); ++i) {
                if (ImGui::MenuItem(textureFileList_[i].c_str())) {
                    pendingCommands_.push_back([this, i]() {
                        const int newIndex = AddSprite(i, { 200.0f, 200.0f });
                        sprites_[newIndex].sprite->GetTransform().Translate = { 100.0f, 100.0f, 0.0f };
                        selection_ = { SelectKind::Sprite, newIndex };
                        });
                }
            }
            ImGui::EndMenu();
        }
        };

    //--------------------------------------------------
    // Hierarchy（左）：シーン内の項目一覧。クリックで選択、右クリックでメニュー
    //--------------------------------------------------
    EditorLayout::Begin("Hierarchy", EditorLayout::Panel::Hierarchy);
    ImGui::Text("Scene: Game");
    if (ImGui::Button("+ Add")) {
        ImGui::OpenPopup("AddPopup");
    }
    if (ImGui::BeginPopup("AddPopup")) {
        drawAddMenu();
        ImGui::EndPopup();
    }
    ImGui::Separator();

    // 3Dオブジェクト
    for (int i = 0; i < objectCount; ++i) {
        std::string label = std::format("[Obj {}] {}", i, objFileList_[objects_[i].objIndex]);
        const bool selected = selection_.kind == SelectKind::Object && selection_.index == i;
        ImGui::PushID(i);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selection_ = { SelectKind::Object, i };
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            selection_ = { SelectKind::Object, i };
        }
        if (ImGui::BeginPopupContextItem("ObjectContext")) {
            if (ImGui::MenuItem("Duplicate")) {
                pendingCommands_.push_back([this, i]() { DuplicateObject(i); });
            }
            if (ImGui::MenuItem("Delete")) {
                pendingCommands_.push_back([this, i]() {
                    if (i < static_cast<int>(objects_.size())) {
                        objects_.erase(objects_.begin() + i);
                        selection_ = {};
                    }
                    });
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // スプライト
    for (int i = 0; i < spriteCount; ++i) {
        std::string label = std::format("[Sprite {}] {}", i, textureFileList_[sprites_[i].texIndex]);
        const bool selected = selection_.kind == SelectKind::Sprite && selection_.index == i;
        ImGui::PushID(1000 + i);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selection_ = { SelectKind::Sprite, i };
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            selection_ = { SelectKind::Sprite, i };
        }
        if (ImGui::BeginPopupContextItem("SpriteContext")) {
            if (ImGui::MenuItem("Duplicate")) {
                pendingCommands_.push_back([this, i]() { DuplicateSprite(i); });
            }
            if (ImGui::MenuItem("Delete")) {
                pendingCommands_.push_back([this, i]() {
                    if (i < static_cast<int>(sprites_.size())) {
                        sprites_.erase(sprites_.begin() + i);
                        selection_ = {};
                    }
                    });
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // ライト（削除不可）
    if (ImGui::Selectable("Directional Light", selection_.kind == SelectKind::Light)) {
        selection_ = { SelectKind::Light, 0 };
    }

    ImGui::Separator();
    ImGui::Text("Party Mode: %s", konamiPartyMode_ ? "ON" : "OFF");
    ImGui::Text("Particles: %zu / %u", ctx_->particleSystem->GetCount(), ParticleSystem::kMaxParticles);
    ImGui::Text("Viewport: click = select, drag = move");
    ImGui::Text("Drag 3D to the edge: camera follows");
    ImGui::Text("Drag edge = stretch that way");
    ImGui::Text("Drag corner = scale (Shift: free)");
    ImGui::Text("(Shift+drag 3D: move on the ground)");
    ImGui::Text("Viewport right-hold: camera");
    ImGui::Text("  mouse=look WASD=move Space/Shift=up/down");
    ImGui::Text("Right click list: Add / Duplicate / Delete");
    ImGui::Text("BACKSPACE: back to Title");

    // 項目の上以外（空きスペース）を右クリックしたときのメニュー
    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        drawAddMenu();
        ImGui::EndPopup();
    }
    ImGui::End();

    //--------------------------------------------------
    // Inspector（右）：選択中の項目のプロパティ
    //--------------------------------------------------
    EditorLayout::Begin("Inspector", EditorLayout::Panel::Inspector);

    if (selection_.kind == SelectKind::Object && selection_.index < objectCount) {
        ObjectSlot& slot = objects_[selection_.index];
        Object3d& object = slot.object->GetObject3d();

        ImGui::Text("Object [%d]", selection_.index);
        ImGui::Separator();
        ImGui::Combo("Obj File", &slot.objIndex, objFileListCStr_.data(), static_cast<int>(objFileListCStr_.size()));
        ImGui::DragFloat3("Scale", &object.GetTransform().Scale.x, 0.01f);
        ImGui::DragFloat3("Rotate", &object.GetTransform().Rotate.x, 0.01f);
        ImGui::DragFloat3("Translate", &object.GetTransform().Translate.x, 0.01f);
        ImGui::ColorEdit4("Color", &object.GetMaterial()->color.x);
        const char* lightingModes[] = { "normal", "Lambert", "Half Lambert" };
        ImGui::Combo("Lighting Mode", &object.GetMaterial()->lightingMode, lightingModes, IM_ARRAYSIZE(lightingModes));
        int blendMode = static_cast<int>(object.GetBlendMode());
        if (ImGui::Combo("Blend Mode", &blendMode, kBlendModeNames, kBlendModeCount)) {
            object.SetBlendMode(static_cast<BlendMode>(blendMode));
        }
        ImGui::SliderFloat("Alpha Cutoff", &object.GetMaterial()->alphaCutoff, 0.0f, 1.0f);
        bool doubleSided = object.IsDoubleSided();
        if (ImGui::Checkbox("Double Sided", &doubleSided)) {
            object.SetDoubleSided(doubleSided);
        }
        if (ImGui::CollapsingHeader("UV Transform")) {
            ImGui::DragFloat2("UVTranslate", &object.GetUvTransform().Translate.x, 0.01f, -10.0f, 10.0f);
            ImGui::DragFloat2("UVScale", &object.GetUvTransform().Scale.x, 0.01f, -10.0f, 10.0f);
            ImGui::SliderAngle("UVRotate", &object.GetUvTransform().Rotate.z);
        }
    }
    else if (selection_.kind == SelectKind::Sprite && selection_.index < spriteCount) {
        SpriteSlot& slot = sprites_[selection_.index];
        Sprite& sprite = *slot.sprite;

        ImGui::Text("Sprite [%d]", selection_.index);
        ImGui::Separator();
        ImGui::Combo("Texture", &slot.texIndex, textureFileListCStr_.data(), static_cast<int>(textureFileListCStr_.size()));
        ImGui::DragFloat3("Scale", &sprite.GetTransform().Scale.x, 0.01f);
        ImGui::DragFloat3("Rotate", &sprite.GetTransform().Rotate.x, 0.01f);
        ImGui::DragFloat3("Translate", &sprite.GetTransform().Translate.x, 1.0f);
        ImGui::ColorEdit4("Color", &sprite.GetMaterial()->color.x);
        int blendMode = static_cast<int>(sprite.GetBlendMode());
        if (ImGui::Combo("Blend Mode", &blendMode, kBlendModeNames, kBlendModeCount)) {
            sprite.SetBlendMode(static_cast<BlendMode>(blendMode));
        }
        ImGui::SliderFloat("Alpha Cutoff", &sprite.GetMaterial()->alphaCutoff, 0.0f, 1.0f);
        if (ImGui::CollapsingHeader("UV Transform")) {
            ImGui::DragFloat2("UVTranslate", &sprite.GetUvTransform().Translate.x, 0.01f, -10.0f, 10.0f);
            ImGui::DragFloat2("UVScale", &sprite.GetUvTransform().Scale.x, 0.01f, -10.0f, 10.0f);
            ImGui::SliderAngle("UVRotate", &sprite.GetUvTransform().Rotate.z);
        }
    }
    else if (selection_.kind == SelectKind::Light) {
        ImGui::Text("Directional Light");
        ImGui::Separator();
        ImGui::ColorEdit4("Color", &directionalLight_->color.x);
        if (ImGui::DragFloat3("Direction", &directionalLight_->direction.x, 0.01f)) {
            directionalLight_->direction = Normalize(directionalLight_->direction);
        }
        ImGui::DragFloat("Intensity", &directionalLight_->intensity, 0.01f);
    }
    else {
        ImGui::Text("Nothing selected");
    }

    ImGui::End();
#endif // USE_IMGUI
}
