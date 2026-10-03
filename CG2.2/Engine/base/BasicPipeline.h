#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include "BlendMode.h"

class DirectXCommon;

/// <summary>
/// テクスチャ付き3D/2D描画用のルートシグネチャとPSO（Object3d.VS/PS.hlsl）
/// Object3dCommon と SpriteCommon が共通で使う
/// </summary>
class BasicPipeline
{
public:
    // ルートパラメータの番号
    static constexpr UINT kRootMaterial = 0;  // b0 (PS) Material
    static constexpr UINT kRootTransform = 1; // b0 (VS) TransformationMatrix
    static constexpr UINT kRootTexture = 2;   // t0 (PS) テクスチャ
    static constexpr UINT kRootLight = 3;     // b1 (PS) DirectionalLight

    // useDepth：深度テストを使うか（3Dはtrue、常に手前に重ねる2Dスプライトはfalse）
    // ブレンドモードごとのPSOを全て作る
    void Initialize(DirectXCommon* dxCommon, bool useDepth = true);

    // ルートシグネチャ・トポロジ・PSO(BlendMode::None)をコマンドリストに設定する
    void Bind(ID3D12GraphicsCommandList* commandList) const;

    // 描画するブレンドモードに切り替える（Bindの後、Drawの前に呼ぶ）
    // doubleSided：裏面も描く（カリングなし）。フェンスなど薄い板向け
    void SetBlendMode(ID3D12GraphicsCommandList* commandList, BlendMode mode, bool doubleSided = false) const;

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates_[kBlendModeCount * 2]; // 前半：裏面カリングあり、後半：両面
};
