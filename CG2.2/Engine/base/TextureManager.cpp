#include "TextureManager.h"
#include "../base/DirectXCommon.h"
#include "../base/SrvManager.h"

#include <cassert>

void TextureManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    assert(dxCommon);
    assert(srvManager);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
}

TextureManager::TextureHandle TextureManager::Load(const std::string& filePath)
{
    auto it = pathToHandle_.find(filePath);
    if (it != pathToHandle_.end()) {
        return it->second;
    }

    DirectX::ScratchImage mipImages = DirectXCommon::LoadTexture(filePath);

    TextureData data;
    data.filePath = filePath;
    data.metadata = mipImages.GetMetadata();
    data.resource = dxCommon_->CreateTextureResource(data.metadata);
    assert(data.resource);
    dxCommon_->UploadTextureData(data.resource.Get(), mipImages);

    data.srvIndex = srvManager_->Allocate();
    srvManager_->CreateSrvForTexture2D(
        data.srvIndex, data.resource.Get(), data.metadata.format, UINT(data.metadata.mipLevels));

    TextureHandle handle = static_cast<TextureHandle>(textures_.size());
    textures_.push_back(std::move(data));
    pathToHandle_.emplace(filePath, handle);
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(TextureHandle handle) const
{
    assert(handle < textures_.size());
    return srvManager_->GetGPUDescriptorHandle(textures_[handle].srvIndex);
}

const DirectX::TexMetadata& TextureManager::GetMetadata(TextureHandle handle) const
{
    assert(handle < textures_.size());
    return textures_[handle].metadata;
}
