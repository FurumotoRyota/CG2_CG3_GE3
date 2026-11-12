#include "PostProcess.h"

#include <algorithm>
#include <cassert>

#include <dxcapi.h>

#include "../base/DirectXCommon.h"
#include "../base/SrvManager.h"
#include "../base/WinApp.h"

namespace
{
    // HLSLの cbuffer Params と同じ並び（float4 が11個 ＝ 44個の値。ルート定数で渡す）
    struct Constants
    {
        float proj[4];
        float texel[4];
        float ao[4];
        float bloom[4];
        float grade[4];
        float mode[4];
        float warm[4];
        float invSize[4];
        float weather[4];  // x=雨 y=砂 z=経過秒 w=風
        float rainColor[4];
        float dustColor[4];
    };
    static_assert(sizeof(Constants) == 44 * sizeof(float), "Constants のサイズが cbuffer と合わない");

    void Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        cmd->ResourceBarrier(1, &b);
    }
}

void PostProcess::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    assert(dxCommon && srvManager);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    ID3D12Device* device = dxCommon->GetDevice();
    HRESULT hr;

    // HDRの画面と深度を読むためのSRV
    hdrSrvIndex_ = srvManager->Allocate();
    dxCommon->CreateSceneSrv(srvManager->GetCPUDescriptorHandle(hdrSrvIndex_));
    hdrSrv_ = srvManager->GetGPUDescriptorHandle(hdrSrvIndex_);
    depthSrvIndex_ = srvManager->Allocate();
    dxCommon->CreateDepthSrv(srvManager->GetCPUDescriptorHandle(depthSrvIndex_));
    depthSrv_ = srvManager->GetGPUDescriptorHandle(depthSrvIndex_);

    // 途中経過用のRTV置き場（AO×2 + ブルームの段数）
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = 2 + kBloomLevels;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(rtvHeap_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    const UINT w = static_cast<UINT>(WinApp::kGameWidth);
    const UINT h = static_cast<UINT>(WinApp::kGameHeight);
    // AOは半分の解像度で計算する
    CreateTarget(aoA_, (std::max)(1u, w / 2), (std::max)(1u, h / 2), DXGI_FORMAT_R8_UNORM, 0);
    CreateTarget(aoB_, (std::max)(1u, w / 2), (std::max)(1u, h / 2), DXGI_FORMAT_R8_UNORM, 1);
    // ブルームは 1/2, 1/4, 1/8, 1/16, 1/32
    for (int i = 0; i < kBloomLevels; ++i) {
        const UINT div = 2u << i;
        CreateTarget(bloom_[i], (std::max)(1u, w / div), (std::max)(1u, h / div), DXGI_FORMAT_R16G16B16A16_FLOAT, 2 + i);
    }

    // ルートシグネチャ：b0=定数(32個) / t0〜t3=SRVテーブル / s0=線形 s1=点
    D3D12_DESCRIPTOR_RANGE ranges[4]{};
    D3D12_ROOT_PARAMETER params[5]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 44;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    for (int i = 0; i < 4; ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = static_cast<UINT>(i);
        ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        params[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
        params[1 + i].DescriptorTable.pDescriptorRanges = &ranges[i];
        params[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (int i = 0; i < 2; ++i) {
        samplers[i].Filter = (i == 0) ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = static_cast<UINT>(i);
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.pParameters = params;
    rootDesc.NumParameters = 5;
    rootDesc.pStaticSamplers = samplers;
    rootDesc.NumStaticSamplers = 2;
    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    vsBlob_ = dxCommon->CompileShader(L"Engine/Shaders/PostProcess.hlsl", L"vs_6_0", L"VSMain");
    psoSsao_ = CreatePipeline(L"PS_SSAO", DXGI_FORMAT_R8_UNORM, false);
    psoBlur_ = CreatePipeline(L"PS_AO_BLUR", DXGI_FORMAT_R8_UNORM, false);
    psoPrefilter_ = CreatePipeline(L"PS_BLOOM_PREFILTER", DXGI_FORMAT_R16G16B16A16_FLOAT, false);
    psoDown_ = CreatePipeline(L"PS_BLOOM_DOWN", DXGI_FORMAT_R16G16B16A16_FLOAT, false);
    psoUp_ = CreatePipeline(L"PS_BLOOM_UP", DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    psoComposite_ = CreatePipeline(L"PS_COMPOSITE", DirectXCommon::kRtvFormat, false);
}

void PostProcess::Finalize()
{
    psoSsao_.Reset();
    psoBlur_.Reset();
    psoPrefilter_.Reset();
    psoDown_.Reset();
    psoUp_.Reset();
    psoComposite_.Reset();
    vsBlob_.Reset();
    rootSignature_.Reset();
    aoA_.resource.Reset();
    aoB_.resource.Reset();
    for (Target& t : bloom_) {
        t.resource.Reset();
    }
    rtvHeap_.Reset();
}

void PostProcess::SetProjection(const Matrix4x4& projection, float nearZ, float farZ)
{
    projection_ = projection;
    nearZ_ = nearZ;
    farZ_ = farZ;
}

void PostProcess::CreateTarget(Target& t, UINT width, UINT height, DXGI_FORMAT format, uint32_t rtvIndex)
{
    ID3D12Device* device = dxCommon_->GetDevice();
    t.width = width;
    t.height = height;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clear{};
    clear.Format = format;

    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(t.resource.GetAddressOf()));
    assert(SUCCEEDED(hr));

    const UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    t.rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    t.rtv.ptr += static_cast<SIZE_T>(rtvIndex) * rtvSize;
    device->CreateRenderTargetView(t.resource.Get(), nullptr, t.rtv);

    t.srvIndex = srvManager_->Allocate();
    srvManager_->CreateSrvForTexture2D(t.srvIndex, t.resource.Get(), format, 1);
    t.srv = srvManager_->GetGPUDescriptorHandle(t.srvIndex);
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PostProcess::CreatePipeline(const wchar_t* psEntry, DXGI_FORMAT format, bool additive)
{
    Microsoft::WRL::ComPtr<IDxcBlob> ps = dxCommon_->CompileShader(L"Engine/Shaders/PostProcess.hlsl", L"ps_6_0", psEntry);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = { vsBlob_->GetBufferPointer(), vsBlob_->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (additive) {
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ONE;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
        rt.DestBlendAlpha = D3D12_BLEND_ONE;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = format;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso.GetAddressOf()));
    assert(SUCCEEDED(hr));
    return pso;
}

void PostProcess::Execute(ID3D12GraphicsCommandList* cmd, D3D12_CPU_DESCRIPTOR_HANDLE finalRtv,
    const D3D12_VIEWPORT& finalViewport, const D3D12_RECT& finalScissor)
{
    const Settings& s = settings_;
    dxCommon_->GpuMark("scene_rest"); // ここまでが、雨のあと〜ポスト処理の前
    const bool active = s.enabled;
    const bool useAo = active && s.ssao;
    const bool useBloom = active && s.bloom;
    const float fullW = static_cast<float>(WinApp::kGameWidth);
    const float fullH = static_cast<float>(WinApp::kGameHeight);

    ID3D12DescriptorHeap* heaps[] = { dxCommon_->GetSrvDescriptorHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(rootSignature_.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 共通の定数
    Constants c{};
    c.proj[0] = projection_.m[0][0];
    c.proj[1] = projection_.m[1][1];
    c.proj[2] = nearZ_;
    c.proj[3] = farZ_;
    c.ao[0] = s.aoRadius;
    c.ao[1] = s.aoIntensity;
    c.ao[2] = 0.03f;
    c.ao[3] = s.aoStrength;
    c.bloom[0] = s.bloomThreshold;
    c.bloom[1] = s.bloomKnee;
    c.bloom[2] = s.bloomIntensity;
    c.grade[0] = s.exposure;
    c.grade[1] = s.saturation;
    c.grade[2] = s.contrast;
    c.grade[3] = s.vignette;
    c.mode[0] = s.tonemap ? 1.0f : 0.0f;
    c.mode[1] = active ? 1.0f : 0.0f;
    c.mode[2] = useAo ? 1.0f : 0.0f;
    c.mode[3] = useBloom ? 1.0f : 0.0f;
    const float warm = std::clamp(s.warmth, 0.0f, 1.0f);
    c.warm[0] = 1.0f + 0.06f * warm;
    c.warm[1] = 1.0f;
    c.warm[2] = 1.0f - 0.10f * warm;
    c.invSize[0] = 1.0f / fullW;
    c.invSize[1] = 1.0f / fullH;
    c.weather[0] = std::clamp(s.rain, 0.0f, 1.0f);
    c.weather[1] = std::clamp(s.dust, 0.0f, 1.0f);
    c.weather[2] = s.weatherTime;
    c.weather[3] = s.wind;
    for (int n = 0; n < 3; ++n) {
        c.rainColor[n] = s.rainColor[n];
        c.dustColor[n] = s.dustColor[n];
    }

    // 1パス：dst に描く（nullptr なら最終の画面）。t0〜t3 に読むテクスチャを渡す
    auto pass = [&](ID3D12PipelineState* pso, Target* dst, D3D12_GPU_DESCRIPTOR_HANDLE t0, D3D12_GPU_DESCRIPTOR_HANDLE t1,
        D3D12_GPU_DESCRIPTOR_HANDLE t2, D3D12_GPU_DESCRIPTOR_HANDLE t3, const Constants& k) {
        if (dst) {
            Transition(cmd, dst->resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->OMSetRenderTargets(1, &dst->rtv, FALSE, nullptr);
            D3D12_VIEWPORT vp{ 0.0f, 0.0f, static_cast<float>(dst->width), static_cast<float>(dst->height), 0.0f, 1.0f };
            D3D12_RECT sc{ 0, 0, static_cast<LONG>(dst->width), static_cast<LONG>(dst->height) };
            cmd->RSSetViewports(1, &vp);
            cmd->RSSetScissorRects(1, &sc);
        }
        else {
            cmd->OMSetRenderTargets(1, &finalRtv, FALSE, nullptr);
            cmd->RSSetViewports(1, &finalViewport);
            cmd->RSSetScissorRects(1, &finalScissor);
        }
        cmd->SetPipelineState(pso);
        cmd->SetGraphicsRoot32BitConstants(0, 44, &k, 0);
        cmd->SetGraphicsRootDescriptorTable(1, t0);
        cmd->SetGraphicsRootDescriptorTable(2, t1);
        cmd->SetGraphicsRootDescriptorTable(3, t2);
        cmd->SetGraphicsRootDescriptorTable(4, t3);
        cmd->DrawInstanced(3, 1, 0, 0);
        if (dst) {
            Transition(cmd, dst->resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
    };

    // --- SSAO：計算 → 縦横にぼかす（結果は aoA_） ---
    if (useAo) {
        Constants k = c;
        pass(psoSsao_.Get(), &aoA_, hdrSrv_, depthSrv_, hdrSrv_, hdrSrv_, k);
        k.texel[0] = 1.0f / aoA_.width;
        k.texel[1] = 1.0f / aoA_.height;
        k.texel[2] = 1.0f;
        k.texel[3] = 0.0f;
        pass(psoBlur_.Get(), &aoB_, aoA_.srv, depthSrv_, hdrSrv_, hdrSrv_, k);
        k.texel[2] = 0.0f;
        k.texel[3] = 1.0f;
        pass(psoBlur_.Get(), &aoA_, aoB_.srv, depthSrv_, hdrSrv_, hdrSrv_, k);
        dxCommon_->GpuMark("ssao");
    }

    // --- ブルーム：明るい所を取り出す → 小さくしていく → 大きくしながら足し合わせる（結果は bloom_[0]） ---
    if (useBloom) {
        Constants k = c;
        k.texel[0] = 1.0f / fullW;
        k.texel[1] = 1.0f / fullH;
        pass(psoPrefilter_.Get(), &bloom_[0], hdrSrv_, hdrSrv_, hdrSrv_, hdrSrv_, k);
        for (int i = 1; i < kBloomLevels; ++i) {
            k.texel[0] = 1.0f / bloom_[i - 1].width;
            k.texel[1] = 1.0f / bloom_[i - 1].height;
            pass(psoDown_.Get(), &bloom_[i], bloom_[i - 1].srv, hdrSrv_, hdrSrv_, hdrSrv_, k);
        }
        for (int i = kBloomLevels - 1; i >= 1; --i) {
            k.texel[0] = 1.0f / bloom_[i].width;
            k.texel[1] = 1.0f / bloom_[i].height;
            pass(psoUp_.Get(), &bloom_[i - 1], bloom_[i].srv, hdrSrv_, hdrSrv_, hdrSrv_, k);
        }
        dxCommon_->GpuMark("bloom");
    }

    // --- 仕上げ：色・AO・ブルームを合わせて、最終の画面へ ---
    pass(psoComposite_.Get(), nullptr, hdrSrv_, depthSrv_, useAo ? aoA_.srv : hdrSrv_, useBloom ? bloom_[0].srv : hdrSrv_, c);
    dxCommon_->GpuMark("composite");
}
