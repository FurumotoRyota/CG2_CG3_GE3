#include "Sprite.h"
#include "../2d/SpriteCommon.h"
#include "../3d/VertexData.h"
#include "../base/BasicPipeline.h"
#include "../base/DirectXCommon.h"

#include <cassert>

void Sprite::Initialize(SpriteCommon* common, const std::string& texturePath, const Vector2& size)
{
    assert(common);
    common_ = common;
    DirectXCommon* dxCommon = common_->GetDxCommon();

    textureHandle_ = common_->GetTextureManager()->Load(texturePath);

    // 頂点バッファ（6頂点・非インデックス描画。左上原点のピクセル座標）
    vertexResource_ = dxCommon->CreateBufferResource(sizeof(VertexData) * 6);
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

    vertexData[0].position = { 0.0f, size.y, 0.0f, 1.0f };
    vertexData[0].texcoord = { 0.0f, 1.0f };
    vertexData[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexData[1].texcoord = { 0.0f, 0.0f };
    vertexData[2].position = { size.x, size.y, 0.0f, 1.0f };
    vertexData[2].texcoord = { 1.0f, 1.0f };
    vertexData[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexData[3].texcoord = { 0.0f, 0.0f };
    vertexData[4].position = { size.x, 0.0f, 0.0f, 1.0f };
    vertexData[4].texcoord = { 1.0f, 0.0f };
    vertexData[5].position = { size.x, size.y, 0.0f, 1.0f };
    vertexData[5].texcoord = { 1.0f, 1.0f };
    for (int i = 0; i < 6; ++i) {
        vertexData[i].normal = { 0.0f, 0.0f, -1.0f };
    }

    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->lightingMode = 0; // スプライトはライティングなし
    materialData_->alphaCutoff = 0.0f; // スプライトは半透明をそのまま使う（Inspectorで変更可）
    materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();

    wvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = Matrix4x4::MakeIdentity4x4();
    wvpData_->World = Matrix4x4::MakeIdentity4x4();
}

void Sprite::SetTexture(const std::string& texturePath)
{
    textureHandle_ = common_->GetTextureManager()->Load(texturePath);
}

void Sprite::Update()
{
    if (!wvpData_) {
        return; // Initialize前のスプライトは何もしない
    }
    Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(transform_.Scale, transform_.Rotate, transform_.Translate);
    Matrix4x4 viewMatrix = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 projectionMatrix = Matrix4x4::MakeOrthographicMatrix(
        0.0f, 0.0f, static_cast<float>(WinApp::kGameWidth), static_cast<float>(WinApp::kGameHeight), 0.1f, 100.0f);

    wvpData_->WVP = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
    wvpData_->World = worldMatrix;

    Matrix4x4 uvMatrix = Matrix4x4::MakeScaleMatrix(uvTransform_.Scale);
    uvMatrix = Multiply(uvMatrix, Matrix4x4::MakeRotateZMatrix(uvTransform_.Rotate.z));
    uvMatrix = Multiply(uvMatrix, Matrix4x4::MakeTranslateMatrix(uvTransform_.Translate));
    materialData_->uvTransform = uvMatrix;
}

void Sprite::Draw()
{
    common_->SetBlendMode(blendMode_);

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(BasicPipeline::kRootMaterial, materialResource_->GetGPUVirtualAddress());
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(BasicPipeline::kRootTransform, wvpResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(
        BasicPipeline::kRootTexture, common_->GetTextureManager()->GetSrvHandleGPU(textureHandle_));
    commandList->DrawInstanced(6, 1, 0, 0);
}
