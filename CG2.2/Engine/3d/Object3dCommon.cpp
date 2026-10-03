#include "Object3dCommon.h"
#include "../base/DirectXCommon.h"

#include <cassert>

void Object3dCommon::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, ModelManager* modelManager)
{
    assert(dxCommon);
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    modelManager_ = modelManager;

    pipeline_.Initialize(dxCommon_);

    directionalLightResource_ = dxCommon_->CreateBufferResource(sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData_->intensity = 1.0f;
}

void Object3dCommon::PreDraw()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    pipeline_.Bind(commandList);
    commandList->SetGraphicsRootConstantBufferView(
        BasicPipeline::kRootLight, directionalLightResource_->GetGPUVirtualAddress());
}

void Object3dCommon::SetBlendMode(BlendMode mode, bool doubleSided) const
{
    pipeline_.SetBlendMode(dxCommon_->GetCommandList(), mode, doubleSided);
}
