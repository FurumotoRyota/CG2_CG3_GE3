#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <vector>

class DirectXCommon;

/// <summary>
/// SRVディスクリプタヒープのスロット管理
/// 0番はImGuiのフォント用に予約してあるので、Allocate() は1番から順に払い出す
/// </summary>
class SrvManager
{
public:
    static constexpr uint32_t kReservedCount = 1;

    void Initialize(DirectXCommon* dxCommon);

    // 空きスロットの番号を1つ払い出す
    uint32_t Allocate();

    // 使い終わったスロットを返す（次のAllocateで再利用される）。GPUが使い終わってから呼ぶこと
    void Free(uint32_t index);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index) const;

    void CreateSrvForTexture2D(uint32_t index, ID3D12Resource* resource, DXGI_FORMAT format, UINT mipLevels);
    void CreateSrvForStructuredBuffer(uint32_t index, ID3D12Resource* resource, UINT numElements, UINT structureByteStride);

private:
    DirectXCommon* dxCommon_ = nullptr;
    uint32_t nextIndex_ = kReservedCount;
    std::vector<uint32_t> freeList_;
};
