#include "SpriteCommon.h"
#include "../base/DirectXCommon.h"

#include <cassert>

void SpriteCommon::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager)
{
    assert(dxCommon);
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;

    pipeline_.Initialize(dxCommon_, false); // スプライトは深度テストなし（描く順に重ねる）

    dummyLightResource_ = dxCommon_->CreateBufferResource(sizeof(DirectionalLight));
    DirectionalLight* lightData = nullptr;
    dummyLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&lightData));
    lightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    lightData->direction = { 0.0f, -1.0f, 0.0f };
    lightData->intensity = 1.0f;
}

void SpriteCommon::PreDraw()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    pipeline_.Bind(commandList);
    commandList->SetGraphicsRootConstantBufferView(
        BasicPipeline::kRootLight, dummyLightResource_->GetGPUVirtualAddress());
}

void SpriteCommon::SetBlendMode(BlendMode mode) const
{
    pipeline_.SetBlendMode(dxCommon_->GetCommandList(), mode);
}
