#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"

class DirectXCommon;
class SrvManager;

/// <summary>
/// ディレクショナルライトの影（シャドウマッピング）
/// 光の側から見た深度をテクスチャに描き、本描画で「光から隠れているか」を比べる
/// </summary>
class ShadowMap
{
public:
    static constexpr uint32_t kSize = 2048; // 影テクスチャの解像度

    static ShadowMap& Instance();

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize(); // デバイスの終了処理より前に呼ぶ

    // Object3d / Sprite 共通のルートシグネチャの影テクスチャ用テーブルを設定する（各PreDrawが呼ぶ）
    void Bind(ID3D12GraphicsCommandList* commandList) const;

    // 影の描画パス。Begin → 各Object3d::DrawShadow → End の順に呼ぶ（本描画の前に行う）
    void Begin(ID3D12GraphicsCommandList* commandList);
    void End(ID3D12GraphicsCommandList* commandList);

    // 光の向き dir・影を落とす範囲の中心 center・半径 range から、光から見たビュー射影行列を作る
    static Matrix4x4 ComputeLightViewProjection(const Vector3& dir, const Vector3& center, float range);

private:
    ShadowMap() = default;

    Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle_{};
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle_{};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};
