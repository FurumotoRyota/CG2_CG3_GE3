#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

#include "../3d/ModelData.h"
#include "../math/Vector3.h"
#include "../base/TextureManager.h"

class DirectXCommon;

/// <summary>
/// 1つのモデルのGPUリソース（サブメッシュごとの頂点バッファ + 専用テクスチャ）
/// MultiMesh / MultiMaterial 対応のため、複数のサブメッシュを持つ
/// </summary>
class Model
{
public:
    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, const ModelData& modelData);

    // 全サブメッシュを描画する（マテリアル・WVPのCBVは呼び出し側で設定しておく）
    void Draw(ID3D12GraphicsCommandList* commandList) const;

    // モデル座標系での外接ボックス（Viewportでのクリック判定・選択枠の表示に使う）
    const Vector3& GetBoundsMin() const { return boundsMin_; }
    const Vector3& GetBoundsMax() const { return boundsMax_; }

private:
    struct SubMesh
    {
        size_t vertexCount = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        TextureManager::TextureHandle textureHandle = 0;
    };

    TextureManager* textureManager_ = nullptr;
    std::vector<SubMesh> subMeshes_;
    Vector3 boundsMin_{ -0.5f, -0.5f, -0.5f };
    Vector3 boundsMax_{ 0.5f, 0.5f, 0.5f };
};
