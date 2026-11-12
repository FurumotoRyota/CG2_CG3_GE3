#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include "../base/BasicPipeline.h"
#include "../base/ShaderTypes.h"
#include "../math/Matrix4x4.h"

class DirectXCommon;
class TextureManager;
class ModelManager;

/// <summary>
/// 3Dオブジェクト描画で共通のもの（パイプライン・平行光源・各マネージャーへの参照）
/// </summary>
class Object3dCommon
{
public:
    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, ModelManager* modelManager);

    // 3Dオブジェクトの描画前に1回呼ぶ（パイプラインと光源を設定する）
    void PreDraw();

    // 以降の描画のブレンドモードを切り替える（Object3d::Drawが呼ぶ）
    void SetBlendMode(BlendMode mode, bool doubleSided = false) const;

    DirectXCommon* GetDxCommon() const { return dxCommon_; }
    TextureManager* GetTextureManager() const { return textureManager_; }
    ModelManager* GetModelManager() const { return modelManager_; }

    // カメラ位置をビュー行列から求めて光源バッファへ書く（鏡面反射の視線ベクトル用。Object3d::Updateが呼ぶ）
    void SetCameraFromView(const Matrix4x4& viewMatrix);

    // ImGuiなどから光源を編集する用（ポイントライト・スポットライトも同じ構造体に入っている）
    DirectionalLight* GetDirectionalLight() const { return directionalLightData_; }

    // 影用：光から見たビュー射影行列（GameSceneが毎フレーム設定し、Object3d::UpdateがLightWVPに使う）
    void SetLightViewProjection(const Matrix4x4& m) { lightViewProjection_ = m; }
    const Matrix4x4& GetLightViewProjection() const { return lightViewProjection_; }

private:
    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    ModelManager* modelManager_ = nullptr;

    BasicPipeline pipeline_;
    Matrix4x4 lightViewProjection_ = Matrix4x4::MakeIdentity4x4();

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;
};
