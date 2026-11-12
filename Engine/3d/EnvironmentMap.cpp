#include "EnvironmentMap.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>

#include <DirectXPackedVector.h>

#include "../base/DirectXCommon.h"
#include "../base/Logger.h"
#include "../base/SrvManager.h"
#include "../math/Vector3.h"

// スカイボックス用の定数バッファ（Skybox.VS.hlsl と対応）
struct SkyConstants
{
    Matrix4x4 invView;
    float projScaleX;
    float projScaleY;
    float mode;      // 0:キューブマップ 1:時間帯の空
    float padding;
    float horizon[4];
    float zenith[4];
    float ground[4];
    float toSun[4];
    float discColor[4];
    float glowColor[4];
    float cloud[4];      // x=雲の量 y=星 z,w=雲の流れ
    float cloudLit[4];   // rgb=雲の明るい側 w=経過秒
    float cloudShade[4];
    float moonDir[4];
    float moonColor[4];
};

namespace
{
    float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    float SmoothStep(float a, float b, float x)
    {
        const float t = Clamp01((x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    // 方向 dir（正規化済み）の空の色。上が青空、下が地面、太陽つき（HDR: 1を超える）
    void SkyColor(float dx, float dy, float dz, float out[3])
    {
        const float horizon[3] = { 0.78f, 0.86f, 1.00f };
        const float zenith[3] = { 0.16f, 0.36f, 0.85f };
        const float ground[3] = { 0.30f, 0.27f, 0.24f };

        float c[3];
        if (dy >= 0.0f) {
            const float t = std::pow(dy, 0.5f);
            for (int i = 0; i < 3; ++i) c[i] = Lerp(horizon[i], zenith[i], t);
        }
        else {
            const float t = std::pow(-dy, 0.4f);
            for (int i = 0; i < 3; ++i) c[i] = Lerp(horizon[i] * 0.85f, ground[i], t);
        }

        // 太陽（円盤 + 周りのにじみ）
        const Vector3 sun = Vector3{ 0.5f, 0.65f, 0.45f }.Normalize();
        const float d = dx * sun.x + dy * sun.y + dz * sun.z;
        if (dy > -0.02f) {
            const float disc = SmoothStep(0.9993f, 0.9996f, d) * 30.0f;
            const float glow = std::pow((std::max)(d, 0.0f), 200.0f) * 1.5f;
            c[0] += (disc + glow) * 1.00f;
            c[1] += (disc + glow) * 0.95f;
            c[2] += (disc + glow) * 0.85f;
        }
        for (int i = 0; i < 3; ++i) out[i] = c[i];
    }

    // 面 face の (u,v)∈[-1,1]（uは右、vは下）に対応する方向（D3Dのキューブマップの向き）
    void FaceDirection(int face, float u, float v, float out[3])
    {
        switch (face) {
        case 0: out[0] = 1.0f;  out[1] = -v; out[2] = -u; break; // +X
        case 1: out[0] = -1.0f; out[1] = -v; out[2] = u; break;  // -X
        case 2: out[0] = u;     out[1] = 1.0f; out[2] = v; break;  // +Y
        case 3: out[0] = u;     out[1] = -1.0f; out[2] = -v; break; // -Y
        case 4: out[0] = u;     out[1] = -v; out[2] = 1.0f; break; // +Z
        default: out[0] = -u;   out[1] = -v; out[2] = -1.0f; break; // -Z
        }
        const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
        out[0] /= len; out[1] /= len; out[2] /= len;
    }

    // 空をプログラムで描いたキューブマップ（R16G16B16A16_FLOAT）を作る
    DirectX::ScratchImage GenerateSkyCube(uint32_t size)
    {
        DirectX::ScratchImage cube;
        HRESULT hr = cube.InitializeCube(DXGI_FORMAT_R16G16B16A16_FLOAT, size, size, 1, 1);
        assert(SUCCEEDED(hr));

        for (int face = 0; face < 6; ++face) {
            const DirectX::Image* image = cube.GetImage(0, face, 0);
            for (uint32_t y = 0; y < size; ++y) {
                DirectX::PackedVector::HALF* row = reinterpret_cast<DirectX::PackedVector::HALF*>(image->pixels + image->rowPitch * y);
                for (uint32_t x = 0; x < size; ++x) {
                    const float u = (x + 0.5f) / size * 2.0f - 1.0f;
                    const float v = (y + 0.5f) / size * 2.0f - 1.0f;
                    float dir[3];
                    float color[3];
                    FaceDirection(face, u, v, dir);
                    SkyColor(dir[0], dir[1], dir[2], color);
                    row[x * 4 + 0] = DirectX::PackedVector::XMConvertFloatToHalf(color[0]);
                    row[x * 4 + 1] = DirectX::PackedVector::XMConvertFloatToHalf(color[1]);
                    row[x * 4 + 2] = DirectX::PackedVector::XMConvertFloatToHalf(color[2]);
                    row[x * 4 + 3] = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
                }
            }
        }
        return cube;
    }
}

EnvironmentMap& EnvironmentMap::Instance()
{
    static EnvironmentMap instance;
    return instance;
}

void EnvironmentMap::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    assert(dxCommon);
    assert(srvManager);

    // resources/skybox.dds（キューブマップ）があれば優先して使う
    DirectX::ScratchImage source;
    bool loaded = false;
    const wchar_t* kDdsPath = L"resources/skybox.dds";
    if (std::filesystem::exists(kDdsPath)) {
        DirectX::TexMetadata meta{};
        if (SUCCEEDED(DirectX::LoadFromDDSFile(kDdsPath, DirectX::DDS_FLAGS_NONE, &meta, source)) && meta.IsCubemap()) {
            loaded = true;
            Logger::Log("EnvironmentMap: loaded resources/skybox.dds");
        }
        else {
            Logger::Log("EnvironmentMap: resources/skybox.dds is not a cubemap. Using generated sky");
        }
    }
    if (!loaded) {
        source = GenerateSkyCube(256);
    }

    // ミップマップ（粗い反射はぼやけたミップを読む）
    DirectX::ScratchImage mipImages;
    if (source.GetMetadata().mipLevels == 1 && !DirectX::IsCompressed(source.GetMetadata().format)) {
        const HRESULT hr = DirectX::GenerateMipMaps(
            source.GetImages(), source.GetImageCount(), source.GetMetadata(),
            DirectX::TEX_FILTER_DEFAULT, 0, mipImages);
        assert(SUCCEEDED(hr));
    }
    else {
        mipImages = std::move(source);
    }

    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    cubeResource_ = dxCommon->CreateTextureResource(metadata);
    assert(cubeResource_);
    dxCommon->UploadTextureData(cubeResource_.Get(), mipImages);

    const uint32_t srvIndex = srvManager->Allocate();
    srvManager->CreateSrvForTextureCube(srvIndex, cubeResource_.Get(), metadata.format, UINT(metadata.mipLevels));
    srvHandle_ = srvManager->GetGPUDescriptorHandle(srvIndex);

    CreateSkyboxPipeline(dxCommon);
}

void EnvironmentMap::Finalize()
{
    if (skyConstantResource_ && skyConstants_) {
        skyConstantResource_->Unmap(0, nullptr);
    }
    skyConstants_ = nullptr;
    skyConstantResource_.Reset();
    skyPipelineState_.Reset();
    skyRootSignature_.Reset();
    cubeResource_.Reset();
}

void EnvironmentMap::Bind(ID3D12GraphicsCommandList* commandList) const
{
    commandList->SetGraphicsRootDescriptorTable(4, srvHandle_); // BasicPipeline::kRootEnvironment
}

void EnvironmentMap::CreateSkyboxPipeline(DirectXCommon* dxCommon)
{
    ID3D12Device* device = dxCommon->GetDevice();
    HRESULT hr;

    // ルートシグネチャ: b0(VS) 定数 / t0(PS) キューブマップ
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // 頂点（視線方向）と ピクセル（空の色）の両方で使う
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.pParameters = params;
    rootDesc.NumParameters = _countof(params);
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.NumStaticSamplers = 1;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Log(reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        assert(false);
    }
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(skyRootSignature_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // PSO: 頂点バッファなし（SV_VertexIDで画面全体の三角形を作る）、深度テスト/書き込みなし
    Microsoft::WRL::ComPtr<IDxcBlob> vs = dxCommon->CompileShader(L"Engine/Shaders/Skybox.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> ps = dxCommon->CompileShader(L"Engine/Shaders/Skybox.PS.hlsl", L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = skyRootSignature_.Get();
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DSVFormat = DirectXCommon::kDsvFormat;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DirectXCommon::kSceneFormat;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(skyPipelineState_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    skyConstantResource_ = dxCommon->CreateBufferResource(sizeof(SkyConstants));
    skyConstantResource_->Map(0, nullptr, reinterpret_cast<void**>(&skyConstants_));
}

void EnvironmentMap::DrawSkybox(ID3D12GraphicsCommandList* commandList, const Matrix4x4& view, const Matrix4x4& projection)
{
    if (!skyboxVisible || !skyPipelineState_) {
        return;
    }
    skyConstants_->invView = Inverse(view);
    skyConstants_->projScaleX = projection.m[0][0];
    skyConstants_->projScaleY = projection.m[1][1];
    skyConstants_->mode = sky_.procedural ? 1.0f : 0.0f;
    skyConstants_->padding = 0.0f;
    auto put = [](float* dst, const Vector3& v) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; dst[3] = 0.0f; };
    put(skyConstants_->horizon, sky_.horizon);
    put(skyConstants_->zenith, sky_.zenith);
    put(skyConstants_->ground, sky_.ground);
    put(skyConstants_->toSun, sky_.toSun);
    put(skyConstants_->discColor, sky_.discColor);
    put(skyConstants_->glowColor, sky_.glowColor);
    skyConstants_->cloud[0] = sky_.cloudCover;
    skyConstants_->cloud[1] = sky_.starAmount;
    skyConstants_->cloud[2] = sky_.cloudOffsetX;
    skyConstants_->cloud[3] = sky_.cloudOffsetZ;
    put(skyConstants_->cloudLit, sky_.cloudLit);
    skyConstants_->cloudLit[3] = sky_.time;
    put(skyConstants_->cloudShade, sky_.cloudShade);
    put(skyConstants_->moonDir, sky_.toMoon);
    put(skyConstants_->moonColor, sky_.moonColor);

    commandList->SetGraphicsRootSignature(skyRootSignature_.Get());
    commandList->SetPipelineState(skyPipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(0, skyConstantResource_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootDescriptorTable(1, srvHandle_);
    commandList->DrawInstanced(3, 1, 0, 0);
}
