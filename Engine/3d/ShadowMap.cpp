#include "ShadowMap.h"

#include <cassert>
#include <cmath>

#include "../base/DirectXCommon.h"
#include "../base/SrvManager.h"

ShadowMap& ShadowMap::Instance()
{
    static ShadowMap instance;
    return instance;
}

void ShadowMap::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    assert(dxCommon);
    assert(srvManager);
    ID3D12Device* device = dxCommon->GetDevice();
    HRESULT hr;

    // 深度テクスチャ：書き込みはD32_FLOAT、読み取りはR32_FLOATで見る（TYPELESSで作る）
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = kSize;
    resourceDesc.Height = kSize;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    hr = device->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(depthResource_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // DSV（専用のヒープ）
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(dsvHeap_.GetAddressOf()));
    assert(SUCCEEDED(hr));
    dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsvHandle_);

    // SRV
    const uint32_t srvIndex = srvManager->Allocate();
    srvManager->CreateSrvForTexture2D(srvIndex, depthResource_.Get(), DXGI_FORMAT_R32_FLOAT, 1);
    srvHandle_ = srvManager->GetGPUDescriptorHandle(srvIndex);

    // ルートシグネチャ：b0(VS) TransformationMatrix（LightWVPだけ使う）
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    param.Descriptor.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootDesc.pParameters = &param;
    rootDesc.NumParameters = 1;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
    assert(SUCCEEDED(hr));
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // PSO：深度だけ描く（ピクセルシェーダーなし・レンダーターゲットなし）
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    Microsoft::WRL::ComPtr<IDxcBlob> vs = dxCommon->CompileShader(L"Engine/Shaders/Shadow.VS.hlsl", L"vs_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 薄い板も影を落とせるよう両面を描く
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.SlopeScaledDepthBias = 1.5f;
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    desc.NumRenderTargets = 0;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    hr = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pipelineState_.GetAddressOf()));
    assert(SUCCEEDED(hr));
}

void ShadowMap::Finalize()
{
    pipelineState_.Reset();
    rootSignature_.Reset();
    dsvHeap_.Reset();
    depthResource_.Reset();
}

void ShadowMap::Bind(ID3D12GraphicsCommandList* commandList) const
{
    commandList->SetGraphicsRootDescriptorTable(5, srvHandle_); // BasicPipeline::kRootShadow
}

void ShadowMap::Begin(ID3D12GraphicsCommandList* commandList)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depthResource_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    commandList->ResourceBarrier(1, &barrier);

    commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle_);
    commandList->ClearDepthStencilView(dsvHandle_, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 1.0f };
    D3D12_RECT scissor{ 0, 0, static_cast<LONG>(kSize), static_cast<LONG>(kSize) };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void ShadowMap::End(ID3D12GraphicsCommandList* commandList)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depthResource_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

Matrix4x4 ShadowMap::ComputeLightViewProjection(const Vector3& dir, const Vector3& center, float range)
{
    // 光の進む向き f（正規化）
    float fl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    Vector3 f = fl > 0.0001f ? Vector3{ dir.x / fl, dir.y / fl, dir.z / fl } : Vector3{ 0.0f, -1.0f, 0.0f };

    // 真上/真下を向く光は up=Y だと外積が0になるので、Zを上にする
    Vector3 up = (std::fabs(f.y) > 0.99f) ? Vector3{ 0.0f, 0.0f, 1.0f } : Vector3{ 0.0f, 1.0f, 0.0f };

    // 左手系：right = up × f, newUp = f × right
    Vector3 r{ up.y * f.z - up.z * f.y, up.z * f.x - up.x * f.z, up.x * f.y - up.y * f.x };
    const float rl = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    r = { r.x / rl, r.y / rl, r.z / rl };
    const Vector3 u{ f.y * r.z - f.z * r.y, f.z * r.x - f.x * r.z, f.x * r.y - f.y * r.x };

    // 影テクスチャの1マスの大きさ（ワールド）。中心をこの刻みに合わせて、カメラが動いても影の縁がちらつかないようにする
    const float texel = range * 2.0f / static_cast<float>(kSize);
    const float cr = center.x * r.x + center.y * r.y + center.z * r.z;
    const float cu = center.x * u.x + center.y * u.y + center.z * u.z;
    const float dr = std::fmod(cr, texel);
    const float du = std::fmod(cu, texel);
    const Vector3 snapped{ center.x - r.x * dr - u.x * du, center.y - r.y * dr - u.y * du, center.z - r.z * dr - u.z * du };

    // 光源の位置：範囲の中心から光と反対側へ引いた位置
    const float distance = range * 1.5f;
    const Vector3 pos{ snapped.x - f.x * distance, snapped.y - f.y * distance, snapped.z - f.z * distance };

    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    view.m[0][0] = r.x; view.m[0][1] = u.x; view.m[0][2] = f.x;
    view.m[1][0] = r.y; view.m[1][1] = u.y; view.m[1][2] = f.y;
    view.m[2][0] = r.z; view.m[2][1] = u.z; view.m[2][2] = f.z;
    view.m[3][0] = -(r.x * pos.x + r.y * pos.y + r.z * pos.z);
    view.m[3][1] = -(u.x * pos.x + u.y * pos.y + u.z * pos.z);
    view.m[3][2] = -(f.x * pos.x + f.y * pos.y + f.z * pos.z);

    const Matrix4x4 projection = Matrix4x4::MakeOrthographicMatrix(-range, range, range, -range, 0.1f, range * 3.0f);
    return Multiply(view, projection);
}
