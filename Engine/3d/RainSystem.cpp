#include "RainSystem.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "../base/DirectXCommon.h"
#include "../base/Logger.h"

// Rain.hlsl の cbuffer RainParams と同じ並び
struct RainConstants
{
    Matrix4x4 viewProj;
    float camPos[4];
    float focus[4];   // xyz=中心, w=経過秒
    float shape[4];   // 箱の広さ, 高さ, 落ちる速さ, 筋の長さ
    float wind[4];    // 風x, 風z, (未使用), 筋の太さ
    float color[4];   // rgb, 強さ
    float ripple[4];  // 地面の高さ, 波紋の半径, 周期, 範囲
};

void RainSystem::Initialize(DirectXCommon* dxCommon)
{
    assert(dxCommon);
    ID3D12Device* device = dxCommon->GetDevice();
    HRESULT hr;

    // ルートシグネチャ：定数バッファ(b0)が1つ。頂点・ピクセルの両方で読む
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    param.Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.pParameters = &param;
    rootDesc.NumParameters = 1;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Log(reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        assert(false);
    }
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // PSO：頂点バッファなし。深度テストは見るだけ（書かない）。加算ブレンド
    Microsoft::WRL::ComPtr<IDxcBlob> psBlob = dxCommon->CompileShader(L"Engine/Shaders/Rain.hlsl", L"ps_6_0", L"PS_Rain");
    auto makePso = [&](const wchar_t* vsEntry, Microsoft::WRL::ComPtr<ID3D12PipelineState>& out) {
        Microsoft::WRL::ComPtr<IDxcBlob> vs = dxCommon->CompileShader(L"Engine/Shaders/Rain.hlsl", L"vs_6_0", vsEntry);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSignature_.Get();
        desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
        desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
        desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        desc.DSVFormat = DirectXCommon::kDsvFormat;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = DirectXCommon::kSceneFormat;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleDesc.Count = 1;
        desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
        const HRESULT h = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(out.GetAddressOf()));
        assert(SUCCEEDED(h));
    };
    makePso(L"VS_Streak", streakPso_);
    makePso(L"VS_Ripple", ripplePso_);

    constantResource_ = dxCommon->CreateBufferResource(sizeof(RainConstants));
    constantResource_->Map(0, nullptr, reinterpret_cast<void**>(&constants_));
}

void RainSystem::Finalize()
{
    if (constantResource_ && constants_) {
        constantResource_->Unmap(0, nullptr);
    }
    constants_ = nullptr;
    constantResource_.Reset();
    streakPso_.Reset();
    ripplePso_.Reset();
    rootSignature_.Reset();
}

void RainSystem::Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& view, const Matrix4x4& projection,
    const Vector3& focus, const Params& p)
{
    if (!streakPso_ || p.intensity <= 0.005f) {
        return;
    }
    const float amount = std::clamp(p.intensity, 0.0f, 1.0f);

    const Matrix4x4 inv = Inverse(view);
    constants_->viewProj = view.Multiply(projection);
    constants_->camPos[0] = inv.m[3][0];
    constants_->camPos[1] = inv.m[3][1];
    constants_->camPos[2] = inv.m[3][2];
    constants_->camPos[3] = 0.0f;
    constants_->focus[0] = focus.x;
    constants_->focus[1] = focus.y;
    constants_->focus[2] = focus.z;
    constants_->focus[3] = p.time;
    constants_->shape[0] = p.boxSize;
    constants_->shape[1] = p.boxHeight;
    constants_->shape[2] = p.fallSpeed;
    constants_->shape[3] = p.streakLength;

    // 風：雨が流れる向きと速さ（横に最大 約6m/s）
    constexpr float kDeg = 3.14159265f / 180.0f;
    const float windSpeed = p.wind * 6.0f;
    constants_->wind[0] = std::sin(p.windAngleDeg * kDeg) * windSpeed;
    constants_->wind[1] = std::cos(p.windAngleDeg * kDeg) * windSpeed;
    constants_->wind[2] = 0.0f;
    constants_->wind[3] = p.streakWidth;

    constants_->color[0] = p.color.x;
    constants_->color[1] = p.color.y;
    constants_->color[2] = p.color.z;
    constants_->color[3] = 1.0f;

    constants_->ripple[0] = p.groundY;
    constants_->ripple[1] = p.rippleRadius;
    constants_->ripple[2] = p.ripplePeriod;
    constants_->ripple[3] = 30.0f; // 波紋を出す範囲（XZの一辺）

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(0, constantResource_->GetGPUVirtualAddress());

    // 波紋（地面）を先に、雨粒を後に
    commandList->SetPipelineState(ripplePso_.Get());
    commandList->DrawInstanced(6, static_cast<UINT>(kMaxRipples * amount), 0, 0);
    commandList->SetPipelineState(streakPso_.Get());
    commandList->DrawInstanced(6, static_cast<UINT>(kMaxStreaks * amount), 0, 0);
}
