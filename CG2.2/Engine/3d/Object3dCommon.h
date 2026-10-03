#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include "../base/BasicPipeline.h"
#include "../base/ShaderTypes.h"

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

    // ImGuiなどから光源を編集する用
    DirectionalLight* GetDirectionalLight() const { return directionalLightData_; }

private:
    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    ModelManager* modelManager_ = nullptr;

    BasicPipeline pipeline_;

    Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
    DirectionalLight* directionalLightData_ = nullptr;
};
