#include "GeometryShaderScene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <random>

#include "base/DirectXCommon.h"
#include "base/TextureManager.h"
#include "base/StringUtility.h"
#include "base/WinApp.h"
#include "3d/EnvironmentMap.h"
#include "3d/Model.h"
#include "3d/ModelLoader.h"
#include "3d/ModelManager.h"
#include "editor/EditorLayout.h"
#include "input/Input.h"

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#endif // USE_IMGUI

namespace
{
    // 拡張子（小文字）が一致するか
    bool HasExt(const std::filesystem::path& path, std::initializer_list<const wchar_t*> exts)
    {
        std::wstring ext = path.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        for (const wchar_t* e : exts) {
            if (ext == e) {
                return true;
            }
        }
        return false;
    }
}

void GeometryShaderScene::Initialize()
{
    auto debugCamera = std::make_unique<DebugCamera>();
    debugCamera->Initialize(static_cast<float>(WinApp::kGameWidth) / WinApp::kGameHeight);
    debugCamera_ = debugCamera.get();
    camera_ = std::move(debugCamera);

    pipeline_.Initialize(ctx_->dxCommon);

    paramsResource_ = ctx_->dxCommon->CreateBufferResource(sizeof(GeoParams));
    paramsResource_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));
    std::memset(paramsData_, 0, sizeof(GeoParams));

    ScanResources();
    BuildPoints();
    BuildTestTriangle();
    time_ = 0.0f;
}

void GeometryShaderScene::Finalize()
{
    pipeline_.Finalize();
    pointResource_.Reset();
    triangleResource_.Reset();
    paramsResource_.Reset();
    paramsData_ = nullptr;
    debugCamera_ = nullptr;
    camera_.reset();
}

// resourcesのモデルと画像を調べて、選択肢にする
void GeometryShaderScene::ScanResources()
{
    modelNames_ = { "suzanne.obj" };
    textureNames_ = { "resources/uvChecker.png" };

    std::vector<std::string> models;
    std::vector<std::string> textures;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("resources", ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        const std::string name = StringUtility::ConvertString(path.filename().wstring());
        if (HasExt(path, { L".obj", L".gltf", L".glb", L".fbx" })) {
            models.push_back(name);
        }
        else if (HasExt(path, { L".png", L".jpg", L".jpeg", L".bmp" })) {
            textures.push_back("resources/" + name);
        }
    }
    std::sort(models.begin(), models.end());
    std::sort(textures.begin(), textures.end());
    modelNames_.push_back(ModelLoader::kSphereTag);
    for (const auto& n : models) {
        if (std::find(modelNames_.begin(), modelNames_.end(), n) == modelNames_.end()) {
            modelNames_.push_back(n);
        }
    }
    for (const auto& n : textures) {
        if (std::find(textureNames_.begin(), textureNames_.end(), n) == textureNames_.end()) {
            textureNames_.push_back(n);
        }
    }
    modelNamesCStr_.clear();
    for (const auto& n : modelNames_) {
        modelNamesCStr_.push_back(n.c_str());
    }
    textureNamesCStr_.clear();
    for (const auto& n : textureNames_) {
        textureNamesCStr_.push_back(n.c_str());
    }

    // 点の既定テクスチャ（あれば monsterBall）
    const auto it = std::find(textureNames_.begin(), textureNames_.end(), "resources/monsterBall.png");
    textureIndex_ = (it != textureNames_.end()) ? static_cast<int>(it - textureNames_.begin()) : 0;
}

// 点を球の中にランダムに散らして、頂点バッファを作る
void GeometryShaderScene::BuildPoints()
{
    pointCount_ = (std::max)(1, (std::min)(pointCount_, 5000));
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    std::vector<GeometryPointVertex> vertices(pointCount_);
    for (GeometryPointVertex& v : vertices) {
        Vector3 p{};
        do {
            p = { dist(rng), dist(rng), dist(rng) };
        } while (p.x * p.x + p.y * p.y + p.z * p.z > 1.0f);
        v.position = { p.x * 4.0f, p.y * 4.0f, p.z * 4.0f };
        v.size = 0.15f + unit(rng) * 0.25f;
        v.color = { 0.5f + unit(rng) * 0.5f, 0.5f + unit(rng) * 0.5f, 0.5f + unit(rng) * 0.5f, 0.9f };
        v.seed = unit(rng);
    }

    const size_t sizeInBytes = sizeof(GeometryPointVertex) * vertices.size();
    pointResource_ = ctx_->dxCommon->CreateBufferResource(sizeInBytes);
    void* mapped = nullptr;
    pointResource_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, vertices.data(), sizeInBytes);
    pointResource_->Unmap(0, nullptr);

    pointView_.BufferLocation = pointResource_->GetGPUVirtualAddress();
    pointView_.SizeInBytes = static_cast<UINT>(sizeInBytes);
    pointView_.StrideInBytes = sizeof(GeometryPointVertex);
    builtPointCount_ = pointCount_;
}

// 三角形1枚の頂点バッファ（Model と同じ頂点の並び：位置4・UV2・法線3）
void GeometryShaderScene::BuildTestTriangle()
{
    const TestVertex vertices[3] = {
        { { -1.5f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
        { { 0.0f, 1.5f, 0.0f, 1.0f }, { 0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { { 1.5f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
    };
    triangleResource_ = ctx_->dxCommon->CreateBufferResource(sizeof(vertices));
    void* mapped = nullptr;
    triangleResource_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, vertices, sizeof(vertices));
    triangleResource_->Unmap(0, nullptr);

    triangleView_.BufferLocation = triangleResource_->GetGPUVirtualAddress();
    triangleView_.SizeInBytes = sizeof(vertices);
    triangleView_.StrideInBytes = sizeof(TestVertex);
}

void GeometryShaderScene::UpdateCameraControl()
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
    debugCamera_->SetMouseLook(middleActive_ ? vp.mouseDelta.x : 0.0f, middleActive_ ? vp.mouseDelta.y : 0.0f);
    debugCamera_->SetZoom(vp.wheel);
    debugCamera_->SetPan(0.0f, 0.0f);
#else
    debugCamera_->SetControlEnabled(true);
#endif // USE_IMGUI
}

void GeometryShaderScene::Update()
{
    time_ += 1.0f / 60.0f;
    UpdateCameraControl();
    camera_->Update(*ctx_->input);

    // 点の数が変わったら作り直す（GPUは前フレームで使い終わっている）
    if (pointCount_ != builtPointCount_) {
        BuildPoints();
    }

    // モデル・テクスチャの読み込み（アップロードのコマンドは描画の前にここで積んでおく）
    currentModel_ = ctx_->modelManager->Load(modelNames_[modelIndex_]);
    const int texIndex = (demo_ == Demo::Samples) ? sampleTextureIndex_ : textureIndex_;
    currentTexture_ = ctx_->textureManager->Load(textureNames_[texIndex]);
}

void GeometryShaderScene::Draw()
{
    ID3D12GraphicsCommandList* commandList = ctx_->dxCommon->GetCommandList();
    const Matrix4x4& view = camera_->GetViewMatrix();
    const Matrix4x4& projection = camera_->GetProjectionMatrix();

    // 背景（スカイボックス）
    EnvironmentMap::Instance().DrawSkybox(commandList, view, projection);

    // 定数バッファ（モデル加工 / 点から図形で共通）
    const Matrix4x4 cameraWorld = Inverse(view);
    paramsData_->viewProj = Multiply(view, projection);
    const float rotateY = autoRotate_ ? time_ * 0.5f : 0.0f;
    paramsData_->world = (demo_ != Demo::PointsToShapes)
        ? Matrix4x4::MakeAffineMatrix({ modelScale_, modelScale_, modelScale_ }, { 0.0f, rotateY, 0.0f }, { 0.0f, 0.0f, 0.0f })
        : Matrix4x4::MakeIdentity4x4();
    paramsData_->lightDir = { -0.4f, -1.0f, 0.6f, 0.0f };
    paramsData_->camRight = { cameraWorld.m[0][0], cameraWorld.m[0][1], cameraWorld.m[0][2], 0.0f };
    paramsData_->camUp = { cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2], 0.0f };
    paramsData_->mode = static_cast<float>(processMode_);
    paramsData_->amount = amount_;
    paramsData_->time = time_;
    paramsData_->animate = animate_ ? 1.0f : 0.0f;
    paramsData_->size = pointSize_;
    paramsData_->useTexture = (shape_ == 1) ? 1.0f : 0.0f;
    paramsData_->shape = static_cast<float>(shape_);

    const D3D12_GPU_VIRTUAL_ADDRESS paramsAddress = paramsResource_->GetGPUVirtualAddress();
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = ctx_->textureManager->GetSrvHandleGPU(currentTexture_);

    if (demo_ == Demo::Samples) {
        // サンプルでは size=UV倍率 / amount=ずらす距離 として使う
        const bool solid = sampleIndex_ <= 4; // 点・線は単色
        paramsData_->color = { sampleColor_[0], sampleColor_[1], sampleColor_[2], sampleColor_[3] };
        paramsData_->size = sampleUvScale_;
        paramsData_->amount = sampleOffset_;
        paramsData_->useTexture = solid ? 0.0f : 1.0f;
        pipeline_.BindSample(commandList, paramsAddress, sampleIndex_);
        commandList->SetGraphicsRootDescriptorTable(GeometryPipeline::kRootTexture, textureSrv);
        if (sampleOnModel_) {
            if (currentModel_) {
                currentModel_->Draw(commandList, &textureSrv);
            }
        }
        else {
            commandList->IASetVertexBuffers(0, 1, &triangleView_);
            commandList->DrawInstanced(3, 1, 0, 0); // 三角形1枚だけ描く
        }
    }
    else if (demo_ == Demo::ModelProcessing) {
        if (!currentModel_) {
            return;
        }
        paramsData_->color = { modelColor_[0], modelColor_[1], modelColor_[2], modelColor_[3] };
        pipeline_.BindModel(commandList, paramsAddress);
        currentModel_->Draw(commandList, useModelTexture_ ? nullptr : &textureSrv);
    }
    else {
        paramsData_->color = { pointColor_[0], pointColor_[1], pointColor_[2], pointColor_[3] };
        pipeline_.BindPoints(commandList, paramsAddress);
        commandList->SetGraphicsRootDescriptorTable(GeometryPipeline::kRootTexture, textureSrv);
        commandList->IASetVertexBuffers(0, 1, &pointView_);
        commandList->DrawInstanced(static_cast<UINT>(builtPointCount_), 1, 0, 0);
    }
}

void GeometryShaderScene::DrawImGui()
{
#ifdef USE_IMGUI
    EditorLayout::Begin("Hierarchy", EditorLayout::Panel::Hierarchy);
    ImGui::Text("シーン: ジオメトリシェーダー");
    ImGui::Separator();
    if (ImGui::Selectable("モデル加工 (80_00 / 80_01)", demo_ == Demo::ModelProcessing)) {
        demo_ = Demo::ModelProcessing;
    }
    if (ImGui::Selectable("点から図形を作る (80_02 / 80_03)", demo_ == Demo::PointsToShapes)) {
        demo_ = Demo::PointsToShapes;
    }
    if (ImGui::Selectable("サンプル：点・線・複製・UV (80 番外編)", demo_ == Demo::Samples)) {
        demo_ = Demo::Samples;
    }
    ImGui::Separator();
    ImGui::TextWrapped("ジオメトリシェーダーは頂点シェーダーとピクセルシェーダーの間で、点・線・三角形の単位で形を作り変える。");
    ImGui::Text("ビューポートを右ボタン長押し: WASDで移動");
    ImGui::Text("ホイールクリック+ドラッグ: カメラの向き");
    ImGui::Text("ホイール: ズーム");
    ImGui::End();

    EditorLayout::Begin("Inspector", EditorLayout::Panel::Inspector);
    ImGui::Checkbox("時間でアニメーション", &animate_);

    if (demo_ == Demo::Samples) {
        ImGui::Text("スライドのサンプル（入力は三角形、出力の種類を切り替える）");
        ImGui::Separator();
        const char* samples[] = {
            "0: 複数頂点の出力  PointStream / maxvertexcount(3)",
            "1: Append(1頂点)  PointStream / maxvertexcount(1)",
            "2: Lineの出力  LineStream / maxvertexcount(2)",
            "3: LineStrip (最初の頂点に戻る)  LineStream / maxvertexcount(4)",
            "4: RestartStrip (辺ごとに区切る)  LineStream / maxvertexcount(6)",
            "5: プリミティブの複製 (ずらしてUV拡大)  TriangleStream / maxvertexcount(6)",
            "6: 数値の加工 (UVタイリング)  TriangleStream / maxvertexcount(3)"
        };
        ImGui::Combo("サンプル", &sampleIndex_, samples, IM_ARRAYSIZE(samples));
        ImGui::Checkbox("モデルに適用する（OFFなら三角形1枚）", &sampleOnModel_);
        if (sampleOnModel_) {
            ImGui::Combo("モデル", &modelIndex_, modelNamesCStr_.data(), static_cast<int>(modelNamesCStr_.size()));
            ImGui::SliderFloat("モデルの大きさ", &modelScale_, 0.1f, 5.0f);
            ImGui::Checkbox("モデルを回す", &autoRotate_);
        }
        if (sampleIndex_ == 5) {
            ImGui::SliderFloat("ずらす距離", &sampleOffset_, 0.0f, 6.0f);
        }
        if (sampleIndex_ >= 5) {
            ImGui::SliderFloat("UVの倍率", &sampleUvScale_, 0.5f, 8.0f);
            ImGui::Combo("テクスチャ", &sampleTextureIndex_, textureNamesCStr_.data(), static_cast<int>(textureNamesCStr_.size()));
        }
        ImGui::ColorEdit4("色", sampleColor_);
        ImGui::Separator();
        ImGui::TextDisabled("点は1ピクセルの大きさで描かれる（Direct3Dの仕様）");
        ImGui::TextDisabled("PSOのトポロジ種類はGSの出力に合わせている");
    }
    else if (demo_ == Demo::ModelProcessing) {
        ImGui::Text("三角形を受け取り、加工して出力する");
        ImGui::Separator();
        ImGui::Combo("モデル", &modelIndex_, modelNamesCStr_.data(), static_cast<int>(modelNamesCStr_.size()));
        const char* modes[] = {
            "0: そのまま (GSを通すだけ。何も起こらない)",
            "1: 爆発 (面の向きに沿って飛ばす)",
            "2: 縮小 (重心に向かって縮める)",
            "3: 回転 (三角形ごとに法線まわりに回す)"
        };
        ImGui::Combo("加工", &processMode_, modes, IM_ARRAYSIZE(modes));
        ImGui::SliderFloat("量", &amount_, 0.0f, processMode_ == 2 ? 1.0f : 2.0f);
        ImGui::SliderFloat("モデルの大きさ", &modelScale_, 0.1f, 5.0f);
        ImGui::Checkbox("モデルを回す", &autoRotate_);
        ImGui::ColorEdit4("色", modelColor_);
        ImGui::Checkbox("モデル自身のテクスチャを使う", &useModelTexture_);
        if (!useModelTexture_) {
            ImGui::Combo("テクスチャ", &textureIndex_, textureNamesCStr_.data(), static_cast<int>(textureNamesCStr_.size()));
        }
        ImGui::Separator();
        ImGui::TextDisabled("入力: triangle / 出力: TriangleStream / maxvertexcount(3)");
    }
    else {
        ImGui::Text("点を受け取り、図形を作って出力する");
        ImGui::Separator();
        const char* shapes[] = { "三角形 (80_02)", "テクスチャ付き四角形 (80_03)" };
        ImGui::Combo("作る図形", &shape_, shapes, IM_ARRAYSIZE(shapes));
        ImGui::SliderInt("点の数", &pointCount_, 1, 5000);
        ImGui::SliderFloat("大きさ", &pointSize_, 0.1f, 4.0f);
        ImGui::ColorEdit4("色", pointColor_);
        if (shape_ == 1) {
            ImGui::Combo("テクスチャ", &textureIndex_, textureNamesCStr_.data(), static_cast<int>(textureNamesCStr_.size()));
        }
        ImGui::Separator();
        ImGui::TextDisabled("入力: point / 出力: TriangleStream / maxvertexcount(4)");
        ImGui::TextDisabled("四角形は常にカメラを向く（ビルボード）");
    }
    ImGui::End();
#endif // USE_IMGUI
}
