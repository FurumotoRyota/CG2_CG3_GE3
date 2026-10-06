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
struct VertexData;

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

    // Show only a part of the texture (rectangle in texture pixels: left-top and size)
    // The sprite size on screen is not changed; call SetSize() to match it if needed
    void SetTextureRect(const Vector2& leftTop, const Vector2& rectSize);
    // Show the whole texture again
    void ResetTextureRect();

    // Size of the sprite on screen (pixels)
    void SetSize(const Vector2& size);
    const Vector2& GetSize() const { return size_; }

    BlendMode GetBlendMode() const { return blendMode_; }
    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }

private:
    // Write positions and texcoords of the 6 vertices from size_ and the uv rectangle
    void UpdateVertices();

    SpriteCommon* common_ = nullptr;
    TextureManager::TextureHandle textureHandle_ = 0;
    BlendMode blendMode_ = BlendMode::Normal; // 既定は通常のαブレンド（透過PNGがそのまま透ける）

    Transform transform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform uvTransform_{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    VertexData* vertexData_ = nullptr;

    Vector2 size_{ 640.0f, 360.0f };       // size on screen (pixels)
    Vector2 uvLeftTop_{ 0.0f, 0.0f };      // visible texture area (normalized 0..1)
    Vector2 uvSize_{ 1.0f, 1.0f };

    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* wvpData_ = nullptr;
};
