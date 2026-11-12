#include "Object3d.h"
#include "../3d/Model.h"
#include "../3d/ModelManager.h"
#include "../3d/Object3dCommon.h"
#include "../3d/SkinInstance.h"
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
    materialData_->shininess = 32.0f;
    materialData_->specularMode = 0; // 鏡面反射なし（インスペクターで Phong / Blinn-Phong を選ぶ）
    materialData_->roughness = 0.4f;
    materialData_->metallic = 0.0f;
    materialData_->receiveShadow = 1.0f;
    materialData_->alphaCutoff = 0.5f; // 透過PNG(フェンス等)の透明部分を描かない。半透明の縁を残したいときはInspectorで下げる
    materialData_->uvTransform = Matrix4x4::MakeIdentity4x4();

    wvpResource_ = dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&wvpData_));
    wvpData_->WVP = Matrix4x4::MakeIdentity4x4();
    wvpData_->World = Matrix4x4::MakeIdentity4x4();
    wvpData_->WorldInverseTranspose = Matrix4x4::MakeIdentity4x4();
    wvpData_->LightWVP = Matrix4x4::MakeIdentity4x4();

    SetModel(modelName);
}

void Object3d::SetModel(const std::string& modelName)
{
    modelName_ = modelName;
    model_ = common_->GetModelManager()->Load(modelName);

    // 骨つきモデルなら、この個体専用の頂点バッファ（アニメーションの結果を書く）を持つ
    skin_.reset();
    if (model_ && model_->IsSkinned()) {
        skin_ = std::make_shared<SkinInstance>();
        skin_->Initialize(common_->GetDxCommon(), model_->GetSkin());
    }
}

bool Object3d::PlayAnimation(const std::string& clip, bool loop, float blend, float speed, bool restart)
{
    return skin_ ? skin_->Play(clip, loop, blend, speed, restart) : false;
}

void Object3d::UpdateSkin(float dt)
{
    if (skin_) {
        skin_->Update(dt);
    }
}

bool Object3d::HasAnimation(const std::string& clip) const
{
    return skin_ && skin_->HasClip(clip);
}

bool Object3d::IsAnimationFinished() const
{
    return skin_ && skin_->IsFinished();
}

bool Object3d::GetBonePointWorld(const std::string& boneName, const Vector3& restPoint, Vector3& out) const
{
    Vector3 local;
    if (!skin_ || !skin_->SkinPoint(boneName, restPoint, local)) {
        return false;
    }
    const Matrix4x4 world = Matrix4x4::MakeAffineMatrix(transform_.Scale, transform_.Rotate, transform_.Translate);
    out = Matrix4x4::Transform(local, world);
    return true;
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
    wvpData_->LightWVP = Multiply(worldMatrix, common_->GetLightViewProjection()); // 影用
    wvpData_->WorldInverseTranspose = Transpose(Inverse(worldMatrix)); // 非均一スケールでも法線が歪まない
    common_->SetCameraFromView(viewMatrix);                           // 鏡面反射用のカメラ位置

    Matrix4x4 uvMatrix = Matrix4x4::MakeScaleMatrix(uvTransform_.Scale);
    uvMatrix = Multiply(uvMatrix, Matrix4x4::MakeRotateZMatrix(uvTransform_.Rotate.z));
    uvMatrix = Multiply(uvMatrix, Matrix4x4::MakeTranslateMatrix(uvTransform_.Translate));
    materialData_->uvTransform = uvMatrix;
}

void Object3d::DrawShadow(ID3D12GraphicsCommandList* commandList)
{
    if (!castShadow_ || !model_) {
        return;
    }
    commandList->SetGraphicsRootConstantBufferView(0, wvpResource_->GetGPUVirtualAddress()); // 影用ルートシグネチャのb0
    model_->DrawGeometry(commandList, skin_ ? skin_->Views() : nullptr);
}

void Object3d::Draw()
{
    common_->SetBlendMode(blendMode_, doubleSided_);

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(BasicPipeline::kRootMaterial, materialResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(BasicPipeline::kRootTransform, wvpResource_->GetGPUVirtualAddress());
    model_->Draw(commandList, useExternalTexture_ ? &externalSrv_ : nullptr, skin_ ? skin_->Views() : nullptr);
}
