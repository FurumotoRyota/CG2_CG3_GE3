#include "Model.h"
#include "../base/BasicPipeline.h"
#include "../base/DirectXCommon.h"

#include <algorithm>
#include <cassert>
#include <cstring>

void Model::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, const ModelData& modelData)
{
    assert(dxCommon);
    assert(textureManager);
    textureManager_ = textureManager;

    subMeshes_.clear();
    subMeshes_.resize(modelData.meshes.size());

    // 全頂点から外接ボックスを求める
    bool hasVertex = false;
    Vector3 minPos{ 0.0f, 0.0f, 0.0f };
    Vector3 maxPos{ 0.0f, 0.0f, 0.0f };
    for (const MeshData& mesh : modelData.meshes) {
        for (const VertexData& v : mesh.vertices) {
            if (!hasVertex) {
                minPos = { v.position.x, v.position.y, v.position.z };
                maxPos = minPos;
                hasVertex = true;
                continue;
            }
            minPos = { (std::min)(minPos.x, v.position.x), (std::min)(minPos.y, v.position.y), (std::min)(minPos.z, v.position.z) };
            maxPos = { (std::max)(maxPos.x, v.position.x), (std::max)(maxPos.y, v.position.y), (std::max)(maxPos.z, v.position.z) };
        }
    }
    if (hasVertex) {
        boundsMin_ = minPos;
        boundsMax_ = maxPos;
    }

    for (size_t i = 0; i < modelData.meshes.size(); ++i)
    {
        const MeshData& mesh = modelData.meshes[i];
        SubMesh& subMesh = subMeshes_[i];
        subMesh.vertexCount = mesh.vertices.size();

        // 頂点0のメッシュはGPUリソースを作らずスキップする（0バイトバッファ作成はクラッシュの原因になるため）
        if (mesh.vertices.empty()) {
            continue;
        }

        const size_t sizeInBytes = sizeof(VertexData) * mesh.vertices.size();
        subMesh.vertexResource = dxCommon->CreateBufferResource(sizeInBytes);

        VertexData* vertexData = nullptr;
        subMesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
        std::memcpy(vertexData, mesh.vertices.data(), sizeInBytes);
        subMesh.vertexResource->Unmap(0, nullptr);

        subMesh.vertexBufferView.BufferLocation = subMesh.vertexResource->GetGPUVirtualAddress();
        subMesh.vertexBufferView.SizeInBytes = UINT(sizeInBytes);
        subMesh.vertexBufferView.StrideInBytes = sizeof(VertexData);

        std::string texturePath = "resources/uvChecker.png";
        auto it = modelData.materials.find(mesh.materialName);
        if (it != modelData.materials.end() && !it->second.textureFilePath.empty()) {
            texturePath = it->second.textureFilePath;
        }
        subMesh.textureHandle = textureManager_->Load(texturePath);
    }
}

void Model::Draw(ID3D12GraphicsCommandList* commandList) const
{
    for (const SubMesh& subMesh : subMeshes_) {
        if (subMesh.vertexCount == 0) {
            continue; // 空メッシュ(GPUリソース未生成)はスキップ
        }
        commandList->SetGraphicsRootDescriptorTable(
            BasicPipeline::kRootTexture, textureManager_->GetSrvHandleGPU(subMesh.textureHandle));
        commandList->IASetVertexBuffers(0, 1, &subMesh.vertexBufferView);
        commandList->DrawInstanced(UINT(subMesh.vertexCount), 1, 0, 0);
    }
}
