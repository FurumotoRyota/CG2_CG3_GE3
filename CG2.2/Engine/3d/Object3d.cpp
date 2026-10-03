#include "Object3d.h"
#include "../3d/Model.h"
#include "../3d/ModelManager.h"
#include "../3d/Object3dCommon.h"
#include "../base/BasicPipeline.h"
#include "../base/DirectXCommon.h"

#include <cassert>

void Object3d::Initialize(Object3dCommon* common, const std::string& modelName)
{
    assert(common);
    common_ = common;

    DirectXCommon* dxCommon = common_->GetDxCommon();

    materialResource_ = dxCommon->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
    materialData_->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->lightingMode = 2;
    materialData_->alphaCutoff = 0.5f; // 透過PNG(フェンス等)の透明部分を描かない。半透明の縁を残したいときはInspectorで下げる
    materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();

    wvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = Matrix4x4::MakeIdentity4x4();
    wvpData_->World = Matrix4x4::MakeIdentity4x4();

    SetModel(modelName);
}

void Object3d::SetModel(const std::string& modelName)
{
    modelName_ = modelName;
    model_ = common_->GetModelManager()->Load(modelName);
}

void Object3d::Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix)
{
    Update(transform_, viewMatrix, projectionMatrix);
}

void Object3d::Update(const Transform& renderTransform, const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix)
{
    Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(renderTransform.Scale, renderTransform.Rotate, renderTransform.Translate);
    wvpData_->WVP = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
    wvpData_->World = worldMatrix;

    Matrix4x4 uvMatrix = Matrix4x4::MakeScaleMatrix(uvTransform_.Scale);
    uvMatrix = Multiply(uvMatrix, Matrix4x4::MakeRotateZMatrix(uvTransform_.Rotate.z));
    uvMatrix = Multiply(uvMatrix, Matrix4x4::MakeTranslateMatrix(uvTransform_.Translate));
    materialData_->uvTransform = uvMatrix;
}

void Object3d::Draw()
{
    common_->SetBlendMode(blendMode_, doubleSided_);

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(BasicPipeline::kRootMaterial, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(BasicPipeline::kRootTransform, wvpResource_->GetGPUVirtualAddress());
    model_->Draw(commandList);
}
