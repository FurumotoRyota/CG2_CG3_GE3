#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "DirectXTex/DirectXTex.h"

class DirectXCommon;
class SrvManager;

/// <summary>
/// テクスチャの読み込みと管理
/// 同じファイルパスは1度しか読み込まず、2回目以降は同じハンドルを返す
/// </summary>
class TextureManager
{
public:
    using TextureHandle = uint32_t;

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    // テクスチャを読み込んでハンドルを返す（読み込み済みならキャッシュを返す）
    // アップロードのコマンドはDirectXCommonのコマンドリストに積まれる
    TextureHandle Load(const std::string& filePath);

    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(TextureHandle handle) const;
    const DirectX::TexMetadata& GetMetadata(TextureHandle handle) const;

private:
    struct TextureData
    {
        std::string filePath;
        DirectX::TexMetadata metadata{};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t srvIndex = 0;
    };

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    std::vector<TextureData> textures_;
    std::unordered_map<std::string, TextureHandle> pathToHandle_;
};
