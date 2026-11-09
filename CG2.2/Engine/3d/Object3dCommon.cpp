#include "Object3dCommon.h"
#include "../base/DirectXCommon.h"
#include "EnvironmentMap.h"
#include "ShadowMap.h"

#include <cassert>
#include <cmath>
#include <cstring>

void Object3dCommon::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, ModelManager* modelManager)
{
    assert(dxCommon);
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    modelManager_ = modelManager;

    pipeline_.Initialize(dxCommon_);

    directionalLightResource_ = dxCommon_->CreateBufferResource(sizeof(DirectionalLight));
    directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));
    std::memset(directionalLightData_, 0, sizeof(DirectionalLight));
    directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData_->intensity = 1.0f;
    directionalLightData_->cameraPosition = { 0.0f, 0.0f, 0.0f };
    directionalLightData_->shadowBias = 0.002f;
    directionalLightData_->shadowStrength = 0.7f;
    directionalLightData_->shadowEnabled = 1;
    directionalLightData_->lightCount = 0; // 配置したライトはGameSceneが毎フレーム書き込む
}

void Object3dCommon::PreDraw()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    pipeline_.Bind(commandList);
    EnvironmentMap::Instance().Bind(commandList);
    ShadowMap::Instance().Bind(commandList);
    commandList->SetGraphicsRootConstantBufferView(
        BasicPipeline::kRootLight, directionalLightResource_->GetGPUVirtualAddress());
}

void Object3dCommon::SetCameraFromView(const Matrix4x4& viewMatrix)
{
    // ビュー行列 = [R t]（行ベクトル規約）。カメラ位置 p は p*R + t = 0 を満たすので p = -t*R^T
    const float tx = viewMatrix.m[3][0];
    const float ty = viewMatrix.m[3][1];
    const float tz = viewMatrix.m[3][2];
    directionalLightData_->cameraPosition = {
        -(tx * viewMatrix.m[0][0] + ty * viewMatrix.m[0][1] + tz * viewMatrix.m[0][2]),
        -(tx * viewMatrix.m[1][0] + ty * viewMatrix.m[1][1] + tz * viewMatrix.m[1][2]),
        -(tx * viewMatrix.m[2][0] + ty * viewMatrix.m[2][1] + tz * viewMatrix.m[2][2])
    };
}

void Object3dCommon::SetBlendMode(BlendMode mode, bool doubleSided) const
{
    pipeline_.SetBlendMode(dxCommon_->GetCommandList(), mode, doubleSided);
}
