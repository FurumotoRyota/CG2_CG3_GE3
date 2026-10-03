#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>

#include "../base/BlendMode.h"
#include "../base/ShaderTypes.h"
#include "../base/TextureManager.h"
#include "../math/Transform.h"
#include "../math/Vector2.h"

class SpriteCommon;

/// <summary>
/// 2Dスプライト（ピクセル座標。左上が原点）
/// </summary>
class Sprite
{
public:
    void Initialize(SpriteCommon* common, const std::string& texturePath, const Vector2& size = { 640.0f, 360.0f });

    // テクスチャを差し替える（読み込み済みのものはキャッシュを再利用）
    void SetTexture(const std::string& texturePath);

    // WVP(正射影)とUV変換行列を更新する
    void Update();
    void Draw();

    Transform& GetTransform() { return transform_; }
    Transform& GetUvTransform() { return uvTransform_; }
    Material* GetMaterial() { return materialData_; }

    BlendMode GetBlendMode() const { return blendMode_; }
    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }

private:
    SpriteCommon* common_ = nullptr;
    TextureManager::TextureHandle textureHandle_ = 0;
    BlendMode blendMode_ = BlendMode::Normal; // 既定は通常のαブレンド（透過PNGがそのまま透ける）

    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform uvTransform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
};
