#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <cstdint>

#include "../math/Matrix4x4.h"

class DirectXCommon;
class SrvManager;

/// <summary>
/// ポストエフェクト（シーンを描いたあとに、画面全体にかける処理）
///   SSAO（物の隅や接地点の暗がり）・ブルーム（明るい所のにじみ）・トーンマップ（HDRの色を画面の色に直す）・仕上げの色調整
/// シーンは HDR（R16G16B16A16_FLOAT）の画面に描かれ、このクラスが最後に通常の画面へ書き出す。
/// 使うシーンは Settings の enabled を true にする（既定は false ＝ 何もせずそのまま出す）。
/// 使い方（シーン側）:
///   PostProcess::Settings& s = ctx_->postProcess->GetSettings();  s.enabled = true;  s.bloom = true; ...
///   ctx_->postProcess->SetProjection(camera.GetProjectionMatrix(), nearZ, farZ);   // 毎フレーム（SSAO用）
/// </summary>
class PostProcess
{
public:
    struct Settings
    {
        bool enabled = false;      // false なら、何も加工せずそのまま出す
        // SSAO
        bool ssao = true;
        float aoRadius = 0.8f;     // 影響する半径（ワールド）
        float aoIntensity = 1.2f;  // 暗さの強さ
        float aoStrength = 0.85f;  // 色にかける濃さ（0〜1）
        // ブルーム
        bool bloom = true;
        float bloomThreshold = 0.85f;
        float bloomKnee = 0.4f;
        float bloomIntensity = 0.45f;
        // 色
        bool tonemap = true;       // ACES風のトーンマップ
        float exposure = 1.15f;
        float saturation = 1.05f;
        float contrast = 0.25f;    // 0〜1（Sカーブの強さ）
        float vignette = 0.25f;
        float warmth = 0.5f;       // 0〜1（夕方のあたたかい色味）
        // 天気（TimeOfDay が書く。シーンが直接書いてもよい）
        float rain = 0.0f;         // 雨の筋の量（0〜1）
        float dust = 0.0f;         // 砂ぼこりの量（0〜1）
        float wind = 0.3f;         // 風（雨の傾き・砂の流れ）
        float weatherTime = 0.0f;  // 経過秒（雨・砂の動き）
        float rainColor[3] = { 0.7f, 0.75f, 0.85f };
        float dustColor[3] = { 0.8f, 0.62f, 0.4f };
    };

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);
    void Finalize(); // デバイスの終了処理より前に呼ぶ

    Settings& GetSettings() { return settings_; }

    // SSAOが奥行きから位置を求めるために必要な、カメラの射影（毎フレーム、描画の前に）
    void SetProjection(const Matrix4x4& projection, float nearZ, float farZ);

    // HDRの画面と深度を読んで、finalRtv（通常の画面）へ書き出す。
    // finalRtv は呼び出し前に設定済みでなくてよい（中で設定する）。HDR画面・深度は「読める状態」になっていること
    void Execute(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE finalRtv,
        const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);

private:
    static constexpr int kBloomLevels = 5;

    struct Target
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t srvIndex = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        UINT width = 0;
        UINT height = 0;
    };
    void CreateTarget(Target& t, UINT width, UINT height, DXGI_FORMAT format, uint32_t rtvIndex);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipeline(const wchar_t* psEntry, DXGI_FORMAT format, bool additive);

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    Settings settings_;
    Matrix4x4 projection_ = Matrix4x4::MakeIdentity4x4();
    float nearZ_ = 0.1f;
    float farZ_ = 300.0f;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<IDxcBlob> vsBlob_;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoSsao_, psoBlur_, psoPrefilter_, psoDown_, psoUp_, psoComposite_;

    uint32_t hdrSrvIndex_ = 0;
    uint32_t depthSrvIndex_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE hdrSrv_{};
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrv_{};

    Target aoA_, aoB_;
    Target bloom_[kBloomLevels];
};
