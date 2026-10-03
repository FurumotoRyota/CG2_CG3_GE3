#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include "../base/BasicPipeline.h"
#include "../base/ShaderTypes.h"

class DirectXCommon;
class TextureManager;

/// <summary>
/// スプライト描画で共通のもの（パイプライン・各マネージャーへの参照）
/// </summary>
class SpriteCommon
{
public:
    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager);

    // スプライトの描画前に1回呼ぶ
    void PreDraw();

    // 以降の描画のブレンドモードを切り替える（Sprite::Drawが呼ぶ）
    void SetBlendMode(BlendMode mode) const;

    DirectXCommon* GetDxCommon() const { return dxCommon_; }
    TextureManager* GetTextureManager() const { return textureManager_; }

private:
    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    BasicPipeline pipeline_;

    // ルートシグネチャ上はライトのCBVが必須なので、ライティングなし(lightingMode=0)の
    // スプライト用にダミーの光源を持たせておく
    Microsoft::WRL::ComPtr<ID3D12Resource> dummyLightResource_;
};
