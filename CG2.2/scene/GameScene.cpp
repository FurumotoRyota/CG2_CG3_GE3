#include <cstdio>
#include <cassert>
#include "math/Collision.h"
#include <climits>
#include <cstdlib>
#include <cstring>
#include "GameScene.h"
#include "TitleScene.h"
#include "scene/SceneManager.h"
#include "base/DirectXCommon.h"

#include "base/Logger.h"
#include "base/WinApp.h"
#include "base/StringUtility.h"
#include "2d/SpriteCommon.h"
#include "3d/ModelLoader.h"
#include "3d/Object3dCommon.h"
#include "3d/EnvironmentMap.h"
#include "3d/ShadowMap.h"
#include "3d/InstancedPlanes.h"
#include "camera/DebugCamera.h"
#include "editor/EditorLayout.h"
#include "editor/ViewportPicking.h"
#include "3d/Model.h"
#include "core/FrameStats.h"
#include "3d/ModelManager.h"
#include "base/TextureManager.h"
#include "2d/ColorBatch.h"
#include "../game2d/CubeMesh.h"
#include "input/Input.h"
#include "math/Color.h"
#include "particle/ParticleSystem.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <climits>
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
        // string() は日本語など ANSI に変換できない名前で例外を投げるので wstring で比較する
        std::wstring ext = path.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        for (const char* e : extensions) {
            if (ext == std::filesystem::path(e).wstring()) {
                return true;
            }
        }
        return false;
    }
}

namespace
{
    constexpr const char* kCubeName = "pf_cube";
    constexpr float kPfFovY = 0.6f; // 2D軸アクション再生中のカメラの縦の視野角[rad]
    // HUDの文字（resources/startup_ds_glyphs.png を使う。1文字 104x150、並びは下の文字順）
    constexpr float kHudCellW = 104.0f;
    constexpr float kHudCellH = 150.0f;
    constexpr const char* kHudCharset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.";
    constexpr int kHudMaxGlyphs = 96;
    // マップのチップの種類（3Dマップ）。solid が false のものは、見た目だけで当たり判定はない
    struct ChipDef
    {
        const char* label;
        float height;
        bool solid;
        bool ownColor;
        Vector4 color;
    };
    const ChipDef kChips[] = {
        { "壁（標準）", 1.5f, true, false, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { "低い壁", 0.6f, true, false, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { "高い壁", 3.5f, true, false, { 1.0f, 1.0f, 1.0f, 1.0f } },
        { "床タイル（通れる）", 0.06f, false, true, { 0.55f, 0.57f, 0.62f, 1.0f } },
        { "草（通れる）", 0.06f, false, true, { 0.35f, 0.7f, 0.3f, 1.0f } },
        { "水たまり（通れる）", 0.06f, false, true, { 0.25f, 0.5f, 0.85f, 1.0f } },
        { "危険な床（通れる）", 0.06f, false, true, { 0.9f, 0.3f, 0.15f, 1.0f } },
    };
    constexpr int kChipCount = static_cast<int>(sizeof(kChips) / sizeof(kChips[0]));
    const char* kRoleNames[] = { "なし", "プレイヤー開始位置", "ブロック", "敵の配置", "ゴール" };
    constexpr int kMapWidth = 48;             // マップの横のマス数
    constexpr int kMapHeight = 48;            // マップの縦（奥行き）のマス数
    constexpr float kEditPlaneZ = -0.5f;      // 編集する平面（ブロックの前面）
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

    gameCamera_.SetPerspective(kPfFovY, static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight, 0.1f, 300.0f);
    LoadScene(scenePath_); // 保存したシーンがあれば読み込む
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
// .gltf が参照している .bin やテクスチャ（"uri"）を resources へコピーする（data: の埋め込みは無視）
static void CopyGltfCompanions(const std::filesystem::path& gltfPath)
{
    namespace fs = std::filesystem;
    std::ifstream file(gltfPath);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    size_t pos = 0;
    while ((pos = text.find("\"uri\"", pos)) != std::string::npos) {
        pos += 5;
        const size_t colon = text.find(':', pos);
        const size_t begin = text.find('"', colon);
        if (colon == std::string::npos || begin == std::string::npos) {
            break;
        }
        const size_t end = text.find('"', begin + 1);
        if (end == std::string::npos) {
            break;
        }
        std::string uri = text.substr(begin + 1, end - begin - 1);
        pos = end + 1;
        if (uri.rfind("data:", 0) == 0 || uri.empty()) {
            continue;
        }
        // %20 などのURLエンコードは簡易的に空白だけ戻す
        for (size_t p; (p = uri.find("%20")) != std::string::npos;) {
            uri.replace(p, 3, " ");
        }
        CopyToResources(gltfPath.parent_path() / fs::path(StringUtility::ConvertString(uri)).filename());
    }
}

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
        const fs::path mtlSrc = objPath.parent_path() / fs::path(StringUtility::ConvertString(value)).filename();
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
                CopyToResources(mtlSrc.parent_path() / fs::path(StringUtility::ConvertString(texName)).filename());
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

        if (HasExtension(src, { ".obj", ".gltf", ".glb", ".fbx" })) {
            if (HasExtension(src, { ".obj" })) {
                CopyObjCompanions(src);
            }
            else if (HasExtension(src, { ".gltf" })) {
                CopyGltfCompanions(src);
            }
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
        if (HasExtension(path, { ".obj", ".gltf", ".glb", ".fbx" })) {
            foundObjs.push_back(StringUtility::ConvertString(path.filename().wstring()));
        }
        else if (HasExtension(path, { ".png", ".jpg", ".jpeg", ".bmp" })) {
            foundTextures.push_back("resources/" + StringUtility::ConvertString(path.filename().wstring()));
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

    // 2D軸アクションのブロック用の立方体（パレット画像の白のマスだけを使う単色の立方体。色は「色」で変える）
    {
        ColorBatch::ColorToUv(ColorRGB{ 1.0f, 1.0f, 1.0f }, cubeWhiteUv_.x, cubeWhiteUv_.y);
        ctx_->modelManager->Register(kCubeName, CubeMesh::MakeCubeFullUv("resources/palette.png"));
        if (std::find(objFileList_.begin(), objFileList_.end(), kCubeName) == objFileList_.end()) {
            objFileList_.push_back(kCubeName);
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
    if (objFileList_[objIndex] == kCubeName) {
        ApplyFlatUv(slot.object->GetObject3d()); // 立方体は最初は単色
    }
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
    const int specularMode = src.GetMaterial()->specularMode;
    const float shininess = src.GetMaterial()->shininess;
    const float roughness = src.GetMaterial()->roughness;
    const bool castShadow = src.GetCastShadow();
    const float receiveShadow = src.GetMaterial()->receiveShadow;
    const float metallic = src.GetMaterial()->metallic;
    const BlendMode blendMode = src.GetBlendMode();
    const float alphaCutoff = src.GetMaterial()->alphaCutoff;
    const bool doubleSided = src.IsDoubleSided();
    // パーティーモード中は演出で色が変わっているので、退避しておいた元の色を引き継ぐ
    const Vector4 color = konamiPartyMode_ ? objects_[index].konamiOriginalColor : src.GetMaterial()->color;

    const int srcRole = objects_[index].role;
    const bool srcShield = objects_[index].shield;
    const int srcTex = objects_[index].texOverride;
    const int newIndex = AddObject(srcModel, transform.Translate);
    objects_[newIndex].role = srcRole;
    objects_[newIndex].shield = srcShield;
    objects_[newIndex].texOverride = srcTex;
    {
        const ObjectSlot& from = objects_[index]; // push_backで参照が変わらないよう、いったん取り直す
        ObjectSlot& to = objects_[newIndex];
        std::memcpy(to.name, from.name, sizeof(to.name));
    }
    Object3d& dst = objects_[newIndex].object->GetObject3d();
    dst.GetTransform() = transform;
    dst.GetTransform().Translate.x += (srcRole == 2) ? 1.0f : 0.5f; // 重ならないように少しずらす
    dst.GetUvTransform() = uvTransform;
    dst.GetMaterial()->color = color;
    dst.GetMaterial()->lightingMode = lightingMode;
    dst.GetMaterial()->specularMode = specularMode;
    dst.GetMaterial()->shininess = shininess;
    dst.GetMaterial()->roughness = roughness;
    dst.SetCastShadow(castShadow);
    dst.GetMaterial()->receiveShadow = receiveShadow;
    dst.GetMaterial()->metallic = metallic;
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

int GameScene::AddLight(int type, const Vector3& position)
{
    LightSlot light;
    light.type = type;
    light.position = position;
    lights_.push_back(light);
    return static_cast<int>(lights_.size()) - 1;
}

void GameScene::DuplicateLight(int index)
{
    if (index < 0 || index >= static_cast<int>(lights_.size())) {
        return;
    }
    LightSlot copy = lights_[index];
    copy.position.x += 1.0f; // 重ならないように少しずらす
    lights_.push_back(copy);
    selection_ = { SelectKind::SceneLight, static_cast<int>(lights_.size()) - 1 };
}

// 影用：ディレクショナルライトの向きから、光から見たビュー射影行列を作って共通部へ渡す
void GameScene::UpdateShadow()
{
    if (!directionalLight_) {
        return;
    }
    ctx_->object3dCommon->SetLightViewProjection(
        ShadowMap::ComputeLightViewProjection(directionalLight_->direction, shadowCenter_, shadowRange_));
}

// 影の深度テクスチャを描く。影を落とす設定のオブジェクトだけ、光から見た位置で深度を書く
void GameScene::RenderShadowPass()
{
    if (!directionalLight_ || directionalLight_->shadowEnabled == 0) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = ctx_->dxCommon->GetCommandList();
    ShadowMap::Instance().Begin(commandList);
    for (size_t i = 0; i < objects_.size(); ++i) {
        if (i < visFlags_.size() && (visFlags_[i] & 2) == 0) {
            continue; // 影の範囲の外のブロックは描かない
        }
        objects_[i].object->GetObject3d().DrawShadow(commandList);
    }
    ShadowMap::Instance().End(commandList);
}

// 配置したライトを定数バッファへ書き込む（上限 kMaxLights 個）
void GameScene::UpdateLights()
{
    if (!directionalLight_) {
        return;
    }
    const float kDegToRad = 3.14159265f / 180.0f;
    const int count = (std::min)(static_cast<int>(lights_.size()), kMaxLights);
    directionalLight_->lightCount = count;
    for (int i = 0; i < count; ++i) {
        const LightSlot& s = lights_[i];
        LightData& d = directionalLight_->lights[i];
        d.color = s.color;
        d.position = s.position;
        d.intensity = s.intensity;
        d.direction = s.direction;
        d.range = (std::max)(s.range, 0.01f);
        d.decay = s.decay;
        d.cosAngle = std::cos(s.outerDeg * kDegToRad);
        d.cosFalloffStart = std::cos(s.innerDeg * kDegToRad);
        d.type = s.type;
    }
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
    else if (selection_.kind == SelectKind::SceneLight && index < static_cast<int>(lights_.size())) {
        lights_.erase(lights_.begin() + index);
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
    snapshot_.lights = lights_;

    ctx_->particleSystem->Clear(); // 編集中のプレビューの粒は消す
    konamiCommand_ = KonamiCommand{};
    konamiPartyMode_ = false;
    konamiTime_ = 0.0f;
    dragMode_ = DragMode::None;
    playing_ = true;

    mapEdit_ = false;
    mapPainting_ = false;
    mapErasing_ = false;
    BuildWallGrid(); // 壁の当たり判定表（ゲームのコードから WallAt / RaycastWalls で使える）

    timeScale_ = 1.0f;
    camFx_.Reset();
}

// 実行停止：保存しておいた状態に戻す
void GameScene::StopPlay()
{
    if (!playing_) {
        return;
    }
    timeScale_ = 1.0f;
    camFx_.Reset();
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
    if (lights_.size() == snapshot_.lights.size()) {
        lights_ = snapshot_.lights;
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
    FrameStats::ScopedMs updateTimer(FrameStats::Cur().updateMs);
    Input& input = *ctx_->input;

    // タイトルへ戻る（誤操作防止のためBACKSPACEは無効。ゲームパッドSTARTのみ）
    if (input.TriggerGamepadButton(9)) {
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
        // テクスチャの上書き（ブロックは「ブロックの見た目」で別に管理する）
        if (!slot.video && slot.role != 2 && slot.texOverride != slot.loadedTexOverride) {
            Object3d& object = slot.object->GetObject3d();
            if (slot.texOverride > 0 && slot.texOverride - 1 < static_cast<int>(textureFileList_.size())) {
                const TextureManager::TextureHandle handle = ctx_->textureManager->Load(textureFileList_[slot.texOverride - 1]);
                object.SetExternalTexture(ctx_->textureManager->GetSrvHandleGPU(handle));
            }
            else {
                object.ClearExternalTexture();
            }
            slot.loadedTexOverride = slot.texOverride;
        }
    }
    // モデルを差し替えたあとの大きさ合わせ
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (objects_[i].refit) {
            FitObjectToSize(i, 0.9f);
            objects_[i].refit = false;
        }
    }
    if (blockStyleDirty_) {
        ApplyBlockStyleAll();
        blockStyleDirty_ = false;
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

    // 入力の窓口：マウス（ゲーム画面のピクセル座標）を渡してから更新する
    {
        Vector2 mouse{ 0.0f, 0.0f };
        bool inside = false;
#ifdef USE_IMGUI
        const EditorLayout::ViewportInput& mouseVp = EditorLayout::GetViewportInput();
        mouse = mouseVp.mouse;
        inside = mouseVp.valid;
#else
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(ctx_->winApp->GetHwnd(), &cursor);
        mouse = { static_cast<float>(cursor.x), static_cast<float>(cursor.y) };
        inside = true;
#endif
        inside = inside && mouse.x >= 0.0f && mouse.x < static_cast<float>(WinApp::kGameWidth) &&
            mouse.y >= 0.0f && mouse.y < static_cast<float>(WinApp::kGameHeight);
        const bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        gameInput_.SetMouse(mouse, inside, leftDown);
        gameInput_.SetAimOrigin({ WinApp::kGameWidth * 0.5f, WinApp::kGameHeight * 0.5f }); // 狙いの基準点（標準は画面の中心）
        gameInput_.Update(input);
    }

    if (playing_) {
        dragMode_ = DragMode::None; // 実行中は選択・ドラッグ移動をしない
        UpdateKonami();             // コナミコマンドは実行中だけ有効
        camFx_.Update(1.0f / 60.0f); // 画面の揺れ・ヒットストップ（実時間で進める）
    }
    else {
        if (mapEdit_) {
            UpdateMapEdit(); // マップ編集中は、クリックでの選択・移動は使わない
        }
        else {
            mapEditPrev_ = false;
            mapPainting_ = false;
            mapErasing_ = false;
            UpdateViewportInteraction();
        }
    }
    UpdateLights();
    UpdateShadow();      // ライトのビュー射影を決めてから
    UpdateObjects();     // 各オブジェクトのLightWVPを更新し
    RenderShadowPass();  // 影の深度テクスチャを描く

    // 板ポリ大量配置：全ての板のWVPを書き込む
    for (auto& slot : planes_) {
        slot.planes->Update(ActiveCamera().GetViewMatrix(), ActiveCamera().GetProjectionMatrix());
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
    ctx_->particleSystem->Update(ActiveCamera().GetViewMatrix(), ActiveCamera().GetProjectionMatrix(), konamiPartyMode_);

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
    if (dragMode_ == DragMode::Move && (selection_.kind == SelectKind::Object || selection_.kind == SelectKind::Planes || selection_.kind == SelectKind::Emitter || selection_.kind == SelectKind::SceneLight) && vp.down) {
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

    const Matrix4x4& view = ActiveCamera().GetViewMatrix();
    const Matrix4x4& projection = ActiveCamera().GetProjectionMatrix();
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

            // 配置したライトも、目印の近くをクリックしたら選ぶ
            if (selection_.kind == SelectKind::None) {
                constexpr float kPickRadius = 16.0f;
                float bestDistance2 = kPickRadius * kPickRadius;
                int bestLight = -1;
                for (int i = 0; i < static_cast<int>(lights_.size()); ++i) {
                    Vector2 screen{};
                    if (!Picking::WorldToScreen(lights_[i].position, view, projection, screen)) {
                        continue;
                    }
                    const float dx = vp.mouse.x - screen.x;
                    const float dy = vp.mouse.y - screen.y;
                    const float distance2 = dx * dx + dy * dy;
                    if (distance2 <= bestDistance2) {
                        bestDistance2 = distance2;
                        bestLight = i;
                    }
                }

                if (bestLight >= 0) {
                    selection_ = { SelectKind::SceneLight, bestLight };

                    const Matrix4x4 cameraWorld = Inverse(view);
                    const Vector3 cameraForward = { cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2] };
                    const Vector3 position = lights_[bestLight].position;
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
        else if (selection_.kind == SelectKind::SceneLight && selection_.index < static_cast<int>(lights_.size())) {
            Vector3 hit{};
            if (Picking::RayIntersectsPlane(ray, dragPlanePoint_, dragPlaneNormal_, hit)) {
                lights_[selection_.index].position = hit.Add(dragObjectOffset_);
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
            if (!Picking::WorldToScreen(p, ActiveCamera().GetViewMatrix(), ActiveCamera().GetProjectionMatrix(), screen)) {
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
        const Matrix4x4& viewMatrix = ActiveCamera().GetViewMatrix();
        const Matrix4x4& projectionMatrix = ActiveCamera().GetProjectionMatrix();
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
        const Matrix4x4& mView = ActiveCamera().GetViewMatrix();
        const Matrix4x4& mProjection = ActiveCamera().GetProjectionMatrix();
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
    const Matrix4x4& view = ActiveCamera().GetViewMatrix();
    const Matrix4x4& projection = ActiveCamera().GetProjectionMatrix();
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

    // 配置したライトの目印。ポイント=四角、スポット=三角。選択中は大きく描き、スポットは向きの線も出す
    for (int i = 0; i < static_cast<int>(lights_.size()); ++i) {
        Vector2 c{};
        if (!Picking::WorldToScreen(lights_[i].position, view, projection, c)) {
            continue;
        }
        const bool selected = selection_.kind == SelectKind::SceneLight && selection_.index == i;
        const float r = selected ? 11.0f : 7.0f;
        if (lights_[i].type == 0) {
            const Vector2 square[4] = { { c.x - r, c.y - r }, { c.x + r, c.y - r }, { c.x + r, c.y + r }, { c.x - r, c.y + r } };
            EditorLayout::DrawViewportOverlay(square, 4, true);
        }
        else {
            const Vector2 triangle[3] = { { c.x, c.y - r }, { c.x + r, c.y + r }, { c.x - r, c.y + r } };
            EditorLayout::DrawViewportOverlay(triangle, 3, true);
            Vector2 tip{};
            const Vector3 dir = lights_[i].direction.Normalize();
            if (Picking::WorldToScreen(lights_[i].position.Add(dir.Multiply(2.0f)), view, projection, tip)) {
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
    const Matrix4x4& viewMatrix = ActiveCamera().GetViewMatrix();
    const Matrix4x4& projectionMatrix = ActiveCamera().GetProjectionMatrix();

    if (konamiPartyMode_) {
        konamiTime_ += 0.05f;
    }

    // ブロック（壁・床）は数が多いので、画面にも影の範囲にも入らないものは更新・描画を省く
    const Matrix4x4 viewProj = Multiply(viewMatrix, projectionMatrix);
    visFlags_.assign(objects_.size(), 3);
    for (size_t i = 0; i < objects_.size(); ++i) {
        const ObjectSlot& slot = objects_[i];
        if (slot.video || slot.role != 2) {
            continue;
        }
        const Vector3& p = slot.object->GetObject3d().GetTransform().Translate;
        const float cw = p.x * viewProj.m[0][3] + p.y * viewProj.m[1][3] + p.z * viewProj.m[2][3] + viewProj.m[3][3];
        const float cx = p.x * viewProj.m[0][0] + p.y * viewProj.m[1][0] + p.z * viewProj.m[2][0] + viewProj.m[3][0];
        const float cy = p.x * viewProj.m[0][1] + p.y * viewProj.m[1][1] + p.z * viewProj.m[2][1] + viewProj.m[3][1];
        unsigned char flags = 0;
        if (cw > 0.0f && std::fabs(cx) <= cw * 1.3f && std::fabs(cy) <= cw * 1.3f) {
            flags |= 1;
        }
        const Vector2 sp = Planar(p);
        const Vector2 sc = Planar(shadowCenter_);
        const float range = shadowRange_ * 1.5f + 2.0f;
        if (std::fabs(sp.x - sc.x) <= range && std::fabs(sp.y - sc.y) <= range) {
            flags |= 2;
        }
        visFlags_[i] = flags;
    }

    for (size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex) {
        ObjectSlot& slot = objects_[objectIndex];
        slot.object->Update();
        if (visFlags_[objectIndex] == 0) {
            continue; // 見えない・影にも関係しないブロックは、行列の更新を省く
        }
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
// 役割（マップ編集で置いたもの）の検索
//==================================================
int GameScene::FindPlayerObject() const
{
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (!objects_[i].video && objects_[i].role == 1) {
            return i;
        }
    }
    return -1;
}

int GameScene::MapW() const
{
    return kMapWidth;
}

int GameScene::MapH() const
{
    return kMapHeight;
}

// 再生開始時に、壁（役割「ブロック」）のあるマスを調べておく
void GameScene::BuildWallGrid()
{
    wallGrid_.clear();
    if (!Ground3D()) {
        return;
    }
    wallGrid_.assign(static_cast<size_t>(MapW()) * MapH(), 0);
    for (const ObjectSlot& slot : objects_) {
        if (slot.video || slot.role != 2) {
            continue;
        }
        if (slot.chip > 0 && slot.chip < kChipCount && !kChips[slot.chip].solid) {
            continue; // 通れるチップ（床・水など）は壁ではない
        }
        const Vector2 p = Planar(slot.object->GetObject3d().GetTransform().Translate);
        const int cx = static_cast<int>(std::lround(p.x));
        const int cz = static_cast<int>(std::lround(p.y));
        if (cx >= 0 && cx < MapW() && cz >= 0 && cz < MapH()) {
            wallGrid_[static_cast<size_t>(cz) * MapW() + cx] = 1;
        }
    }
}

bool GameScene::WallAt(int cx, int cz) const
{
    if (cx < 0 || cx >= MapW() || cz < 0 || cz >= MapH()) {
        return true; // マップの外は壁
    }
    if (wallGrid_.empty()) {
        return false;
    }
    return wallGrid_[static_cast<size_t>(cz) * MapW() + cx] != 0;
}

// 地面の上の線分（from→to）が壁のマスに当たるか。当たったら true、いちばん手前の当たり位置を hit に返す
bool GameScene::RaycastWalls(const Vector2& from, const Vector2& to, Vector2* hit) const
{
    const int x0 = static_cast<int>(std::floor((std::min)(from.x, to.x) + 0.5f));
    const int x1 = static_cast<int>(std::floor((std::max)(from.x, to.x) + 0.5f));
    const int z0 = static_cast<int>(std::floor((std::min)(from.y, to.y) + 0.5f));
    const int z1 = static_cast<int>(std::floor((std::max)(from.y, to.y) + 0.5f));
    float best = 2.0f;
    for (int cx = x0; cx <= x1; ++cx) {
        for (int cz = z0; cz <= z1; ++cz) {
            if (!WallAt(cx, cz)) {
                continue;
            }
            const Collision::Rect cell{ cx - 0.5f, cz - 0.5f, cx + 0.5f, cz + 0.5f };
            float t = 0.0f;
            if (Collision::SegmentRect(from, to, cell, &t) && t < best) {
                best = t;
            }
        }
    }
    if (best > 1.0f) {
        return false;
    }
    if (hit) {
        *hit = { from.x + (to.x - from.x) * best, from.y + (to.y - from.y) * best };
    }
    return true;
}

// 円（中心 p、半径 r）が壁のマスにめり込んでいたら、外へ押し出す
bool GameScene::PushOutOfWalls(Vector2& p, float r) const
{
    bool hit = false;
    for (int iter = 0; iter < 2; ++iter) {
        const int x0 = static_cast<int>(std::floor(p.x - r + 0.5f));
        const int x1 = static_cast<int>(std::floor(p.x + r + 0.5f));
        const int z0 = static_cast<int>(std::floor(p.y - r + 0.5f));
        const int z1 = static_cast<int>(std::floor(p.y + r + 0.5f));
        for (int cx = x0; cx <= x1; ++cx) {
            for (int cz = z0; cz <= z1; ++cz) {
                if (!WallAt(cx, cz)) {
                    continue;
                }
                const float qx = (std::max)(cx - 0.5f, (std::min)(cx + 0.5f, p.x)); // マスの中でいちばん近い点
                const float qz = (std::max)(cz - 0.5f, (std::min)(cz + 0.5f, p.y));
                const float dx = p.x - qx;
                const float dz = p.y - qz;
                const float d2 = dx * dx + dz * dz;
                if (d2 >= r * r) {
                    continue;
                }
                hit = true;
                if (d2 > 1e-8f) {
                    const float d = std::sqrt(d2);
                    p.x += dx / d * (r - d);
                    p.y += dz / d * (r - d);
                }
                else {
                    // 中心がマスの中：いちばん近い辺から外へ出す
                    const float l = p.x - (cx - 0.5f);
                    const float rt = (cx + 0.5f) - p.x;
                    const float b = p.y - (cz - 0.5f);
                    const float tp = (cz + 0.5f) - p.y;
                    const float m = (std::min)((std::min)(l, rt), (std::min)(b, tp));
                    if (m == l) { p.x -= l + r; }
                    else if (m == rt) { p.x += rt + r; }
                    else if (m == b) { p.y -= b + r; }
                    else { p.y += tp + r; }
                }
            }
        }
    }
    return hit;
}

//==================================================
// 描画
//==================================================
void GameScene::Draw()
{
    FrameStats::ScopedMs drawTimer(FrameStats::Cur().drawMs);
    // 背景のスカイボックス（キューブマップ）。3Dオブジェクトより先に描く
    EnvironmentMap::Instance().DrawSkybox(ctx_->dxCommon->GetCommandList(), ActiveCamera().GetViewMatrix(), ActiveCamera().GetProjectionMatrix());

    // 3Dオブジェクト（MultiMesh/MultiMaterial対応はModel側で処理）
    ctx_->object3dCommon->PreDraw();

    // ① 不透明(None)を先に描く
    for (size_t i = 0; i < objects_.size(); ++i) {
        if (i < visFlags_.size() && (visFlags_[i] & 1) == 0) {
            continue; // 画面に映らないブロックは描かない
        }
        if (objects_[i].object->GetObject3d().GetBlendMode() == BlendMode::None) {
            objects_[i].object->Draw();
        }
    }

    // ② ブレンドありは、カメラから遠い順に描く（近い物ほど後から重ねるため）
    {
        const Matrix4x4 cameraWorld = Inverse(ActiveCamera().GetViewMatrix());
        const Vector3 cameraPosition = { cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2] };

        std::vector<std::pair<float, GameObject*>> blended;
        for (size_t i = 0; i < objects_.size(); ++i) {
            ObjectSlot& slot = objects_[i];
            if (i < visFlags_.size() && (visFlags_[i] & 1) == 0) {
                continue;
            }
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
        DrawMapEditOverlay();
    }

    const int objectCount = static_cast<int>(objects_.size());
    const int spriteCount = static_cast<int>(sprites_.size());

    // テクスチャの選択（0:なし / i+1: textureFileList_[i]）。変わったら true
    auto textureCombo = [&](const char* label, int& index, const char* noneLabel) {
        const char* preview = (index > 0 && index - 1 < static_cast<int>(textureFileList_.size())) ? textureFileList_[index - 1].c_str() : noneLabel;
        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            if (ImGui::Selectable(noneLabel, index == 0)) {
                index = 0;
                changed = true;
            }
            for (int i = 0; i < static_cast<int>(textureFileList_.size()); ++i) {
                if (ImGui::Selectable(textureFileList_[i].c_str(), index == i + 1)) {
                    index = i + 1;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
        };

    // 役割ごとの見た目をまとめて変える（ブロック: テクスチャと色 / プレイヤー・敵: モデル・テクスチャ・色・大きさ）
    auto roleBulk = [&](const char* title, int role) {
        if (!ImGui::CollapsingHeader(title)) {
            return;
        }
        ImGui::PushID(role);
        int first = -1;
        for (int i = 0; i < objectCount; ++i) {
            if (!objects_[i].video && objects_[i].role == role) {
                first = i;
                break;
            }
        }
        if (role == 2) {
            if (textureCombo("テクスチャ", blockTexIndex_, "(なし: 単色)")) {
                blockStyleDirty_ = true;
            }
            if (ImGui::ColorEdit4("色（まとめて）", &blockTint_.x)) {
                blockStyleDirty_ = true;
            }
        }
        else if (first < 0) {
            ImGui::TextDisabled("(まだ置かれていません)");
        }
        else {
            int model = objects_[first].objIndex;
            if (ImGui::Combo("モデル（全部に適用）", &model, objFileListCStr_.data(), static_cast<int>(objFileListCStr_.size()))) {
                for (int i = 0; i < objectCount; ++i) {
                    if (!objects_[i].video && objects_[i].role == role) {
                        objects_[i].objIndex = model;
                        objects_[i].refit = true; // 差し替えたあと、大きさを自動で合わせる
                    }
                }
            }
            int tex = objects_[first].texOverride;
            if (textureCombo("テクスチャ（全部に適用）", tex, "(モデルのまま)")) {
                for (int i = 0; i < objectCount; ++i) {
                    if (!objects_[i].video && objects_[i].role == role) {
                        objects_[i].texOverride = tex;
                    }
                }
            }
            Vector4 color = objects_[first].object->GetObject3d().GetMaterial()->color;
            if (ImGui::ColorEdit4("色（全部に適用）", &color.x)) {
                for (int i = 0; i < objectCount; ++i) {
                    if (!objects_[i].video && objects_[i].role == role) {
                        objects_[i].object->GetObject3d().GetMaterial()->color = color;
                        objects_[i].konamiOriginalColor = color;
                    }
                }
            }
            if (ImGui::Button("大きさをマスに合わせる")) {
                for (int i = 0; i < objectCount; ++i) {
                    if (!objects_[i].video && objects_[i].role == role) {
                        objects_[i].refit = true;
                    }
                }
            }
        }
        ImGui::PopID();
        };

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
        if (ImGui::BeginMenu("ライトを追加")) {
            if (ImGui::MenuItem("ポイントライト")) {
                pendingCommands_.push_back([this]() {
                    selection_ = { SelectKind::SceneLight, AddLight(0, { 0.0f, 3.0f, 0.0f }) };
                    });
            }
            if (ImGui::MenuItem("スポットライト")) {
                pendingCommands_.push_back([this]() {
                    selection_ = { SelectKind::SceneLight, AddLight(1, { 0.0f, 4.0f, 0.0f }) };
                    });
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
    // 入力テスト：窓口が受け取っている値をそのまま表示する（キーボードとパッドの両方で動くか確認できる）
    if (ImGui::CollapsingHeader("入力テスト")) {
        const bool pad = gameInput_.GetDevice() == GameInput::Device::Gamepad;
        ImGui::Text("使用中: %s", pad ? "ゲームパッド" : "キーボード＋マウス");
        ImGui::Text("移動: (%.2f, %.2f)", gameInput_.Move().x, gameInput_.Move().y);
        if (gameInput_.HasAim()) {
            ImGui::Text("狙い: (%.2f, %.2f)", gameInput_.Aim().x, gameInput_.Aim().y);
        }
        else {
            ImGui::TextDisabled("狙い: なし");
        }
        ImGui::Text("撃つ: %s  ジャンプ: %s", gameInput_.Fire() ? "ON" : "-", gameInput_.Jump() ? "ON" : "-");
        ImGui::Text("再生中: %s", playing_ ? "はい" : "いいえ");
        if (playing_) {
            {
                // 1フレームの描画量（前のフレームの結果）
                const FrameStats::Data& f = FrameStats::Last();
                ImGui::Separator();
                ImGui::Text("フレーム間隔 %.1fms (%.0fFPS)  Update %.2fms  Draw命令作成 %.2fms", f.frameMs, f.frameMs > 0.01f ? 1000.0f / f.frameMs : 0.0f, f.updateMs, f.drawMs);
                ImGui::Text("ImGui作成 %.2fms  Present+GPU待ち %.2fms  60FPS待ち %.2fms", f.uiMs, f.presentMs, f.sleepMs);
                ImGui::Text("描画命令 計%u回  三角形 %llu個（影を除く）", FrameStats::TotalCalls(f), static_cast<unsigned long long>(FrameStats::TotalTriangles(f, false)));
                ImGui::Text("  モデル %u回  影 %u回  パーティクル %u回  スプライト %u回  板ポリ %u回", f.calls[FrameStats::kModel], f.calls[FrameStats::kShadow], f.calls[FrameStats::kParticle], f.calls[FrameStats::kSprite], f.calls[FrameStats::kPlanes]);
                ImGui::TextDisabled("Update+Drawよりフレーム間隔が大きい → GPU待ち／VSync。命令回数が多い → 重い原因はCPU側。");
            }
        }
        int stickMode = static_cast<int>(gameInput_.rightStick);
        const char* stickNames[] = { "右スティック: Xbox系 (Rx/Ry)", "右スティック: DualShock・汎用 (Z/Rz)" };
        if (ImGui::Combo("右スティックの軸", &stickMode, stickNames, IM_ARRAYSIZE(stickNames))) {
            gameInput_.rightStick = static_cast<GameInput::RightStick>(stickMode);
        }
        ImGui::SliderFloat("スティックの遊び", &gameInput_.deadZone, 0.05f, 0.6f);
        ImGui::TextDisabled("移動 WASD/矢印/左スティック  狙い マウス/右スティック  撃つ マウス左/J/RB・RT  ジャンプ Space/A");
    }

    if (ImGui::CollapsingHeader("演出（画面の揺れ・ヒットストップ）")) {
        ImGui::BeginDisabled(!playing_);
        if (ImGui::Button("揺らす（テスト）")) {
            camFx_.Shake(0.3f, 0.3f);
        }
        ImGui::SameLine();
        if (ImGui::Button("ヒットストップ（テスト）")) {
            camFx_.HitStop(6);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("テストは再生中だけ。ゲームのコードからは camFx_.Shake(大きさ, 秒) と camFx_.HitStop(フレーム数) で呼べます。");
    }

    if (!playing_) {
        if (Ground3D()) {
            if (ImGui::CollapsingHeader("マップ編集（3Dマップ）", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextWrapped("地面はXZ平面（上がY）。マップは48x48マスで、役割「ブロック」が壁になります（ゲームのコードから WallAt / RaycastWalls で調べられます）。");
                ImGui::Checkbox("マップ編集モード（ビューポートをクリックして置く）", &mapEdit_);
                if (mapEdit_) {
                    const char* tools3d[] = { "チップを置く", "消しゴム", "塗り替え（置いたチップに適用）", "敵", "プレイヤー開始位置", "範囲選択", "貼り付け" };
                    const int toolValue[] = { 0, 1, 6, 2, 5, 7, 8 };
                    int toolIndex = 0;
                    for (int k = 0; k < 7; ++k) {
                        if (toolValue[k] == mapTool_) { toolIndex = k; }
                    }
                    if (ImGui::Combo("ツール", &toolIndex, tools3d, IM_ARRAYSIZE(tools3d))) {
                        mapTool_ = toolValue[toolIndex];
                    }
                    if (mapTool_ == 2) {
                        ImGui::DragFloat("敵の大きさ", &enemySize_, 0.02f, 0.3f, 4.0f);
                        ImGui::TextDisabled("これから置く敵の配置マーカーの大きさ（最大辺）。");
                    }
                    if (mapTool_ == 0 || mapTool_ == 6) {
                        // チップの設定：種類を選ぶと、その種類の標準の高さ・色になる。高さと色はこのあと1つずつ変えられる
                        if (ImGui::BeginCombo("チップの種類", kChips[mapChip_].label)) {
                            for (int k = 0; k < kChipCount; ++k) {
                                if (ImGui::Selectable(kChips[k].label, mapChip_ == k)) {
                                    mapChip_ = k;
                                    mapProps_.chip = k;
                                    mapProps_.height = kChips[k].height;
                                    mapProps_.ownColor = kChips[k].ownColor;
                                    if (kChips[k].ownColor) {
                                        mapProps_.color = kChips[k].color;
                                    }
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::DragFloat("高さ", &mapProps_.height, 0.02f, 0.02f, 8.0f);
                        ImGui::Checkbox("色を指定する（オフなら、全体のブロックの色）", &mapProps_.ownColor);
                        if (mapProps_.ownColor) {
                            ImGui::ColorEdit3("色", &mapProps_.color.x);
                        }
                    }
                    if (mapTool_ == 0 || mapTool_ == 1 || mapTool_ == 6) {
                        ImGui::Checkbox("四角でまとめて（ドラッグした範囲）", &mapRect_);
                        if (!mapRect_) {
                            ImGui::SliderInt("ブラシの大きさ", &mapBrush_, 1, 5);
                        }
                    }
                    if (mapTool_ == 7 || mapTool_ == 8) {
                        ImGui::TextWrapped("範囲選択：ドラッグで四角を選びます。コピーしたら、「貼り付け」ツールに切り替えてクリックした位置に置きます。");
                        ImGui::BeginDisabled(!selValid_);
                        if (ImGui::Button("コピー")) {
                            CopySelection(false);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("切り取り")) {
                            CopySelection(true);
                        }
                        ImGui::EndDisabled();
                        ImGui::BeginDisabled(clip_.empty());
                        if (ImGui::Button("右に90度回す")) {
                            TransformClip(0);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("左右反転")) {
                            TransformClip(1);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("上下反転")) {
                            TransformClip(2);
                        }
                        if (ImGui::Button("貼り付けツールにする")) {
                            mapTool_ = 8;
                        }
                        ImGui::EndDisabled();
                        ImGui::Text("コピー済み: %d マス（%d x %d）", static_cast<int>(clip_.size()), clipW_, clipH_);
                    }
                    if (ImGui::Button("元に戻す (Ctrl+Z)")) {
                        pendingCommands_.push_back([this]() { UndoMapEdit(); });
                    }
                    ImGui::TextWrapped("左クリック/ドラッグ: 置く  右クリック/ドラッグ か Ctrl+左: 消す");
                    ImGui::TextWrapped("カメラ  ホイール: ズーム  中ドラッグ: つかんで移動  Shift+中ドラッグ: 回る・角度  WASD/矢印: 移動（Shiftで速く）  Q/E: 回る  Z/X: 角度  1:真上 2:斜め 3:低め  F: プレイヤー（なければ中心）へ");
                }
                if (ImGui::TreeNode("見た目をまとめて変更")) {
                    roleBulk("ブロックの見た目（まとめて変更）", 2);
                    roleBulk("プレイヤー開始位置の見た目", 1);
                    roleBulk("敵の配置の見た目（まとめて変更）", 3);
                    ImGui::TreePop();
                }
                ImGui::InputText("CSV", csvPath_, sizeof(csvPath_));
                if (ImGui::Button("CSVへ書き出し")) {
                    sceneMessage_ = ExportStageCsv(csvPath_) ? "csv exported" : "csv export failed";
                }
                ImGui::SameLine();
                if (ImGui::Button("CSVを読み込む")) {
                    pendingCommands_.push_back([this]() {
                        sceneMessage_ = ImportStageCsv(csvPath_) ? "csv imported" : "csv not found";
                        });
                }
                ImGui::TextDisabled("CSV: 0=空 1=ブロック P=開始 E=敵 G=ゴール（48列x48行・同名は上書き）");
            }
        }
        ImGui::InputText("シーンファイル", scenePath_, sizeof(scenePath_));
        if (ImGui::Button("シーンを保存")) {
            sceneMessage_ = SaveScene(scenePath_) ? "scene saved" : "scene save failed";
        }
        ImGui::SameLine();
        if (ImGui::Button("シーンを読み込む")) {
            pendingCommands_.push_back([this]() {
                sceneMessage_ = LoadScene(scenePath_) ? "scene loaded" : "scene file not found";
                });
        }
        if (!sceneMessage_.empty()) {
            ImGui::TextDisabled("%s", sceneMessage_.c_str());
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

    // 3Dオブジェクト（ブロックは数が多いので、まとめて折りたたみ、見えている行だけ描く）
    auto drawObjectRow = [&](int i) {
        std::string label = objects_[i].video
            ? std::format("[動画3D {}] {}", i, objects_[i].videoName)
            : std::format("[オブジェクト {}] {}{}{}", i, objFileList_[objects_[i].objIndex], objects_[i].name[0] ? "  #" : "", objects_[i].name);
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
    };
    std::vector<int> blockRows;
    for (int i = 0; i < objectCount; ++i) {
        if (!objects_[i].video && objects_[i].role == 2) {
            blockRows.push_back(i);
            continue;
        }
        drawObjectRow(i);
    }
    if (!blockRows.empty()) {
        ImGui::PushID("blockRows");
        if (ImGui::TreeNode("blocks", "ブロック（%d個）", static_cast<int>(blockRows.size()))) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(blockRows.size()));
            while (clipper.Step()) {
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                    drawObjectRow(blockRows[r]);
                }
            }
            ImGui::TreePop();
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

    // 配置したライト（ポイント / スポット）
    for (int i = 0; i < static_cast<int>(lights_.size()); ++i) {
        const std::string label = std::format("[{} {}]", lights_[i].type == 0 ? "ポイントライト" : "スポットライト", i);
        const bool selected = selection_.kind == SelectKind::SceneLight && selection_.index == i;
        ImGui::PushID(3000 + i);
        if (ImGui::Selectable(label.c_str(), selected)) {
            selection_ = { SelectKind::SceneLight, i };
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            selection_ = { SelectKind::SceneLight, i };
        }
        if (ImGui::BeginPopupContextItem("LightContext")) {
            if (ImGui::MenuItem("複製", nullptr, false, !playing_)) {
                pendingCommands_.push_back([this, i]() { DuplicateLight(i); });
            }
            if (ImGui::MenuItem("削除", nullptr, false, !playing_)) {
                pendingCommands_.push_back([this, i]() {
                    if (i < static_cast<int>(lights_.size())) {
                        lights_.erase(lights_.begin() + i);
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
    ImGui::Text("エミッターはひし形、ポイントライトは四角、スポットは三角のマーク");
    ImGui::Text("インスタンス平面: 1グループ = N枚の平面");
    ImGui::Text("3Dを画面端までドラッグ: カメラが追従");
    ImGui::Checkbox("位置マーカーを表示", &showMarkers_);
    ImGui::Checkbox("スカイボックスを表示", &EnvironmentMap::Instance().skyboxVisible);
    ImGui::Separator();
    ImGui::Text("辺をドラッグ: その方向に伸縮");
    ImGui::Text("角をドラッグ: 拡大縮小 (Shift: 自由変形)");
    ImGui::Text("(Shift+3Dをドラッグ: 地面の上を移動)");
    ImGui::Text("ビューポートを右ボタン長押し: WASDで移動");
    ImGui::Text("ホイールクリック+ドラッグ: カメラの向きを変える");
    ImGui::Text("  マウス=視点 WASD=移動 Space/Shift=上昇/下降");
    ImGui::Text("リストを右クリック: 追加 / 複製 / 削除");

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
    if (!playing_ && (selection_.kind == SelectKind::Object || selection_.kind == SelectKind::Sprite || selection_.kind == SelectKind::Planes || selection_.kind == SelectKind::Emitter || selection_.kind == SelectKind::SceneLight)) {
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
        if (!slot.video) {
            ImGui::Combo("ゲーム用の役割", &slot.role, kRoleNames, IM_ARRAYSIZE(kRoleNames));
            ImGui::InputText("名前", slot.name, IM_ARRAYSIZE(slot.name));
            if (slot.role == 2 && !playing_) {
                if (ImGui::Button("位置を整数のマスに合わせる")) {
                    Vector3& tr = object.GetTransform().Translate;
                    tr.x = std::round(tr.x);
                    tr.z = std::round(tr.z);
                }
                ImGui::TextDisabled("ブロックのテクスチャ・色は「ブロックの見た目」でまとめて変更");
            }
            if (slot.role != 2) {
                textureCombo("テクスチャ（上書き）", slot.texOverride, "(モデルのまま)");
            }
            if ((slot.role == 1 || slot.role == 3) && !playing_) {
                if (ImGui::Button("大きさをマスに合わせる")) {
                    slot.refit = true;
                }
            }
        }
        ImGui::DragFloat3("スケール", &object.GetTransform().Scale.x, 0.01f);
        ImGui::DragFloat3("回転", &object.GetTransform().Rotate.x, 0.01f);
        ImGui::DragFloat3("移動", &object.GetTransform().Translate.x, 0.01f);
        ImGui::ColorEdit4("色", &object.GetMaterial()->color.x);
        const char* lightingModes[] = { "なし", "ランバート", "ハーフランバート" };
        ImGui::Combo("ライティング", &object.GetMaterial()->lightingMode, lightingModes, IM_ARRAYSIZE(lightingModes));
        const char* specularModes[] = { "なし", "Phong", "Blinn-Phong", "PBR (GGX)" };
        ImGui::Combo("鏡面反射", &object.GetMaterial()->specularMode, specularModes, IM_ARRAYSIZE(specularModes));
        if (object.GetMaterial()->specularMode == 3) {
            ImGui::SliderFloat("粗さ (roughness)", &object.GetMaterial()->roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("金属度 (metallic)", &object.GetMaterial()->metallic, 0.0f, 1.0f);
        }
        else if (object.GetMaterial()->specularMode != 0) {
            ImGui::SliderFloat("光沢(鋭さ)", &object.GetMaterial()->shininess, 1.0f, 256.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
        }
        int blendMode = static_cast<int>(object.GetBlendMode());
        if (ImGui::Combo("ブレンドモード", &blendMode, kBlendModeNames, kBlendModeCount)) {
            object.SetBlendMode(static_cast<BlendMode>(blendMode));
        }
        ImGui::SliderFloat("アルファカットオフ", &object.GetMaterial()->alphaCutoff, 0.0f, 1.0f);
        bool castShadow = object.GetCastShadow();
        if (ImGui::Checkbox("影を落とす", &castShadow)) {
            object.SetCastShadow(castShadow);
        }
        ImGui::SameLine();
        bool receiveShadow = object.GetMaterial()->receiveShadow > 0.5f;
        if (ImGui::Checkbox("影を受ける", &receiveShadow)) {
            object.GetMaterial()->receiveShadow = receiveShadow ? 1.0f : 0.0f;
        }
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

        // 影（シャドウマッピング）
        ImGui::Separator();
        bool shadowEnabled = directionalLight_->shadowEnabled != 0;
        if (ImGui::Checkbox("影を有効にする", &shadowEnabled)) {
            directionalLight_->shadowEnabled = shadowEnabled ? 1 : 0;
        }
        ImGui::SliderFloat("影の濃さ", &directionalLight_->shadowStrength, 0.0f, 1.0f);
        ImGui::DragFloat("バイアス", &directionalLight_->shadowBias, 0.0001f, 0.0f, 0.05f, "%.4f");
        ImGui::DragFloat("影の範囲(半径)", &shadowRange_, 0.1f, 2.0f, 200.0f);
        ImGui::DragFloat3("影の範囲の中心", &shadowCenter_.x, 0.1f);
        ImGui::TextDisabled("影が縞模様ならバイアスを上げる。影が浮くなら下げる");
        ImGui::Separator();
        ImGui::TextDisabled("ポイント/スポットライトは Hierarchy の「+ 追加」→「ライトを追加」");
    }
    else if (selection_.kind == SelectKind::SceneLight && selection_.index < static_cast<int>(lights_.size())) {
        LightSlot& l = lights_[selection_.index];
        ImGui::Text(l.type == 0 ? "ポイントライト [%d]" : "スポットライト [%d]", selection_.index);
        ImGui::Separator();
        const char* types[] = { "ポイント", "スポット" };
        ImGui::Combo("種類", &l.type, types, IM_ARRAYSIZE(types));
        ImGui::ColorEdit4("色", &l.color.x);
        ImGui::DragFloat3("位置", &l.position.x, 0.05f);
        ImGui::DragFloat("強さ (0で消灯)", &l.intensity, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat("届く距離", &l.range, 0.05f, 0.1f, 1000.0f);
        ImGui::DragFloat("距離の減衰", &l.decay, 0.01f, 0.01f, 10.0f);
        if (l.type == 1) {
            if (ImGui::DragFloat3("向き", &l.direction.x, 0.01f)) {
                l.direction = Normalize(l.direction);
            }
            if (ImGui::SliderFloat("外側の角度", &l.outerDeg, 1.0f, 89.0f)) {
                l.innerDeg = (std::min)(l.innerDeg, l.outerDeg - 0.5f);
            }
            if (ImGui::SliderFloat("内側の角度 (ここから減衰)", &l.innerDeg, 0.5f, 88.5f)) {
                l.innerDeg = (std::min)(l.innerDeg, l.outerDeg - 0.5f);
            }
        }
        ImGui::TextDisabled("ビューポートの目印をドラッグして移動 (Shift: 水平)");
    }
    else {
        ImGui::Text("何も選択されていません");
    }

    ImGui::End();
#endif // USE_IMGUI
}

//==================================================
// マップ編集（AL4_2D風）
//==================================================
// オブジェクトの原点から見た、モデルの中心のずれ（回転は無視）
Vector3 GameScene::ModelCenterOffset(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects_.size())) {
        return { 0.0f, 0.0f, 0.0f };
    }
    Object3d& object = objects_[objectIndex].object->GetObject3d();
    const Model* model = object.GetModel();
    if (!model) {
        return { 0.0f, 0.0f, 0.0f };
    }
    const Vector3& bmin = model->GetBoundsMin();
    const Vector3& bmax = model->GetBoundsMax();
    const Vector3& s = object.GetTransform().Scale;
    return { (bmin.x + bmax.x) * 0.5f * s.x, (bmin.y + bmax.y) * 0.5f * s.y, (bmin.z + bmax.z) * 0.5f * s.z };
}

int GameScene::FindBlockAt(int x, int y)
{
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (objects_[i].video || objects_[i].role != 2) {
            continue;
        }
        const Vector2 t = Planar(objects_[i].object->GetObject3d().GetTransform().Translate);
        if (static_cast<int>(std::lround(t.x)) == x && static_cast<int>(std::lround(t.y)) == y) {
            return i;
        }
    }
    return -1;
}

// ブロックの今の設定
GameScene::BlockProps GameScene::GetBlockProps(int objectIndex)
{
    BlockProps p;
    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects_.size())) {
        return p;
    }
    const ObjectSlot& s = objects_[objectIndex];
    p.chip = s.chip;
    p.height = Ground3D() ? s.object->GetObject3d().GetTransform().Scale.y : wallHeight_;
    p.ownColor = s.ownColor;
    p.color = s.ownTint;
    return p;
}

// ブロックを置く/消す。変化したら true（描画中のリソースを壊さないよう、pendingCommands_ の中で呼ぶこと）
// props があれば、その設定（種類・高さ・色）で置く。既にあるマスなら作り替える
bool GameScene::SetBlockCell(int x, int y, bool solid, const BlockProps* props)
{
    const int existing = FindBlockAt(x, y);
    if (solid) {
        BlockProps p;
        p.height = wallHeight_;
        if (props) {
            p = *props;
        }
        int n = existing;
        if (existing < 0) {
            const auto it = std::find(objFileList_.begin(), objFileList_.end(), kCubeName);
            if (it == objFileList_.end()) {
                return false;
            }
            n = AddObject(static_cast<int>(it - objFileList_.begin()), CellWorld(static_cast<float>(x), static_cast<float>(y), p.height * 0.5f));
            objects_[n].role = 2;
        }
        else if (!props) {
            return false;
        }
        ObjectSlot& slot = objects_[n];
        slot.chip = p.chip;
        slot.ownColor = p.ownColor;
        slot.ownTint = p.color;
        if (Ground3D()) {
            Transform& t = slot.object->GetObject3d().GetTransform();
            t.Scale = { 1.0f, p.height, 1.0f }; // 背の高さ（チップごと・マスごと）
            t.Translate = CellWorld(static_cast<float>(x), static_cast<float>(y), p.height * 0.5f);
        }
        ApplyBlockStyle(n);
        return true;
    }
    if (!solid && existing >= 0) {
        objects_.erase(objects_.begin() + existing);
        selection_ = {};
        return true;
    }
    return false;
}

// 選択範囲のチップをコピーする（cut なら元を消す）
void GameScene::CopySelection(bool cut)
{
    if (!selValid_) {
        return;
    }
    clip_.clear();
    const int ax = (std::min)(selX0_, selX1_);
    const int bx = (std::max)(selX0_, selX1_);
    const int ay = (std::min)(selY0_, selY1_);
    const int by = (std::max)(selY0_, selY1_);
    clipW_ = bx - ax + 1;
    clipH_ = by - ay + 1;
    for (int y = ay; y <= by; ++y) {
        for (int x = ax; x <= bx; ++x) {
            const int idx = FindBlockAt(x, y);
            if (idx < 0) {
                continue;
            }
            ClipCell cc;
            cc.dx = x - ax;
            cc.dy = y - ay;
            cc.p = GetBlockProps(idx);
            clip_.push_back(cc);
            if (cut) {
                BlockEdit be;
                be.x = x;
                be.y = y;
                be.before = true;
                be.beforeProps = cc.p;
                mapStroke_.push_back(be);
                pendingCommands_.push_back([this, x, y]() { SetBlockCell(x, y, false); });
            }
        }
    }
}

// コピーしたものを変形する（0:右に90度回す 1:左右反転 2:上下反転）
void GameScene::TransformClip(int mode)
{
    if (mode == 0) {
        for (ClipCell& cc : clip_) {
            const int nx = cc.dy;
            const int ny = clipW_ - 1 - cc.dx;
            cc.dx = nx;
            cc.dy = ny;
        }
        std::swap(clipW_, clipH_);
    }
    else if (mode == 1) {
        for (ClipCell& cc : clip_) { cc.dx = clipW_ - 1 - cc.dx; }
    }
    else {
        for (ClipCell& cc : clip_) { cc.dy = clipH_ - 1 - cc.dy; }
    }
}

// (x, y) の近くにある敵（とゴール）を消す
void GameScene::EraseRoleObjectsNear(float x, float y, float radius, bool includeGoal)
{
    for (int i = static_cast<int>(objects_.size()) - 1; i >= 0; --i) {
        const int role = objects_[i].role;
        if (objects_[i].video || (role != 3 && !(includeGoal && role == 4))) {
            continue;
        }
        const Vector3 center = [&] {
            const Vector3 off = ModelCenterOffset(i);
            const Vector3& t = objects_[i].object->GetObject3d().GetTransform().Translate;
            return Vector3{ t.x + off.x, t.y + off.y, t.z + off.z };
            }();
        const Vector2 cp = Planar(center);
        if (std::fabs(cp.x - x) < radius && std::fabs(cp.y - y) < radius) {
            objects_.erase(objects_.begin() + i);
            selection_ = {};
        }
    }
}

// 役割付きのオブジェクトを、モデルの中心がマスの中心に来るように置く（大きさは最大辺が fitSize になるよう合わせる）
int GameScene::AddRoleObject(int role, const std::string& modelName, const Vector3& cellCenter, float fitSize, const Vector4& color, bool shield)
{
    auto it = std::find(objFileList_.begin(), objFileList_.end(), modelName);
    if (it == objFileList_.end()) {
        it = std::find(objFileList_.begin(), objFileList_.end(), kCubeName);
    }
    if (it == objFileList_.end()) {
        return -1;
    }
    const int n = AddObject(static_cast<int>(it - objFileList_.begin()), cellCenter);
    objects_[n].role = role;
    objects_[n].shield = shield;
    Object3d& o = objects_[n].object->GetObject3d();
    if (const Model* model = o.GetModel()) {
        const Vector3& bmin = model->GetBoundsMin();
        const Vector3& bmax = model->GetBoundsMax();
        const float extent = (std::max)({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 0.0001f });
        const float s = fitSize / extent;
        o.GetTransform().Scale = { s, s, s };
        o.GetTransform().Translate = {
            cellCenter.x - (bmin.x + bmax.x) * 0.5f * s,
            cellCenter.y - (bmin.y + bmax.y) * 0.5f * s,
            cellCenter.z - (bmin.z + bmax.z) * 0.5f * s };
    }
    o.GetMaterial()->color = color;
    objects_[n].konamiOriginalColor = color;
    return n;
}

void GameScene::UndoMapEdit()
{
    if (mapUndo_.empty()) {
        return;
    }
    const std::vector<BlockEdit> stroke = mapUndo_.back();
    mapUndo_.pop_back();
    for (auto it = stroke.rbegin(); it != stroke.rend(); ++it) {
        if (it->before) {
            SetBlockCell(it->x, it->y, true, &it->beforeProps); // 逆順に、変更前の状態（種類・高さ・色）へ戻す
        }
        else {
            SetBlockCell(it->x, it->y, false);
        }
    }
}

// マップ編集：編集カメラ、マウス → マス、配置・消去、アンドゥ
void GameScene::UpdateMapEdit()
{
#ifdef USE_IMGUI
    const EditorLayout::ViewportInput& vp = EditorLayout::GetViewportInput();
    ImGuiIO& io = ImGui::GetIO();
    const Input& input = *ctx_->input;
    const float screenW = static_cast<float>(WinApp::kGameWidth);
    const float screenH = static_cast<float>(WinApp::kGameHeight);

    // 編集モードに入った直後は、プレイヤー（なければ最初のブロック）を映す
    if (!mapEditPrev_) {
        if (Ground3D()) {
            editCamX_ = MapW() * 0.5f;
            editCamY_ = MapH() * 0.5f;
            editDist_ = (std::max)(editDist_, 30.0f);
            editYaw_ = -1.5707963f;
            editPitch_ = 1.0471976f;
        }
        for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
            if (objects_[i].role == 1 || objects_[i].role == 2) {
                const Vector3& t = objects_[i].object->GetObject3d().GetTransform().Translate;
                editCamX_ = t.x;
                editCamY_ = Ground3D() ? t.z : (std::max)(t.y, 4.0f);
                if (objects_[i].role == 1) {
                    break;
                }
            }
        }
    }
    mapEditPrev_ = true;

    // 編集カメラ
    //  3Dマップ：地面の1点を中心に回る軌道カメラ（常に水平なので傾かない）
    //    ホイール:ズーム  中ドラッグ:つかんで移動  Shift+中ドラッグ:回る・角度  WASD/矢印:移動  Q/E:回る  Z/X:角度  1/2/3:真上/斜め/低め  F:中心へ
    //  それ以外（2D）：中ドラッグで画面移動、ホイールでズーム、矢印/WASDで移動
    const float tanHalf = std::tan(kPfFovY * 0.5f);
    if (Ground3D()) {
        const float kPi = 3.14159265f;
        const float minPitch = 0.30f;
        const float maxPitch = 1.553f; // 約89度
        if (vp.valid) {
            editDist_ = (std::max)(6.0f, (std::min)(90.0f, editDist_ * std::pow(0.9f, vp.wheel)));
        }
        // 画面の上方向・右方向（地面の上の向き）
        auto basis = [&](Vector2& fwd, Vector2& right) {
            fwd = { -std::cos(editYaw_), -std::sin(editYaw_) };
            right = { fwd.y, -fwd.x };
        };
        Vector2 fwd{}, right{};
        basis(fwd, right);
        if (vp.valid && vp.middleDown) {
            if (io.KeyShift) {
                editYaw_ += vp.mouseDelta.x * 0.006f;
                editPitch_ += vp.mouseDelta.y * 0.006f;
            }
            else {
                const float worldPerPixel = editDist_ * 2.0f * tanHalf / screenH;
                const float vertical = worldPerPixel / (std::max)(0.35f, std::sin(editPitch_)); // 斜めのときは奥行きが縮んで見えるぶん補正
                editCamX_ += -right.x * vp.mouseDelta.x * worldPerPixel + fwd.x * vp.mouseDelta.y * vertical;
                editCamY_ += -right.y * vp.mouseDelta.x * worldPerPixel + fwd.y * vp.mouseDelta.y * vertical;
            }
        }
        if (!io.WantTextInput) {
            const float speed = (io.KeyShift ? 0.04f : 0.016f) * editDist_;
            float mx = 0.0f;
            float my = 0.0f;
            if (input.PushKey(DIK_LEFT) || input.PushKey(DIK_A)) { mx -= 1.0f; }
            if (input.PushKey(DIK_RIGHT) || input.PushKey(DIK_D)) { mx += 1.0f; }
            if (input.PushKey(DIK_UP) || input.PushKey(DIK_W)) { my += 1.0f; }
            if (input.PushKey(DIK_DOWN) || input.PushKey(DIK_S)) { my -= 1.0f; }
            editCamX_ += (right.x * mx + fwd.x * my) * speed;
            editCamY_ += (right.y * mx + fwd.y * my) * speed;
            if (input.PushKey(DIK_Q)) { editYaw_ -= 0.03f; }
            if (input.PushKey(DIK_E)) { editYaw_ += 0.03f; }
            if (!io.KeyCtrl) {
                if (input.PushKey(DIK_Z)) { editPitch_ += 0.02f; }
                if (input.PushKey(DIK_X)) { editPitch_ -= 0.02f; }
            }
            if (input.TriggerKey(DIK_1)) { editPitch_ = maxPitch; }
            if (input.TriggerKey(DIK_2)) { editPitch_ = 0.96f; }
            if (input.TriggerKey(DIK_3)) { editPitch_ = 0.5f; }
            if (input.TriggerKey(DIK_F)) {
                editCamX_ = MapW() * 0.5f;
                editCamY_ = MapH() * 0.5f;
                for (const ObjectSlot& o : objects_) { // プレイヤー開始位置があればそこへ
                    if (o.role == 1 && !o.video) {
                        const Vector3& t = o.object->GetObject3d().GetTransform().Translate;
                        editCamX_ = t.x;
                        editCamY_ = t.z;
                        break;
                    }
                }
            }
        }
        if (editYaw_ > kPi) { editYaw_ -= 2.0f * kPi; }
        if (editYaw_ < -kPi) { editYaw_ += 2.0f * kPi; }
        editPitch_ = (std::max)(minPitch, (std::min)(maxPitch, editPitch_));
    }
    else {
        if (vp.valid) {
            editDist_ = (std::max)(6.0f, (std::min)(60.0f, editDist_ - vp.wheel * 1.5f));
            if (vp.middleDown) {
                const float worldPerPixel = editDist_ * 2.0f * tanHalf / screenH;
                editCamX_ -= vp.mouseDelta.x * worldPerPixel;
                editCamY_ += vp.mouseDelta.y * worldPerPixel;
            }
        }
        if (!io.WantTextInput) {
            const float speed = io.KeyShift ? 0.6f : 0.25f;
            if (input.PushKey(DIK_LEFT) || input.PushKey(DIK_A)) { editCamX_ -= speed; }
            if (input.PushKey(DIK_RIGHT) || input.PushKey(DIK_D)) { editCamX_ += speed; }
            if (input.PushKey(DIK_UP) || input.PushKey(DIK_W)) { editCamY_ += speed; }
            if (input.PushKey(DIK_DOWN) || input.PushKey(DIK_S)) { editCamY_ -= speed; }
        }
    }
    editCamX_ = (std::max)(-5.0f, (std::min)(static_cast<float>(MapW()) + 5.0f, editCamX_));
    editCamY_ = (std::max)(-5.0f, (std::min)(static_cast<float>(MapH()) + 5.0f, editCamY_));

    if (Ground3D()) {
        const float cp = std::cos(editPitch_);
        const Vector3 target{ editCamX_, 0.0f, editCamY_ };
        const Vector3 eye{ editCamX_ + editDist_ * cp * std::cos(editYaw_), editDist_ * std::sin(editPitch_), editCamY_ + editDist_ * cp * std::sin(editYaw_) };
        gameCamera_.SetLookAt(eye, target, { 0.0f, 1.0f, 0.0f });
    }
    else {
        Transform& ct = gameCamera_.GetTransform();
        ct.Translate = { editCamX_, editCamY_, -editDist_ };
        ct.Rotate = { 0.0f, 0.0f, 0.0f };
        gameCamera_.Update(input);
    }

    // マウス → 編集平面（z = -0.5）の点 → マス
    mapHoverValid_ = false;
    if (vp.valid && vp.mouse.x >= 0.0f && vp.mouse.x < screenW && vp.mouse.y >= 0.0f && vp.mouse.y < screenH) {
        const Picking::Ray ray = Picking::ScreenToRay(vp.mouse, gameCamera_.GetViewMatrix(), gameCamera_.GetProjectionMatrix());
        Vector3 hit{};
        const bool planeHit = Ground3D()
            ? Picking::RayIntersectsPlane(ray, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, hit)
            : Picking::RayIntersectsPlane(ray, { 0.0f, 0.0f, kEditPlaneZ }, { 0.0f, 0.0f, -1.0f }, hit);
        if (planeHit) {
            const Vector2 hp2 = Planar(hit);
            const int cx = static_cast<int>(std::floor(hp2.x + 0.5f));
            const int cy = static_cast<int>(std::floor(hp2.y + 0.5f));
            if (cx >= 0 && cx < MapW() && cy >= 0 && cy < MapH()) {
                mapHoverValid_ = true;
                mapHoverX_ = cx;
                mapHoverY_ = cy;
            }
        }
    }

    // ボタンの状態：押し始めがビューポート上のときだけ「塗る/消す」を始める
    if (vp.clicked && mapHoverValid_) {
        mapPainting_ = true;
    }
    if (vp.rightClicked && mapHoverValid_) {
        mapErasing_ = true;
    }
    if (!vp.down) {
        mapPainting_ = false;
    }
    if (!vp.rightDown) {
        mapErasing_ = false;
    }
    if (vp.middleDown) {
        mapPainting_ = false;
        mapErasing_ = false;
    }

    // ---- 3Dマップ：チップ・四角塗り・塗り替え・範囲選択・貼り付け ----
    const bool chip3D = Ground3D() && (mapTool_ == 0 || mapTool_ == 1 || mapTool_ == 6 || mapTool_ == 7 || mapTool_ == 8);
    if (chip3D) {
        wallHeight_ = mapProps_.height;
        const bool rectTool = mapTool_ == 7 || (mapRect_ && (mapTool_ == 0 || mapTool_ == 1 || mapTool_ == 6));
        // 1マスぶんの変更。mode 0:空きに置く 1:消す 2:置く／上書き 3:あるものだけ上書き
        auto editCell = [&](int x, int y, int mode, const BlockProps& props) {
            if (x < 0 || x >= MapW() || y < 0 || y >= MapH()) {
                return;
            }
            const int idx = FindBlockAt(x, y);
            BlockEdit be;
            be.x = x;
            be.y = y;
            be.before = idx >= 0;
            if (idx >= 0) {
                be.beforeProps = GetBlockProps(idx);
            }
            if (mode == 1) {
                if (idx >= 0) {
                    mapStroke_.push_back(be);
                    pendingCommands_.push_back([this, x, y]() { SetBlockCell(x, y, false); });
                }
                // そのマスの敵・ゴールも消す
                pendingCommands_.push_back([this, x, y]() { EraseRoleObjectsNear(static_cast<float>(x), static_cast<float>(y), 0.7f, true); });
                return;
            }
            if ((mode == 0 && idx >= 0) || (mode == 3 && idx < 0)) {
                return;
            }
            if (idx >= 0) {
                const BlockProps& b = be.beforeProps;
                const bool sameColor = !props.ownColor || (std::fabs(b.color.x - props.color.x) < 0.002f && std::fabs(b.color.y - props.color.y) < 0.002f && std::fabs(b.color.z - props.color.z) < 0.002f);
                if (b.chip == props.chip && std::fabs(b.height - props.height) < 0.001f && b.ownColor == props.ownColor && sameColor) {
                    return; // 変わらない
                }
            }
            mapStroke_.push_back(be);
            pendingCommands_.push_back([this, x, y, props]() { SetBlockCell(x, y, true, &props); });
        };
        const bool eraseMode = mapTool_ == 1 || io.KeyCtrl;
        if (rectTool) {
            if (mapHoverValid_) {
                rectX1_ = mapHoverX_;
                rectY1_ = mapHoverY_;
            }
            if (!rectDrag_ && mapHoverValid_ && (vp.clicked || vp.rightClicked) && !vp.middleDown) {
                rectDrag_ = true;
                rectX0_ = mapHoverX_;
                rectY0_ = mapHoverY_;
                rectX1_ = rectX0_;
                rectY1_ = rectY0_;
                rectErase_ = vp.rightClicked || eraseMode;
            }
            if (rectDrag_ && vp.middleDown) {
                rectDrag_ = false;
            }
            if (rectDrag_ && !vp.down && !vp.rightDown) {
                rectDrag_ = false;
                const int ax = (std::min)(rectX0_, rectX1_);
                const int bx = (std::max)(rectX0_, rectX1_);
                const int ay = (std::min)(rectY0_, rectY1_);
                const int by = (std::max)(rectY0_, rectY1_);
                if (mapTool_ == 7) {
                    selValid_ = true;
                    selX0_ = ax;
                    selY0_ = ay;
                    selX1_ = bx;
                    selY1_ = by;
                }
                else {
                    const int mode = rectErase_ ? 1 : (mapTool_ == 6 ? 3 : 0);
                    for (int y = ay; y <= by; ++y) {
                        for (int x = ax; x <= bx; ++x) {
                            editCell(x, y, mode, mapProps_);
                        }
                    }
                }
            }
        }
        else {
            rectDrag_ = false;
            if (mapTool_ == 8) {
                if (vp.clicked && mapHoverValid_ && !clip_.empty()) {
                    for (const ClipCell& cc : clip_) {
                        editCell(mapHoverX_ + cc.dx, mapHoverY_ + cc.dy, 2, cc.p);
                    }
                }
            }
            else if (mapHoverValid_ && (mapPainting_ || mapErasing_)) {
                const bool erasing = mapErasing_ || (mapPainting_ && eraseMode);
                const int half = mapBrush_ / 2;
                for (int dy = 0; dy < mapBrush_; ++dy) {
                    for (int dx = 0; dx < mapBrush_; ++dx) {
                        editCell(mapHoverX_ - half + dx, mapHoverY_ - half + dy, erasing ? 1 : (mapTool_ == 6 ? 3 : 0), mapProps_);
                    }
                }
            }
        }
    }
    else {
        rectDrag_ = false;
    }

    const bool erase = !chip3D && (mapErasing_ || (mapPainting_ && (io.KeyCtrl || mapTool_ == 1)));
    const bool paint = !chip3D && mapPainting_ && !erase && mapTool_ == 0;
    const bool clickedOnce = vp.clicked && mapHoverValid_ && !io.KeyCtrl;

    if (mapHoverValid_) {
        const int cx = mapHoverX_;
        const int cy = mapHoverY_;
        const int half = mapBrush_ / 2;
        const Vector3 cell = CellWorld(static_cast<float>(cx), static_cast<float>(cy), 0.45f);

        if (paint || erase) {
            for (int dy = 0; dy < mapBrush_; ++dy) {
                for (int dx = 0; dx < mapBrush_; ++dx) {
                    const int x = cx - half + dx;
                    const int y = cy - half + dy;
                    if (x < 0 || x >= MapW() || y < 0 || y >= MapH()) {
                        continue;
                    }
                    const bool exists = FindBlockAt(x, y) >= 0;
                    if (paint && !exists) {
                        mapStroke_.push_back({ x, y, false });
                        pendingCommands_.push_back([this, x, y]() { SetBlockCell(x, y, true); });
                    }
                    else if (erase) {
                        if (exists) {
                            mapStroke_.push_back({ x, y, true });
                            pendingCommands_.push_back([this, x, y]() { SetBlockCell(x, y, false); });
                        }
                        // そのマスの敵・ゴールも消す
                        pendingCommands_.push_back([this, x, y]() { EraseRoleObjectsNear(static_cast<float>(x), static_cast<float>(y), 0.7f, true); });
                    }
                }
            }
        }
        else if (clickedOnce && mapTool_ == 2) {
            pendingCommands_.push_back([this, cell]() {
                const Vector2 pc = Planar(cell);
                EraseRoleObjectsNear(pc.x, pc.y, 0.5f, false); // 同じマスの敵は置き換える
                AddRoleObject(3, "teapot.obj", cell, enemySize_, { 0.85f, 0.22f, 0.22f, 1.0f }, false);
                });
        }
        else if (clickedOnce && mapTool_ == 3) {
            pendingCommands_.push_back([this, cell]() {
                const Vector2 pc = Planar(cell);
                EraseRoleObjectsNear(pc.x, pc.y, 0.5f, false);
                AddRoleObject(3, "teapot.obj", cell, 0.9f, { 0.55f, 0.30f, 0.75f, 1.0f }, true);
                });
        }
        else if (clickedOnce && mapTool_ == 4) {
            pendingCommands_.push_back([this, cell]() {
                // ゴールは1つだけ
                for (int i = static_cast<int>(objects_.size()) - 1; i >= 0; --i) {
                    if (objects_[i].role == 4) {
                        objects_.erase(objects_.begin() + i);
                    }
                }
                selection_ = {};
                const int n = AddRoleObject(4, kCubeName, { cell.x, cell.y + 0.5f, 0.0f }, 1.0f, { 1.0f, 0.85f, 0.2f, 1.0f }, false);
                if (n >= 0) {
                    objects_[n].object->GetObject3d().GetTransform().Scale = { 0.3f, 2.0f, 0.3f }; // 細長い旗竿
                    objects_[n].object->GetObject3d().GetTransform().Translate = { cell.x, cell.y + 0.5f, 0.0f };
                }
                });
        }
        else if (clickedOnce && mapTool_ == 5) {
            pendingCommands_.push_back([this, cell]() {
                // プレイヤー開始位置は1つだけ（既にあれば移動、なければ置く）
                int existing = -1;
                for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
                    if (objects_[i].role == 1) {
                        existing = i;
                        break;
                    }
                }
                if (existing >= 0) {
                    const Vector3 off = ModelCenterOffset(existing);
                    objects_[existing].object->GetObject3d().GetTransform().Translate = { cell.x - off.x, cell.y - off.y, cell.z - off.z };
                }
                else {
                    AddRoleObject(1, "suzanne.obj", cell, 0.9f, { 0.95f, 0.55f, 0.15f, 1.0f }, false);
                }
                });
        }
    }

    // アンドゥ：ボタンを押してから離すまでを1ストロークにまとめる（Ctrl+Z）
    if (!mapPainting_ && !mapErasing_ && !mapStroke_.empty()) {
        mapUndo_.push_back(mapStroke_);
        mapStroke_.clear();
        if (mapUndo_.size() > 200) {
            mapUndo_.erase(mapUndo_.begin());
        }
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        pendingCommands_.push_back([this]() { UndoMapEdit(); });
    }
#endif // USE_IMGUI
}

// グリッドと、マウスの下のマスの枠をビューポートに重ねる
void GameScene::DrawMapEditOverlay()
{
#ifdef USE_IMGUI
    if (!mapEdit_) {
        return;
    }
    const Matrix4x4& view = gameCamera_.GetViewMatrix();
    const Matrix4x4& proj = gameCamera_.GetProjectionMatrix();
    // マス座標（横, 奥）を、編集する平面の上のワールド座標にする
    auto onPlane = [&](float u, float v) {
        return Ground3D() ? Vector3{ u, 0.02f, v } : Vector3{ u, v, kEditPlaneZ };
    };
    auto line = [&](float x0, float y0, float x1, float y1) {
        Vector2 p[2];
        if (Picking::WorldToScreen(onPlane(x0, y0), view, proj, p[0]) && Picking::WorldToScreen(onPlane(x1, y1), view, proj, p[1])) {
            EditorLayout::DrawViewportOverlay(p, 2, false);
        }
        };

    // 見えている範囲のマス目だけ描く
    const float halfH = editDist_ * std::tan(kPfFovY * 0.5f);
    const float halfW = halfH * static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight;
    // 3Dマップは斜めから見ることがあるので、マップ全体のマス目を描く（見えない線は描かれない）
    const int x0 = Ground3D() ? 0 : (std::max)(0, static_cast<int>(std::floor(editCamX_ - halfW)));
    const int x1 = Ground3D() ? MapW() : (std::min)(MapW(), static_cast<int>(std::ceil(editCamX_ + halfW)) + 1);
    const int y0 = Ground3D() ? 0 : (std::max)(0, static_cast<int>(std::floor(editCamY_ - halfH)));
    const int y1 = Ground3D() ? MapH() : (std::min)(MapH(), static_cast<int>(std::ceil(editCamY_ + halfH)) + 1);
    for (int x = x0; x <= x1; ++x) {
        line(x - 0.5f, y0 - 0.5f, x - 0.5f, y1 - 0.5f);
    }
    for (int y = y0; y <= y1; ++y) {
        line(x0 - 0.5f, y - 0.5f, x1 - 0.5f, y - 0.5f);
    }

    // マウスの下のブラシ範囲
    if (mapHoverValid_) {
        const int half = mapBrush_ / 2;
        const float left = static_cast<float>(mapHoverX_ - half) - 0.5f;
        const float bottom = static_cast<float>(mapHoverY_ - half) - 0.5f;
        const float right = left + static_cast<float>(mapBrush_);
        const float top = bottom + static_cast<float>(mapBrush_);
        Vector2 corners[4];
        if (Picking::WorldToScreen(onPlane(left, top), view, proj, corners[0]) &&
            Picking::WorldToScreen(onPlane(right, top), view, proj, corners[1]) &&
            Picking::WorldToScreen(onPlane(right, bottom), view, proj, corners[2]) &&
            Picking::WorldToScreen(onPlane(left, bottom), view, proj, corners[3])) {
            EditorLayout::DrawViewportOverlay(corners, 4, true);
        }
    }

    // 3Dマップ：四角のドラッグ・範囲選択・貼り付けの位置
    if (Ground3D()) {
        auto rectOutline = [&](int ax, int ay, int bx, int by) {
            const float l = static_cast<float>((std::min)(ax, bx)) - 0.5f;
            const float r = static_cast<float>((std::max)(ax, bx)) + 0.5f;
            const float b = static_cast<float>((std::min)(ay, by)) - 0.5f;
            const float t = static_cast<float>((std::max)(ay, by)) + 0.5f;
            Vector2 corners[4];
            if (Picking::WorldToScreen(onPlane(l, t), view, proj, corners[0]) &&
                Picking::WorldToScreen(onPlane(r, t), view, proj, corners[1]) &&
                Picking::WorldToScreen(onPlane(r, b), view, proj, corners[2]) &&
                Picking::WorldToScreen(onPlane(l, b), view, proj, corners[3])) {
                EditorLayout::DrawViewportOverlay(corners, 4, true);
            }
        };
        if (rectDrag_) {
            rectOutline(rectX0_, rectY0_, rectX1_, rectY1_);
        }
        if (selValid_) {
            rectOutline(selX0_, selY0_, selX1_, selY1_);
        }
        if (mapTool_ == 8 && mapHoverValid_ && !clip_.empty()) {
            rectOutline(mapHoverX_, mapHoverY_, mapHoverX_ + clipW_ - 1, mapHoverY_ + clipH_ - 1);
        }
    }
#endif // USE_IMGUI
}

//==================================================
// マップCSV（200列 x 20行。0:空 1:ブロック P:開始 E:敵 S:盾の敵 G:ゴール）
//==================================================
bool GameScene::ExportStageCsv(const char* path)
{
    std::vector<std::string> grid(kMapHeight * kMapWidth, "0");
    auto put = [&](const Vector3& p, const char* token) {
        const int x = static_cast<int>(std::lround(p.x));
        const int y = static_cast<int>(std::lround(p.y));
        if (x < 0 || x >= kMapWidth || y < 0 || y >= kMapHeight) {
            return;
        }
        grid[(kMapHeight - 1 - y) * kMapWidth + x] = token; // CSVの上が高い y
        };
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const ObjectSlot& slot = objects_[i];
        if (slot.video || slot.role == 0) {
            continue;
        }
        const Vector3 off = ModelCenterOffset(i);
        const Vector3& t = slot.object->GetObject3d().GetTransform().Translate;
        const Vector3 center{ t.x + off.x, t.y + off.y, t.z + off.z };
        if (slot.role == 2) {
            put(center, "1");
        }
    }
    // ブロックの上に敵などが重ならないよう、ブロックのあとで書く
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const ObjectSlot& slot = objects_[i];
        if (slot.video || slot.role == 0 || slot.role == 2 || slot.role == 5) {
            continue;
        }
        const Vector3 off = ModelCenterOffset(i);
        const Vector3& t = slot.object->GetObject3d().GetTransform().Translate;
        const Vector3 center{ t.x + off.x, t.y + off.y, t.z + off.z };
        put(center, slot.role == 1 ? "P" : (slot.role == 3 ? (slot.shield ? "S" : "E") : "G"));
    }

    std::ofstream file(path);
    if (!file) {
        return false;
    }
    for (int row = 0; row < kMapHeight; ++row) {
        for (int col = 0; col < kMapWidth; ++col) {
            file << grid[row * kMapWidth + col] << (col + 1 < kMapWidth ? "," : "");
        }
        file << "\n";
    }
    return static_cast<bool>(file);
}

// CSVを読み込み、役割付きのオブジェクトを作り直す（役割なしの飾りは残す）
bool GameScene::ImportStageCsv(const char* path)
{
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    for (int i = static_cast<int>(objects_.size()) - 1; i >= 0; --i) {
        if (!objects_[i].video && objects_[i].role != 0 && objects_[i].role != 5) {
            objects_.erase(objects_.begin() + i);
        }
    }
    selection_ = {};
    mapUndo_.clear();
    mapStroke_.clear();

    std::string line;
    int row = 0;
    while (std::getline(file, line) && row < kMapHeight) {
        std::stringstream ss(line);
        std::string token;
        int col = 0;
        while (std::getline(ss, token, ',') && col < kMapWidth) {
            while (!token.empty() && (token.back() == '\r' || token.back() == ' ')) {
                token.pop_back();
            }
            const int x = col;
            const int y = kMapHeight - 1 - row;
            const Vector3 cell{ static_cast<float>(x), static_cast<float>(y), 0.0f };
            if (token == "1") {
                SetBlockCell(x, y, true);
            }
            else if (token == "E") {
                AddRoleObject(3, "teapot.obj", cell, 0.9f, { 0.85f, 0.22f, 0.22f, 1.0f }, false);
            }
            else if (token == "S") {
                AddRoleObject(3, "teapot.obj", cell, 0.9f, { 0.55f, 0.30f, 0.75f, 1.0f }, true);
            }
            else if (token == "P") {
                AddRoleObject(1, "suzanne.obj", cell, 0.9f, { 0.95f, 0.55f, 0.15f, 1.0f }, false);
            }
            else if (token == "G") {
                const int n = AddRoleObject(4, kCubeName, { cell.x, cell.y + 0.5f, 0.0f }, 1.0f, { 1.0f, 0.85f, 0.2f, 1.0f }, false);
                if (n >= 0) {
                    objects_[n].object->GetObject3d().GetTransform().Scale = { 0.3f, 2.0f, 0.3f };
                    objects_[n].object->GetObject3d().GetTransform().Translate = { cell.x, cell.y + 0.5f, 0.0f };
                }
            }
            ++col;
        }
        ++row;
    }
    return true;
}

//==================================================
// 見た目（テクスチャ・色・大きさ）
//==================================================
// テクスチャの白い1点だけを使う（UVを0倍にして、1点のUVへ平行移動する）
void GameScene::ApplyFlatUv(Object3d& object)
{
    Transform& uv = object.GetUvTransform();
    uv.Scale = { 0.0f, 0.0f, 1.0f };
    uv.Rotate = { 0.0f, 0.0f, 0.0f };
    uv.Translate = { cubeWhiteUv_.x, cubeWhiteUv_.y, 0.0f };
}

// ブロックの見た目を反映する（テクスチャを選んでいれば面ごとに貼り、なければ単色）
void GameScene::ApplyBlockStyle(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects_.size())) {
        return;
    }
    ObjectSlot& slot = objects_[objectIndex];
    Object3d& object = slot.object->GetObject3d();
    Vector4 color = slot.ownColor ? slot.ownTint : blockTint_;
    if (blockTexIndex_ > 0 && blockTexIndex_ - 1 < static_cast<int>(textureFileList_.size())) {
        const TextureManager::TextureHandle handle = ctx_->textureManager->Load(textureFileList_[blockTexIndex_ - 1]);
        object.SetExternalTexture(ctx_->textureManager->GetSrvHandleGPU(handle));
        Transform& uv = object.GetUvTransform();
        uv.Scale = { 1.0f, 1.0f, 1.0f };
        uv.Rotate = { 0.0f, 0.0f, 0.0f };
        uv.Translate = { 0.0f, 0.0f, 0.0f };
    }
    else {
        object.ClearExternalTexture();
        ApplyFlatUv(object);
        // 単色のときは、市松模様に少しだけ明るさを変える
        const Vector2 t = Planar(object.GetTransform().Translate);
        if ((static_cast<int>(std::lround(t.x)) + static_cast<int>(std::lround(t.y))) & 1) {
            color.x *= 0.93f;
            color.y *= 0.93f;
            color.z *= 0.93f;
        }
    }
    object.GetMaterial()->color = color;
    slot.konamiOriginalColor = color;
}

void GameScene::ApplyBlockStyleAll()
{
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        if (!objects_[i].video && objects_[i].role == 2) {
            ApplyBlockStyle(i);
        }
    }
}

// モデルの中心を動かさずに、最大辺が size になるよう拡縮する
void GameScene::FitObjectToSize(int objectIndex, float size)
{
    if (objectIndex < 0 || objectIndex >= static_cast<int>(objects_.size())) {
        return;
    }
    Object3d& object = objects_[objectIndex].object->GetObject3d();
    const Model* model = object.GetModel();
    if (!model) {
        return;
    }
    const Vector3& bmin = model->GetBoundsMin();
    const Vector3& bmax = model->GetBoundsMax();
    const float extent = (std::max)({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 0.0001f });
    const float s = size / extent;

    const Vector3 oldOffset = ModelCenterOffset(objectIndex);
    Transform& t = object.GetTransform();
    const Vector3 center{ t.Translate.x + oldOffset.x, t.Translate.y + oldOffset.y, t.Translate.z + oldOffset.z };
    t.Scale = { s, s, s };
    const Vector3 newOffset = ModelCenterOffset(objectIndex);
    t.Translate = { center.x - newOffset.x, center.y - newOffset.y, center.z - newOffset.z };
}
