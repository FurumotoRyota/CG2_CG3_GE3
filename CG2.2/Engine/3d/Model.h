#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <string>
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
    // overrideSrv: use this texture for every sub mesh instead of the model's own textures (e.g. a video frame)
    // skinnedViews: 骨を動かした頂点バッファ（Object3d が持つ SkinInstance のもの）。指定するとモデルの頂点バッファの代わりに使う
    void Draw(ID3D12GraphicsCommandList* commandList, const D3D12_GPU_DESCRIPTOR_HANDLE* overrideSrv = nullptr,
        const D3D12_VERTEX_BUFFER_VIEW* skinnedViews = nullptr) const;

    // 形だけ描く（影の描画パス用。テクスチャは設定しない）
    void DrawGeometry(ID3D12GraphicsCommandList* commandList, const D3D12_VERTEX_BUFFER_VIEW* skinnedViews = nullptr) const;

    // 形の元データ（まとめ描き用に、頂点をCPUにも残してある。骨つきモデルは空）
    struct CpuMesh
    {
        std::vector<VertexData> vertices;
        std::string texturePath;
    };
    const std::vector<CpuMesh>& GetCpuMeshes() const { return cpuMeshes_; }

    // 骨つきモデルか（FBX などで骨とアニメーションを持つ）
    bool IsSkinned() const { return skin_ != nullptr; }
    const SkinData* GetSkin() const { return skin_.get(); }
    size_t GetSubMeshCount() const { return subMeshes_.size(); }

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
    std::shared_ptr<const SkinData> skin_;
    std::vector<SubMesh> subMeshes_;
    std::vector<CpuMesh> cpuMeshes_;
    Vector3 boundsMin_{ -0.5f, -0.5f, -0.5f };
    Vector3 boundsMax_{ 0.5f, 0.5f, 0.5f };
};
