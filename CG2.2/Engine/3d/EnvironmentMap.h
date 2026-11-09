#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

#include "../math/Matrix4x4.h"

class DirectXCommon;
class SrvManager;
struct SkyConstants;

/// <summary>
/// 環境キューブマップ（スカイボックス描画 と PBRの反射用）
/// resources/skybox.dds（キューブマップ）があればそれを使い、無ければ空をプログラムで生成する
/// </summary>
class EnvironmentMap
{
public:
    static EnvironmentMap& Instance();

    // 読み込み/生成してSRVを作る（SrvManager初期化後に1回呼ぶ）
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    // GPUリソースを解放する（デバイスの終了処理より前に呼ぶ。呼ばないと終了時にリーク扱いになる）
    void Finalize();

    // Object3d / Sprite 共通のルートシグネチャのキューブマップ用テーブルを設定する（各PreDrawが呼ぶ）
    void Bind(ID3D12GraphicsCommandList* commandList) const;

    // 背景としてスカイボックスを描く。3Dオブジェクトより先に呼ぶこと
    void DrawSkybox(ID3D12GraphicsCommandList* commandList, const Matrix4x4& view, const Matrix4x4& projection);

    bool skyboxVisible = true; // falseなら背景は描かない（反射は使われる）

private:
    EnvironmentMap() = default;

    void CreateSkyboxPipeline(DirectXCommon* dxCommon);

    Microsoft::WRL::ComPtr<ID3D12Resource> cubeResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle_{};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> skyRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> skyConstantResource_;
    SkyConstants* skyConstants_ = nullptr;
};
