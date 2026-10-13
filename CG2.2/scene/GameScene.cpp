#include "GameScene.h"
#include "TitleScene.h"
#include "scene/SceneManager.h"

#include "base/Logger.h"
#include "base/WinApp.h"
#include "base/StringUtility.h"
#include "2d/SpriteCommon.h"
#include "3d/ModelLoader.h"
#include "3d/Object3dCommon.h"
#include "3d/InstancedPlanes.h"
#include "camera/DebugCamera.h"
#include "editor/EditorLayout.h"
#include "editor/ViewportPicking.h"
#include "3d/Model.h"
#include "input/Input.h"
#include "math/Color.h"
#include "particle/ParticleSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <limits>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
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
    emitters_.clear();
    planes_.clear();
    pendingCommands_.clear();

    // 最初は何も置かない（Hierarchyの右クリック / [+ Add] から追加する）

    directionalLight_ = ctx_->object3dCommon->GetDirectionalLight();

    konamiCommand_ = KonamiCommand{};
    konamiPartyMode_ = false;
    konamiTime_ = 0.0f;
    selection_ = {};
    dragMode_ = DragMode::None;
    playing_ = false;

    ctx_->particleSystem->Clear();
}

void GameScene::Finalize()
{
    pendingCommands_.clear();
    objects_.clear();
    sprites_.clear();
    emitters_.clear();
    planes_.clear();
    debugCamera_ = nullptr;
    camera_.reset();
    ctx_->particleSystem->Clear();
}

// resourcesフォルダのobj/画像を調べて、追加メニューの選択肢にする
// Copy a file into resources/ without overwriting an existing file. Returns false if the source is missing.
static bool CopyToResources(const std::filesystem::path& src)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
        return false;
    }
    fs::create_directories("resources", ec);
    const fs::path dst = fs::path("resources") / src.filename();
    if (fs::exists(dst, ec)) {
        return true; // already there: reuse it
    }
    fs::copy_file(src, dst, fs::copy_options::skip_existing, ec);
    return !ec;
}

// Copy the .mtl referenced by an .obj and the textures referenced by that .mtl (same folder as the source)
static void CopyObjCompanions(const std::filesystem::path& objPath)
{
    namespace fs = std::filesystem;
    std::ifstream objFile(objPath);
    std::string line;
    while (std::getline(objFile, line)) {
        std::istringstream stream(line);
        std::string key, value;
        stream >> key;
        if (key != "mtllib" || !(stream >> value)) {
            continue;
        }
        const fs::path mtlSrc = objPath.parent_path() / fs::path(value).filename();
        CopyToResources(mtlSrc);

        std::ifstream mtlFile(mtlSrc);
        std::string mtlLine;
        while (std::getline(mtlFile, mtlLine)) {
            std::istringstream mtlStream(mtlLine);
            std::string mtlKey, texName, last;
            mtlStream >> mtlKey;
            if (mtlKey != "map_Kd") {
                continue;
            }
            while (mtlStream >> last) {
                texName = last; // the file name is the last token
            }
            if (!texName.empty()) {
                CopyToResources(mtlSrc.parent_path() / fs::path(texName).filename());
            }
        }
    }
}

void GameScene::ImportDroppedFiles()
{
    const std::vector<std::wstring> files = WinApp::TakeDroppedFiles();
    if (files.empty() || playing_) {
        return;
    }

    for (const std::wstring& file : files) {
        const std::filesystem::path src(file);
        // The engine expects UTF-8 paths (filename().string() would be the ANSI code page)
        const std::string name = StringUtility::ConvertString(src.filename().wstring());

        if (HasExtension(src, { ".obj" })) {
            CopyObjCompanions(src);
            if (!CopyToResources(src)) {
                continue;
            }
            auto it = std::find(objFileList_.begin(), objFileList_.end(), name);
            if (it == objFileList_.end()) {
                objFileList_.push_back(name);
                it = objFileList_.end() - 1;
            }
            const int index = AddObject(static_cast<int>(it - objFileList_.begin()), { 0.0f, 0.0f, 0.0f });
            selection_ = { SelectKind::Object, index };
        }
        else if (HasExtension(src, { ".mp4", ".m4v", ".mov", ".wmv" })) {
            // Videos are not copied (they are large): play them from where they are
            // Normal drop: a 3D screen in the scene. Shift + drop: a 2D sprite on top of the screen.
            const std::string videoPath = StringUtility::ConvertString(src.wstring());
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                const int index = AddVideoSprite(videoPath);
                if (index >= 0) {
                    selection_ = { SelectKind::Sprite, index };
                }
            }
            else {
                const int index = AddVideoObject(videoPath);
                if (index >= 0) {
                    selection_ = { SelectKind::Object, index };
                }
            }
        }
        else if (HasExtension(src, { ".png", ".jpg", ".jpeg", ".bmp" })) {
            if (!CopyToResources(src)) {
                continue;
            }
            const std::string path = "resources/" + name;
            auto it = std::find(textureFileList_.begin(), textureFileList_.end(), path);
            if (it == textureFileList_.end()) {
                textureFileList_.push_back(path);
                it = textureFileList_.end() - 1;
            }
            const int index = AddSprite(static_cast<int>(it - textureFileList_.begin()), { 200.0f, 200.0f });
            selection_ = { SelectKind::Sprite, index };
        }
    }

    // Rebuild the C-string lists used by the ImGui combo boxes
    objFileListCStr_.clear();
    for (const auto& n : objFileList_) {
        objFileListCStr_.push_back(n.c_str());
    }
    textureFileListCStr_.clear();
    for (const auto& n : textureFileList_) {
        textureFileListCStr_.push_back(n.c_str());
    }
}

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

int GameScene::AddVideoObject(const std::string& utf8Path)
{
    ObjectSlot slot;
    slot.video = std::make_unique<VideoPlayer>();
    if (!slot.video->Initialize(ctx_->dxCommon, ctx_->srvManager, ctx_->audio, utf8Path)) {
        return -1;
    }

    // A video screen is the plane model with the video frame as its texture
    const auto it = std::find(objFileList_.begin(), objFileList_.end(), "plane.obj");
    const int planeIndex = (it == objFileList_.end()) ? 0 : static_cast<int>(it - objFileList_.begin());

    slot.object = std::make_unique<GameObject>();
    slot.object->Initialize(ctx_->object3dCommon, objFileList_[planeIndex], { 0.0f, 1.5f, 0.0f });
    Object3d& object = slot.object->GetObject3d();
    object.SetExternalTexture(slot.video->GetSrvHandle());
    object.SetDoubleSided(true);                // visible from behind too
    object.GetMaterial()->lightingMode = 0;     // unlit, like a screen
    object.GetMaterial()->alphaCutoff = 0.0f;
    // plane.obj is 2 x 2: keep the video's aspect ratio (height 3)
    const float aspect = static_cast<float>(slot.video->GetWidth()) / static_cast<float>(slot.video->GetHeight());
    object.GetTransform().Scale = { 1.5f * aspect, 1.5f, 1.0f };

    slot.objIndex = planeIndex;
    slot.loadedObjIndex = planeIndex;
    slot.konamiOriginalColor = object.GetMaterial()->color;
    slot.videoName = StringUtility::ConvertString(std::filesystem::path(StringUtility::ConvertString(utf8Path)).filename().wstring()); // UTF-8 for ImGui
    slot.video->Play();

    objects_.push_back(std::move(slot));
    return static_cast<int>(objects_.size()) - 1;
}

int GameScene::AddVideoSprite(const std::string& utf8Path)
{
    SpriteSlot slot;
    slot.video = std::make_unique<VideoPlayer>();
    if (!slot.video->Initialize(ctx_->dxCommon, ctx_->srvManager, ctx_->audio, utf8Path)) {
        return -1;
    }

    // Keep the video's aspect ratio (width 640)
    const float width = 640.0f;
    const float height = width * static_cast<float>(slot.video->GetHeight()) / static_cast<float>(slot.video->GetWidth());
    slot.size = { width, height };

    slot.sprite = std::make_unique<Sprite>();
    slot.sprite->Initialize(ctx_->spriteCommon, textureFileList_[0], slot.size); // placeholder texture, replaced below
    slot.sprite->SetExternalTexture(slot.video->GetSrvHandle());
    slot.texIndex = 0;
    slot.loadedTexIndex = 0;
    slot.videoName = StringUtility::ConvertString(std::filesystem::path(StringUtility::ConvertString(utf8Path)).filename().wstring()); // UTF-8 for ImGui
    slot.video->Play();

    sprites_.push_back(std::move(slot));
    return static_cast<int>(sprites_.size()) - 1;
}

void GameScene::DuplicateObject(int index)
{
    if (index < 0 || index >= static_cast<int>(objects_.size())) {
        return;
    }
    // 参照を保持したままpush_backすると無効になるので、値をコピーしてから追加する
    if (objects_[index].video) {
        return; // videos cannot be duplicated
    }
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
    if (sprites_[index].video) {
        return; // videos cannot be duplicated
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

int GameScene::AddPlanes(int texIndex, const Vector3& position)
{
    PlanesSlot slot;
    slot.planes = std::make_unique<InstancedPlanes>();
    slot.planes->Initialize(ctx_->instancedPlaneCommon, textureFileList_[texIndex]);
    slot.planes->GetSettings().base.Translate = position;
    slot.texIndex = texIndex;
    slot.loadedTexIndex = texIndex;
    planes_.push_back(std::move(slot));
    return static_cast<int>(planes_.size()) - 1;
}

void GameScene::DuplicatePlanes(int index)
{
    if (index < 0 || index >= static_cast<int>(planes_.size())) {
        return;
    }
    const int srcTex = planes_[index].texIndex;
    const InstancedPlanes::Settings settings = planes_[index].planes->GetSettings(); // push_backで無効になる前にコピー

    const int newIndex = AddPlanes(srcTex, settings.base.Translate);
    planes_[newIndex].planes->GetSettings() = settings;
    planes_[newIndex].planes->GetSettings().base.Translate.y += 3.0f; // 重ならないように少しずらす

    selection_ = { SelectKind::Planes, newIndex };
}

int GameScene::AddEmitter(ParticlePreset preset, const Vector3& position)
{
    ParticleEmitter emitter;
    emitter.ApplyPreset(preset);
    emitter.position = position;
    emitters_.push_back(emitter);
    return static_cast<int>(emitters_.size()) - 1;
}

void GameScene::DuplicateEmitter(int index)
{
    if (index < 0 || index >= static_cast<int>(emitters_.size())) {
        return;
    }
    ParticleEmitter copy = emitters_[index];
    copy.position.x += 1.0f; // 重ならないように少しずらす
    copy.emitAccumulator = 0.0f;
    copy.burstTimer = 0.0f;
    copy.playRequested = false;
    emitters_.push_back(copy);
    selection_ = { SelectKind::Emitter, static_cast<int>(emitters_.size()) - 1 };
}

// 選択中の項目を削除する（編集モードのみ）
void GameScene::DeleteSelection()
{
    if (playing_) {
        return;
    }
    const int index = selection_.index;
    if (selection_.kind == SelectKind::Object && index < static_cast<int>(objects_.size())) {
        objects_.erase(objects_.begin() + index);
    }
    else if (selection_.kind == SelectKind::Sprite && index < static_cast<int>(sprites_.size())) {
        sprites_.erase(sprites_.begin() + index);
    }
    else if (selection_.kind == SelectKind::Planes && index < static_cast<int>(planes_.size())) {
        planes_.erase(planes_.begin() + index);
    }
    else if (selection_.kind == SelectKind::Emitter && index < static_cast<int>(emitters_.size())) {
        emitters_.erase(emitters_.begin() + index);
    }
    else {
        return;
    }
    selection_ = {};
    dragMode_ = DragMode::None;
}

// 実行開始：Play前の状態を保存する。実行中に動いた位置・色などはStopで元に戻る
// （実行中は追加・複製・削除ができないので、項目の数と並びは変わらない）
void GameScene::StartPlay()
{
    if (playing_) {
        return;
    }

    snapshot_ = {};
    for (auto& slot : objects_) {
        Object3d& object = slot.object->GetObject3d();
        snapshot_.objects.push_back({ object.GetTransform(), object.GetUvTransform(), object.GetMaterial()->color });
    }
    for (auto& slot : sprites_) {
        Sprite& sprite = *slot.sprite;
        snapshot_.sprites.push_back({ sprite.GetTransform(), sprite.GetUvTransform(), sprite.GetMaterial()->color });
    }

    // エミッターは発生状態をリセットしてから保存する
    for (auto& emitter : emitters_) {
        emitter.emitAccumulator = 0.0f;
        emitter.burstTimer = 0.0f;
        emitter.playRequested = false;
    }
    for (auto& slot : planes_) {
        snapshot_.planes.push_back(slot.planes->GetSettings());
    }
    snapshot_.emitters = emitters_;

    ctx_->particleSystem->Clear(); // 編集中のプレビューの粒は消す
    konamiCommand_ = KonamiCommand{};
    konamiPartyMode_ = false;
    konamiTime_ = 0.0f;
    dragMode_ = DragMode::None;
    playing_ = true;
}

// 実行停止：保存しておいた状態に戻す
void GameScene::StopPlay()
{
    if (!playing_) {
        return;
    }
    playing_ = false;
    konamiPartyMode_ = false;
    konamiTime_ = 0.0f;
    dragMode_ = DragMode::None;

    for (size_t i = 0; i < objects_.size() && i < snapshot_.objects.size(); ++i) {
        Object3d& object = objects_[i].object->GetObject3d();
        object.GetTransform() = snapshot_.objects[i].transform;
        object.GetUvTransform() = snapshot_.objects[i].uvTransform;
        object.GetMaterial()->color = snapshot_.objects[i].color;
        objects_[i].konamiOriginalColor = snapshot_.objects[i].color;
    }
    for (size_t i = 0; i < sprites_.size() && i < snapshot_.sprites.size(); ++i) {
        Sprite& sprite = *sprites_[i].sprite;
        sprite.GetTransform() = snapshot_.sprites[i].transform;
        sprite.GetUvTransform() = snapshot_.sprites[i].uvTransform;
        sprite.GetMaterial()->color = snapshot_.sprites[i].color;
    }
    for (size_t i = 0; i < planes_.size() && i < snapshot_.planes.size(); ++i) {
        planes_[i].planes->GetSettings() = snapshot_.planes[i];
    }
    if (emitters_.size() == snapshot_.emitters.size()) {
        emitters_ = snapshot_.emitters;
    }

    ctx_->particleSystem->Clear(); // 実行中の粒を消す
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
    ImportDroppedFiles();

    // ImGuiでobj/テクスチャが切り替えられたものを差し替える
    // （読み込み済みのものはManagerのキャッシュから再利用される）
    for (auto& slot : objects_) {
        if (slot.video) {
            slot.video->Update(); // decode + upload the current frame
        }
        if (slot.objIndex != slot.loadedObjIndex) {
            slot.object->GetObject3d().SetModel(objFileList_[slot.objIndex]);
            slot.loadedObjIndex = slot.objIndex;
        }
    }
    for (auto& slot : sprites_) {
        if (slot.video) {
            slot.video->Update(); // decode + upload the current frame
            continue;
        }
        if (slot.texIndex != slot.loadedTexIndex) {
            slot.sprite->SetTexture(textureFileList_[slot.texIndex]);
            slot.loadedTexIndex = slot.texIndex;
        }
    }

    // Deleteキーで選択中の項目を削除（文字入力中は無効）
#ifdef USE_IMGUI
    const bool typing = ImGui::GetIO().WantTextInput;
#else
    const bool typing = false;
#endif
    if (!playing_ && !typing && input.TriggerKey(DIK_DELETE)) {
        DeleteSelection();
    }

    for (auto& slot : planes_) {
        if (slot.texIndex != slot.loadedTexIndex) {
            slot.planes->SetTexture(textureFileList_[slot.texIndex]);
            slot.loadedTexIndex = slot.texIndex;
        }
    }

    UpdateCameraControl();
    camera_->Update(input);
    if (playing_) {
        dragMode_ = DragMode::None; // 実行中は選択・ドラッグ移動をしない
        UpdateKonami();             // コナミコマンドは実行中だけ有効
    }
    else {
        UpdateViewportInteraction();
    }
    UpdateObjects();

    // 板ポリ大量配置：全ての板のWVPを書き込む
    for (auto& slot : planes_) {
        slot.planes->Update(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
    }

    // パーティクル：エミッターが粒を発生させ、そのあと全ての粒を更新する
    // （パーティーモードOFFでも生存中のものは更新される）
    if (playing_) {
        for (auto& emitter : emitters_) {
            ctx_->particleSystem->UpdateEmitter(emitter);
        }
    }
    else {
        // 編集中は発生させない。Inspectorの「Play (burst now)」だけ、その場で1回プレビューする
        for (auto& emitter : emitters_) {
            if (emitter.playRequested) {
                ParticleEmitter preview = emitter;
                preview.enabled = false; // 自動発生はさせず、バーストだけ実行する
                ctx_->particleSystem->UpdateEmitter(preview);
                emitter.playRequested = false;
            }
        }
    }
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
    if (vp.middleClicked) { middleActive_ = true; }
    if (!vp.middleDown) { middleActive_ = false; }
    const bool looking = middleActive_; // right button no longer rotates (WASD only)
    debugCamera_->SetMouseLook(looking ? vp.mouseDelta.x : 0.0f, looking ? vp.mouseDelta.y : 0.0f);
    debugCamera_->SetZoom(vp.wheel); // ホイールはViewportにマウスが乗っていれば常に有効

    // 左クリックでオブジェクトをドラッグ中、カーソルが画面の端に来たらWASD(A/D・Space/Shift)と
    // 同じ速度でカメラを動かす。左端=A、右端=D、上端=Space(上)、下端=LShift(下)
    float panRight = 0.0f;
    float panUp = 0.0f;
    if (dragMode_ == DragMode::Move && (selection_.kind == SelectKind::Object || selection_.kind == SelectKind::Planes || selection_.kind == SelectKind::Emitter) && vp.down) {
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
    if (vp.clicked && !cameraActive_ && !middleActive_) {
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

            // パーティクルエミッターは、目印(画面上の小さな菱形)の近くをクリックしたら選ぶ（3Dより優先）
            if (selection_.kind == SelectKind::None) {
                constexpr float kPickRadius = 16.0f; // ゲーム画面ピクセル
                float bestDistance2 = kPickRadius * kPickRadius;
                int bestEmitter = -1;
                for (int i = 0; i < static_cast<int>(emitters_.size()); ++i) {
                    Vector2 screen{};
                    if (!Picking::WorldToScreen(emitters_[i].position, view, projection, screen)) {
                        continue;
                    }
                    const float dx = vp.mouse.x - screen.x;
                    const float dy = vp.mouse.y - screen.y;
                    const float distance2 = dx * dx + dy * dy;
                    if (distance2 <= bestDistance2) {
                        bestDistance2 = distance2;
                        bestEmitter = i;
                    }
                }

                if (bestEmitter >= 0) {
                    selection_ = { SelectKind::Emitter, bestEmitter };

                    // ドラッグ平面：通常はカメラに平行、Shiftなら水平
                    const Matrix4x4 cameraWorld = Inverse(view);
                    const Vector3 cameraForward = { cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2] };
                    const Vector3 position = emitters_[bestEmitter].position;
                    dragPlaneNormal_ = vp.shift ? Vector3{ 0.0f, 1.0f, 0.0f } : cameraForward;
                    dragPlanePoint_ = position;

                    Vector3 hit{};
                    if (Picking::RayIntersectsPlane(ray, dragPlanePoint_, dragPlaneNormal_, hit)) {
                        dragObjectOffset_ = position.Subtract(hit);
                        dragMode_ = DragMode::Move;
                    }
                }
            }

            // 板ポリ大量配置：どれか1枚でも当たれば選ぶ。ただし3Dオブジェクトのほうが手前ならそちらを優先する
            if (selection_.kind == SelectKind::None) {
                const Vector3 planeMin = { -0.5f, -0.5f, -0.02f };
                const Vector3 planeMax = { 0.5f, 0.5f, 0.02f };

                float bestPlaneT = (std::numeric_limits<float>::max)();
                int bestPlanes = -1;
                for (int i = 0; i < static_cast<int>(planes_.size()); ++i) {
                    const int drawCount = planes_[i].planes->GetDrawCount();
                    for (int k = 0; k < drawCount; ++k) {
                        float t = 0.0f;
                        if (Picking::RayIntersectsBox(ray, planes_[i].planes->GetInstanceTransform(k), planeMin, planeMax, t) && t < bestPlaneT) {
                            bestPlaneT = t;
                            bestPlanes = i;
                        }
                    }
                }

                float nearestObjectT = (std::numeric_limits<float>::max)();
                for (auto& slot : objects_) {
                    Object3d& object = slot.object->GetObject3d();
                    const Model* model = object.GetModel();
                    float t = 0.0f;
                    if (model && Picking::RayIntersectsBox(ray, object.GetTransform(), model->GetBoundsMin(), model->GetBoundsMax(), t)) {
                        nearestObjectT = (std::min)(nearestObjectT, t);
                    }
                }

                if (bestPlanes >= 0 && bestPlaneT < nearestObjectT) {
                    selection_ = { SelectKind::Planes, bestPlanes };

                    // ドラッグ平面：通常はカメラに平行、Shiftなら水平
                    const Matrix4x4 cameraWorld = Inverse(view);
                    const Vector3 cameraForward = { cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2] };
                    const Vector3 position = planes_[bestPlanes].planes->GetSettings().base.Translate;
                    dragPlaneNormal_ = vp.shift ? Vector3{ 0.0f, 1.0f, 0.0f } : cameraForward;
                    dragPlanePoint_ = position;

                    Vector3 hit{};
                    if (Picking::RayIntersectsPlane(ray, dragPlanePoint_, dragPlaneNormal_, hit)) {
                        dragObjectOffset_ = position.Subtract(hit);
                        dragMode_ = DragMode::Move;
                    }
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
        else if (selection_.kind == SelectKind::Planes && selection_.index < static_cast<int>(planes_.size())) {
            Vector3 hit{};
            if (Picking::RayIntersectsPlane(ray, dragPlanePoint_, dragPlaneNormal_, hit)) {
                planes_[selection_.index].planes->GetSettings().base.Translate = hit.Add(dragObjectOffset_);
            }
        }
        else if (selection_.kind == SelectKind::Emitter && selection_.index < static_cast<int>(emitters_.size())) {
            Vector3 hit{};
            if (Picking::RayIntersectsPlane(ray, dragPlanePoint_, dragPlaneNormal_, hit)) {
                emitters_[selection_.index].position = hit.Add(dragObjectOffset_);
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

    // 板ポリ大量配置：選択中のグループは、全ての板の枠を描く
    if (selection_.kind == SelectKind::Planes && selection_.index < static_cast<int>(planes_.size())) {
        const InstancedPlanes& planes = *planes_[selection_.index].planes;
        const Matrix4x4& viewMatrix = camera_->GetViewMatrix();
        const Matrix4x4& projectionMatrix = camera_->GetProjectionMatrix();
        const Vector3 local[4] = { { -0.5f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f }, { -0.5f, -0.5f, 0.0f } };
        for (int i = 0; i < planes.GetDrawCount(); ++i) {
            const Transform t = planes.GetInstanceTransform(i);
            const Matrix4x4 world = Matrix4x4::MakeAffineMatrix(t.Scale, t.Rotate, t.Translate);
            Vector2 corners[4];
            bool visible = true;
            for (int c = 0; c < 4 && visible; ++c) {
                const Vector3 p = {
                    local[c].x * world.m[0][0] + local[c].y * world.m[1][0] + local[c].z * world.m[2][0] + world.m[3][0],
                    local[c].x * world.m[0][1] + local[c].y * world.m[1][1] + local[c].z * world.m[2][1] + world.m[3][1],
                    local[c].x * world.m[0][2] + local[c].y * world.m[1][2] + local[c].z * world.m[2][2] + world.m[3][2]
                };
                visible = Picking::WorldToScreen(p, viewMatrix, projectionMatrix, corners[c]);
            }
            if (visible) {
                EditorLayout::DrawViewportOverlay(corners, 4, true);
            }
        }
    }

    // Position markers for every object / sprite / plane group so nothing gets lost
    if (showMarkers_) {
        const Matrix4x4& mView = camera_->GetViewMatrix();
        const Matrix4x4& mProjection = camera_->GetProjectionMatrix();
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            Vector2 s{};
            if (Picking::WorldToScreen(objects_[i].object->GetObject3d().GetTransform().Translate, mView, mProjection, s)) {
                const bool sel = selection_.kind == SelectKind::Object && selection_.index == i;
                const std::string name = objects_[i].video ? objects_[i].videoName : objFileList_[objects_[i].objIndex];
                EditorLayout::DrawViewportMarker(s, name.c_str(), sel ? IM_COL32(255, 200, 0, 255) : IM_COL32(80, 220, 255, 255), sel);
            }
        }
        for (int i = 0; i < static_cast<int>(sprites_.size()); ++i) {
            Vector2 corners[4];
            Picking::GetSpriteCorners(sprites_[i].sprite->GetTransform(), sprites_[i].size, corners);
            const Vector2 c = { (corners[0].x + corners[2].x) * 0.5f, (corners[0].y + corners[2].y) * 0.5f };
            const bool sel = selection_.kind == SelectKind::Sprite && selection_.index == i;
            const std::string name = sprites_[i].video ? sprites_[i].videoName : textureFileList_[sprites_[i].texIndex];
            EditorLayout::DrawViewportMarker(c, name.c_str(), sel ? IM_COL32(255, 200, 0, 255) : IM_COL32(120, 255, 120, 255), sel);
        }
        for (int i = 0; i < static_cast<int>(planes_.size()); ++i) {
            if (planes_[i].planes->GetDrawCount() <= 0) {
                continue;
            }
            Vector2 s{};
            if (Picking::WorldToScreen(planes_[i].planes->GetInstanceTransform(0).Translate, mView, mProjection, s)) {
                const bool sel = selection_.kind == SelectKind::Planes && selection_.index == i;
                const std::string name = std::format("{}", textureFileList_[planes_[i].texIndex]);
                EditorLayout::DrawViewportMarker(s, name.c_str(), sel ? IM_COL32(255, 200, 0, 255) : IM_COL32(255, 130, 255, 255), sel);
            }
        }
    }

    // パーティクルエミッターの目印（菱形）。選択中は大きく描き、発射方向の線も出す
    const Matrix4x4& view = camera_->GetViewMatrix();
    const Matrix4x4& projection = camera_->GetProjectionMatrix();
    for (int i = 0; i < static_cast<int>(emitters_.size()); ++i) {
        Vector2 c{};
        if (!Picking::WorldToScreen(emitters_[i].position, view, projection, c)) {
            continue;
        }
        const bool selected = selection_.kind == SelectKind::Emitter && selection_.index == i;
        const float r = selected ? 11.0f : 7.0f;
        const Vector2 diamond[4] = { { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y } };
        EditorLayout::DrawViewportOverlay(diamond, 4, true);

        if (selected) {
            const Vector3 dir = emitters_[i].direction.Normalize();
            Vector2 tip{};
            if (Picking::WorldToScreen(emitters_[i].position.Add(dir.Multiply(3.0f)), view, projection, tip)) {
                const Vector2 line[2] = { c, tip };
                EditorLayout::DrawViewportOverlay(line, 2, false);
            }
        }
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

    // 板ポリ大量配置（DrawInstancedで1グループ1回）
    if (!planes_.empty()) {
        ctx_->instancedPlaneCommon->PreDraw();
        for (auto& slot : planes_) {
            slot.planes->Draw();
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
#ifdef USE_IMGUI
// Play / pause / restart / loop / volume controls for a video
static void DrawVideoControls(VideoPlayer& video, const std::string& name)
{
    ImGui::Text("ファイル: %s", name.c_str());
    ImGui::Text("%d x %d  %.1f / %.1f 秒", static_cast<int>(video.GetWidth()), static_cast<int>(video.GetHeight()), video.GetTime(), video.GetDuration());
    if (ImGui::Button(video.IsPlaying() ? "一時停止" : "再生")) {
        if (video.IsPlaying()) {
            video.Pause();
        }
        else {
            video.Play();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("最初から")) {
        video.Restart();
    }
    bool loop = video.IsLoop();
    if (ImGui::Checkbox("ループ", &loop)) {
        video.SetLoop(loop);
    }
    float volume = video.GetVolume();
    if (ImGui::SliderFloat("音量", &volume, 0.0f, 1.0f)) {
        video.SetVolume(volume);
    }
}
#endif // USE_IMGUI

void GameScene::DrawImGui()
{
#ifdef USE_IMGUI
    if (!playing_) {
        DrawSelectionOutline(); // 実行中は選択枠・目印を出さない
    }

    const int objectCount = static_cast<int>(objects_.size());
    const int spriteCount = static_cast<int>(sprites_.size());

    // 「追加」メニューの中身（[+ Add]ボタンと、Hierarchyの空きスペースの右クリックで共通）
    auto drawAddMenu = [&]() {
        if (ImGui::BeginMenu("オブジェクトを追加")) {
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
        if (ImGui::BeginMenu("インスタンス平面を追加")) {
            for (int i = 0; i < static_cast<int>(textureFileList_.size()); ++i) {
                if (ImGui::MenuItem(textureFileList_[i].c_str())) {
                    pendingCommands_.push_back([this, i]() {
                        // 10枚が横一列に並んで、ちょうど画面の中央に来る位置に置く
                        const InstancedPlanes::Settings defaults{};
                        const float startX = -defaults.spacing.x * (defaults.count - 1) * 0.5f;
                        const int newIndex = AddPlanes(i, { startX, 0.0f, 0.0f });
                        selection_ = { SelectKind::Planes, newIndex };
                        });
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("パーティクルエミッターを追加")) {
            for (int i = 0; i < kParticlePresetCount; ++i) {
                if (ImGui::MenuItem(kParticlePresetNames[i])) {
                    pendingCommands_.push_back([this, i]() {
                        const int newIndex = AddEmitter(static_cast<ParticlePreset>(i), { 0.0f, 0.0f, 0.0f });
                        selection_ = { SelectKind::Emitter, newIndex };
                        });
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("スプライトを追加")) {
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
    EditorLayout::Begin("ヒエラルキー", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: ゲーム");

    // 実行 / 停止
    if (playing_) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "モード: 再生中");
        ImGui::SameLine();
        if (ImGui::Button("停止")) {
            StopPlay();
        }
    }
    else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "モード: 編集中");
        ImGui::SameLine();
        if (ImGui::Button("再生")) {
            StartPlay();
        }
    }

    // 追加・複製・削除は編集モードのときだけ
    if (playing_) {
        ImGui::TextDisabled("(追加/複製/削除は編集モードのみ)");
    }
    else {
        if (ImGui::Button("+ 追加")) {
            ImGui::OpenPopup("AddPopup");
        }
        if (ImGui::BeginPopup("AddPopup")) {
            drawAddMenu();
            ImGui::EndPopup();
        }
    }
    ImGui::Separator();

    // 3Dオブジェクト
    for (int i = 0; i < objectCount; ++i) {
        std::string label = objects_[i].video
            ? std::format("[動画3D {}] {}", i, objects_[i].videoName)
            : std::format("[オブジェクト {}] {}", i, objFileList_[objects_[i].objIndex]);
        const bool selected = selection_.kind == SelectKind::Object && selection_.index == i;
        ImGui::PushID(i);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selection_ = { SelectKind::Object, i };
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            selection_ = { SelectKind::Object, i };
        }
        if (ImGui::BeginPopupContextItem("ObjectContext")) {
            if (ImGui::MenuItem("複製", nullptr, false, !playing_)) {
                pendingCommands_.push_back([this, i]() { DuplicateObject(i); });
            }
            if (ImGui::MenuItem("削除", nullptr, false, !playing_)) {
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
        std::string label = sprites_[i].video
            ? std::format("[動画 {}] {}", i, sprites_[i].videoName)
            : std::format("[スプライト {}] {}", i, textureFileList_[sprites_[i].texIndex]);
        const bool selected = selection_.kind == SelectKind::Sprite && selection_.index == i;
        ImGui::PushID(1000 + i);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selection_ = { SelectKind::Sprite, i };
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            selection_ = { SelectKind::Sprite, i };
        }
        if (ImGui::BeginPopupContextItem("SpriteContext")) {
            if (ImGui::MenuItem("複製", nullptr, false, !playing_)) {
                pendingCommands_.push_back([this, i]() { DuplicateSprite(i); });
            }
            if (ImGui::MenuItem("削除", nullptr, false, !playing_)) {
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

    // 板ポリ大量配置
    for (int i = 0; i < static_cast<int>(planes_.size()); ++i) {
        std::string label = std::format("[平面 {}] {} x{}", i, textureFileList_[planes_[i].texIndex], planes_[i].planes->GetDrawCount());
        const bool selected = selection_.kind == SelectKind::Planes && selection_.index == i;
        ImGui::PushID(3000 + i);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selection_ = { SelectKind::Planes, i };
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            selection_ = { SelectKind::Planes, i };
        }
        if (ImGui::BeginPopupContextItem("PlanesContext")) {
            if (ImGui::MenuItem("複製", nullptr, false, !playing_)) {
                pendingCommands_.push_back([this, i]() { DuplicatePlanes(i); });
            }
            if (ImGui::MenuItem("削除", nullptr, false, !playing_)) {
                pendingCommands_.push_back([this, i]() {
                    if (i < static_cast<int>(planes_.size())) {
                        planes_.erase(planes_.begin() + i);
                        selection_ = {};
                    }
                    });
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // パーティクルエミッター
    for (int i = 0; i < static_cast<int>(emitters_.size()); ++i) {
        std::string label = std::format("[エミッター {}] {}", i, kParticlePresetNames[static_cast<int>(emitters_[i].preset)]);
        const bool selected = selection_.kind == SelectKind::Emitter && selection_.index == i;
        ImGui::PushID(2000 + i);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selection_ = { SelectKind::Emitter, i };
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            selection_ = { SelectKind::Emitter, i };
        }
        if (ImGui::BeginPopupContextItem("EmitterContext")) {
            if (ImGui::MenuItem("複製", nullptr, false, !playing_)) {
                pendingCommands_.push_back([this, i]() { DuplicateEmitter(i); });
            }
            if (ImGui::MenuItem("削除", nullptr, false, !playing_)) {
                pendingCommands_.push_back([this, i]() {
                    if (i < static_cast<int>(emitters_.size())) {
                        emitters_.erase(emitters_.begin() + i);
                        selection_ = {};
                    }
                    });
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // ライト（削除不可）
    if (ImGui::Selectable("ディレクショナルライト", selection_.kind == SelectKind::Light)) {
        selection_ = { SelectKind::Light, 0 };
    }

    ImGui::Separator();
    ImGui::Text("パーティーモード: %s (コナミコマンド・再生中のみ)", konamiPartyMode_ ? "オン" : "オフ");
    ImGui::Text("パーティクル数: %zu / %u", ctx_->particleSystem->GetCount(), ParticleSystem::kMaxParticles);
    ImGui::Text("再生: エミッターが動く / 停止: シーンが元に戻る");
    ImGui::Text("ビューポート: クリックで選択、ドラッグで移動");
    ImGui::Text("エミッターはひし形のマーク(クリックで選択)");
    ImGui::Text("インスタンス平面: 1グループ = N枚の平面");
    ImGui::Text("3Dを画面端までドラッグ: カメラが追従");
    ImGui::Checkbox("位置マーカーを表示", &showMarkers_);
    ImGui::Separator();
    ImGui::Text("辺をドラッグ: その方向に伸縮");
    ImGui::Text("角をドラッグ: 拡大縮小 (Shift: 自由変形)");
    ImGui::Text("(Shift+3Dをドラッグ: 地面の上を移動)");
    ImGui::Text("ビューポートを右ボタン長押し: WASDで移動");
    ImGui::Text("ホイールクリック+ドラッグ: カメラの向きを変える");
    ImGui::Text("  マウス=視点 WASD=移動 Space/Shift=上昇/下降");
    ImGui::Text("リストを右クリック: 追加 / 複製 / 削除");
    ImGui::Text("BACKSPACE: タイトルに戻る");

    // 項目の上以外（空きスペース）を右クリックしたときのメニュー
    if (!playing_ && ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        drawAddMenu();
        ImGui::EndPopup();
    }
    ImGui::End();

    //--------------------------------------------------
    // Inspector（右）：選択中の項目のプロパティ
    //--------------------------------------------------
    EditorLayout::Begin("インスペクター", EditorLayout::Panel::Inspector);

    // 選択中の項目の削除ボタン（編集モードのみ）。Deleteキー / Hierarchyの右クリックでも削除できる
    if (!playing_ && (selection_.kind == SelectKind::Object || selection_.kind == SelectKind::Sprite || selection_.kind == SelectKind::Planes || selection_.kind == SelectKind::Emitter)) {
        if (ImGui::Button("削除 (Delキー)")) {
            pendingCommands_.push_back([this]() { DeleteSelection(); });
        }
    }

    if (selection_.kind == SelectKind::Object && selection_.index < objectCount) {
        ObjectSlot& slot = objects_[selection_.index];
        Object3d& object = slot.object->GetObject3d();

        ImGui::Text(slot.video ? "動画 (3D) [%d]" : "オブジェクト [%d]", selection_.index);
        ImGui::Separator();
        if (slot.video) {
            DrawVideoControls(*slot.video, slot.videoName);
        }
        else {
            ImGui::Combo("OBJファイル", &slot.objIndex, objFileListCStr_.data(), static_cast<int>(objFileListCStr_.size()));
        }
        ImGui::DragFloat3("スケール", &object.GetTransform().Scale.x, 0.01f);
        ImGui::DragFloat3("回転", &object.GetTransform().Rotate.x, 0.01f);
        ImGui::DragFloat3("移動", &object.GetTransform().Translate.x, 0.01f);
        ImGui::ColorEdit4("色", &object.GetMaterial()->color.x);
        const char* lightingModes[] = { "なし", "ランバート", "ハーフランバート" };
        ImGui::Combo("ライティング", &object.GetMaterial()->lightingMode, lightingModes, IM_ARRAYSIZE(lightingModes));
        int blendMode = static_cast<int>(object.GetBlendMode());
        if (ImGui::Combo("ブレンドモード", &blendMode, kBlendModeNames, kBlendModeCount)) {
            object.SetBlendMode(static_cast<BlendMode>(blendMode));
        }
        ImGui::SliderFloat("アルファカットオフ", &object.GetMaterial()->alphaCutoff, 0.0f, 1.0f);
        bool doubleSided = object.IsDoubleSided();
        if (ImGui::Checkbox("両面描画", &doubleSided)) {
            object.SetDoubleSided(doubleSided);
        }
        if (ImGui::CollapsingHeader("UV変換")) {
            ImGui::DragFloat2("UV移動", &object.GetUvTransform().Translate.x, 0.01f, -10.0f, 10.0f);
            ImGui::DragFloat2("UVスケール", &object.GetUvTransform().Scale.x, 0.01f, -10.0f, 10.0f);
            ImGui::SliderAngle("UV回転", &object.GetUvTransform().Rotate.z);
        }
    }
    else if (selection_.kind == SelectKind::Sprite && selection_.index < spriteCount) {
        SpriteSlot& slot = sprites_[selection_.index];
        Sprite& sprite = *slot.sprite;

        ImGui::Text(slot.video ? "動画 [%d]" : "スプライト [%d]", selection_.index);
        ImGui::Separator();
        if (slot.video) {
            DrawVideoControls(*slot.video, slot.videoName);
        }
        else {
            ImGui::Combo("テクスチャ", &slot.texIndex, textureFileListCStr_.data(), static_cast<int>(textureFileListCStr_.size()));
        }
        ImGui::DragFloat3("スケール", &sprite.GetTransform().Scale.x, 0.01f);
        ImGui::DragFloat3("回転", &sprite.GetTransform().Rotate.x, 0.01f);
        ImGui::DragFloat3("移動", &sprite.GetTransform().Translate.x, 1.0f);
        ImGui::ColorEdit4("色", &sprite.GetMaterial()->color.x);
        int blendMode = static_cast<int>(sprite.GetBlendMode());
        if (ImGui::Combo("ブレンドモード", &blendMode, kBlendModeNames, kBlendModeCount)) {
            sprite.SetBlendMode(static_cast<BlendMode>(blendMode));
        }
        ImGui::SliderFloat("アルファカットオフ", &sprite.GetMaterial()->alphaCutoff, 0.0f, 1.0f);
        if (ImGui::CollapsingHeader("UV変換")) {
            ImGui::DragFloat2("UV移動", &sprite.GetUvTransform().Translate.x, 0.01f, -10.0f, 10.0f);
            ImGui::DragFloat2("UVスケール", &sprite.GetUvTransform().Scale.x, 0.01f, -10.0f, 10.0f);
            ImGui::SliderAngle("UV回転", &sprite.GetUvTransform().Rotate.z);
        }
    }
    else if (selection_.kind == SelectKind::Planes && selection_.index < static_cast<int>(planes_.size())) {
        PlanesSlot& slot = planes_[selection_.index];
        InstancedPlanes::Settings& settings = slot.planes->GetSettings();

        ImGui::Text("インスタンス平面 [%d]", selection_.index);
        ImGui::Text("%d枚の平面 (DrawInstanced 1回)", slot.planes->GetDrawCount());
        ImGui::Separator();
        ImGui::Combo("テクスチャ", &slot.texIndex, textureFileListCStr_.data(), static_cast<int>(textureFileListCStr_.size()));
        ImGui::SliderInt("数", &settings.count, 1, static_cast<int>(InstancedPlanes::kMaxInstances));
        ImGui::DragFloat3("サイズ", &settings.base.Scale.x, 0.02f);
        ImGui::DragFloat3("回転", &settings.base.Rotate.x, 0.01f);
        ImGui::DragFloat3("位置", &settings.base.Translate.x, 0.05f);
        ImGui::DragFloat3("間隔", &settings.spacing.x, 0.02f);
        ImGui::DragFloat3("回転ステップ", &settings.rotateStep.x, 0.005f);
        ImGui::ColorEdit4("色", &settings.color.x);
    }
    else if (selection_.kind == SelectKind::Emitter && selection_.index < static_cast<int>(emitters_.size())) {
        ParticleEmitter& e = emitters_[selection_.index];

        // Texture picker: "None" or one of textureFileList_ (the chosen path is stored in 'path')
        auto drawTextureCombo = [&](const char* label, std::string& path) {
            const char* kNoTexture = "(なし: 形で描く)";
            int currentTexture = 0; // 0 = None
            for (int i = 0; i < static_cast<int>(textureFileList_.size()); ++i) {
                if (textureFileList_[i] == path) {
                    currentTexture = i + 1;
                    break;
                }
            }
            const char* preview = (currentTexture == 0) ? kNoTexture : textureFileList_[currentTexture - 1].c_str();
            if (ImGui::BeginCombo(label, preview)) {
                if (ImGui::Selectable(kNoTexture, currentTexture == 0)) {
                    path.clear();
                }
                for (int i = 0; i < static_cast<int>(textureFileList_.size()); ++i) {
                    if (ImGui::Selectable(textureFileList_[i].c_str(), currentTexture == i + 1)) {
                        path = textureFileList_[i];
                    }
                }
                ImGui::EndCombo();
            }
        };

        ImGui::Text("パーティクルエミッター [%d]", selection_.index);
        ImGui::Separator();

        // プリセットを選ぶと、下の値が全てそのプリセットの値に置き換わる
        int preset = static_cast<int>(e.preset);
        if (ImGui::Combo("プリセット", &preset, kParticlePresetNames, kParticlePresetCount)) {
            e.ApplyPreset(static_cast<ParticlePreset>(preset));
        }
        ImGui::Checkbox("有効", &e.enabled);
        ImGui::SameLine();
        if (ImGui::Button("再生(今すぐバースト)")) {
            e.playRequested = true;
        }
        ImGui::DragFloat3("位置", &e.position.x, 0.05f);

        if (ImGui::CollapsingHeader("発生", ImGuiTreeNodeFlags_DefaultOpen)) {
            int mode = static_cast<int>(e.mode);
            if (ImGui::Combo("モード", &mode, kEmitModeNames, 2)) {
                e.mode = static_cast<EmitMode>(mode);
            }
            if (e.mode == EmitMode::Continuous) {
                ImGui::DragFloat("発生レート (個/秒)", &e.rate, 1.0f, 0.0f, 1000.0f);
            }
            else {
                ImGui::DragInt("バースト数", &e.burstCount, 1.0f, 1, 1000);
                ImGui::DragFloat("間隔 (0=手動)", &e.burstInterval, 0.05f, 0.0f, 30.0f);
            }
            ImGui::DragFloat("半径", &e.radius, 0.05f, 0.0f, 50.0f);
            ImGui::Checkbox("平らな円盤 (XZ)", &e.emitFlat);
            ImGui::Checkbox("花火 (最後に弾ける)", &e.fireworks);
            if (e.fireworks) {
                ImGui::DragInt("火花の数", &e.sparkCount, 1.0f, 1, 300);
                drawTextureCombo("火花のテクスチャ", e.sparkTexturePath);
            }
        }

        if (ImGui::CollapsingHeader("動き", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::DragFloat3("方向", &e.direction.x, 0.01f)) {
                if (e.direction.Length() > 0.0f) {
                    e.direction = e.direction.Normalize();
                }
                else {
                    e.direction = { 0.0f, 1.0f, 0.0f };
                }
            }
            ImGui::SliderFloat("拡散角 (度)", &e.spreadDeg, 0.0f, 180.0f);
            ImGui::DragFloatRange2("速度", &e.speedMin, &e.speedMax, 0.05f, 0.0f, 50.0f);
            ImGui::DragFloatRange2("寿命 (秒)", &e.lifeMin, &e.lifeMax, 0.05f, 0.05f, 30.0f);
            ImGui::DragFloat("重力", &e.gravity, 0.05f, -20.0f, 20.0f);
            ImGui::DragFloat("空気抵抗", &e.drag, 0.02f, 0.0f, 10.0f);
            ImGui::DragFloat("ゆれ", &e.wobble, 0.02f, 0.0f, 10.0f);
        }

        if (ImGui::CollapsingHeader("見た目", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("開始サイズ", &e.sizeStart, 0.02f, 0.0f, 20.0f);
            ImGui::DragFloat("終了サイズ", &e.sizeEnd, 0.02f, 0.0f, 20.0f);
            ImGui::SliderFloat("サイズのばらつき", &e.sizeRandom, 0.0f, 1.0f);
            ImGui::Checkbox("虹色 (ランダムな色相)", &e.rainbow);
            ImGui::ColorEdit4("開始色", &e.colorStart.x);
            ImGui::ColorEdit4("終了色", &e.colorEnd.x);
            int shape = static_cast<int>(e.shape);
            if (ImGui::Combo("形", &shape, kParticleShapeNames, 3)) {
                e.shape = static_cast<ParticleShape>(shape);
            }

            // テクスチャ（なしなら上の「形」で描く）。エミッターごとに別の画像を選べる
            drawTextureCombo("パーティクルのテクスチャ", e.texturePath);
            int blend = e.additive ? 1 : 0;
            const char* blendNames[] = { "通常", "加算" };
            if (ImGui::Combo("ブレンド", &blend, blendNames, 2)) {
                e.additive = (blend == 1);
            }
        }
    }
    else if (selection_.kind == SelectKind::Light) {
        ImGui::Text("ディレクショナルライト");
        ImGui::Separator();
        ImGui::ColorEdit4("色", &directionalLight_->color.x);
        if (ImGui::DragFloat3("方向", &directionalLight_->direction.x, 0.01f)) {
            directionalLight_->direction = Normalize(directionalLight_->direction);
        }
        ImGui::DragFloat("強さ", &directionalLight_->intensity, 0.01f);
    }
    else {
        ImGui::Text("何も選択されていません");
    }

    ImGui::End();
#endif // USE_IMGUI
}
