#include "SrvManager.h"
#include "../base/DirectXCommon.h"

#include <cassert>

void SrvManager::Initialize(DirectXCommon* dxCommon)
{
    assert(dxCommon);
    dxCommon_ = dxCommon;
    nextIndex_ = kReservedCount;
}

uint32_t SrvManager::Allocate()
{
    assert(nextIndex_ < DirectXCommon::kSrvHeapSize); // ヒープが足りない場合は kSrvHeapSize を増やす
    return nextIndex_++;
}

D3D12_CPU_DESCRIPTOR_HANDLE SrvManager::GetCPUDescriptorHandle(uint32_t index) const
{
    return dxCommon_->GetSRVCPUDescriptorHandle(index);
}

D3D12_GPU_DESCRIPTOR_HANDLE SrvManager::GetGPUDescriptorHandle(uint32_t index) const
{
    return dxCommon_->GetSRVGPUDescriptorHandle(index);
}

void SrvManager::CreateSrvForTexture2D(uint32_t index, ID3D12Resource* resource, DXGI_FORMAT format, UINT mipLevels)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = mipLevels;

    dxCommon_->GetDevice()->CreateShaderResourceView(resource, &srvDesc, GetCPUDescriptorHandle(index));
}

void SrvManager::CreateSrvForStructuredBuffer(uint32_t index, ID3D12Resource* resource, UINT numElements, UINT structureByteStride)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = numElements;
    srvDesc.Buffer.StructureByteStride = structureByteStride;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    dxCommon_->GetDevice()->CreateShaderResourceView(resource, &srvDesc, GetCPUDescriptorHandle(index));
}
