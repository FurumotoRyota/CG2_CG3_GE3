#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

class DirectXCommon;

// ジオメトリシェーダーのデモ用の定数バッファ（Geometry.hlsli の GeoParams と同じ並び）
struct GeoParams
{
    Matrix4x4 viewProj;
    Matrix4x4 world;
    Vector4 color;
    Vector4 lightDir;
    Vector4 camRight;
    Vector4 camUp;

    float mode;       // モデル加工の種類（0:そのまま 1:爆発 2:縮小 3:回転）
    float amount;     // 加工の量
    float time;       // 経過時間[秒]
    float animate;    // 1なら時間でアニメーションする

    float size;       // 点から作る図形の大きさ
    float useTexture; // 1ならテクスチャを使う
    float shape;      // 点から作る図形（0:三角形 1:テクスチャ付き四角形）
    float pad;
};

// 点から図形を作るときの頂点（Geometry Point VS の入力と同じ並び）
struct GeometryPointVertex
{
    Vector3 position;
    float size;
    Vector4 color;
    float seed;
    float padding[3];
};

/// <summary>
/// ジオメトリシェーダーを使うパイプライン2種類
///  ・モデル加工：三角形を受け取って、形を変えて出力する（80_00 / 80_01）
///  ・点から図形：点を受け取って、三角形 / テクスチャ付き四角形を作る（80_02 / 80_03）
/// ルートシグネチャ：[0]=b0(定数・全シェーダー) [1]=予備 [2]=t0(テクスチャ。Model::Drawと同じ番号)
/// </summary>
class GeometryPipeline
{
public:
    static constexpr UINT kRootParams = 0;
    static constexpr UINT kRootSpare = 1;
    static constexpr UINT kRootTexture = 2;

    void Initialize(DirectXCommon* dxCommon);
    void Finalize(); // PSO・ルートシグネチャを解放する（デバイスより先に呼ぶ）

    // ルートシグネチャ・PSO・トポロジを設定し、定数バッファを結び付ける
    void BindModel(ID3D12GraphicsCommandList* commandList, D3D12_GPU_VIRTUAL_ADDRESS paramsAddress) const;
    void BindPoints(ID3D12GraphicsCommandList* commandList, D3D12_GPU_VIRTUAL_ADDRESS paramsAddress) const;
    // スライドのサンプル（点・線・複製など）。入力は三角形リストのまま、GSの出力の種類だけが変わる
    void BindSample(ID3D12GraphicsCommandList* commandList, D3D12_GPU_VIRTUAL_ADDRESS paramsAddress, int sampleIndex) const;

    static constexpr int kSampleCount = 7;

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> modelPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pointPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> samplePso_[kSampleCount];
};
