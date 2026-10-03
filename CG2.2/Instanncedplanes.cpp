#include "InstancedPlanes.h"
#include "../base/DirectXCommon.h"
#include "../base/Logger.h"
#include "../base/SrvManager.h"
#include "../base/TextureManager.h"
#include "../3d/VertexData.h"

#include <algorithm>
#include <cassert>

using Microsoft::WRL::ComPtr;

//==================================================
// InstancedPlaneCommon
//==================================================
void InstancedPlaneCommon::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, SrvManager* srvManager)
{
    assert(dxCommon);
    assert(textureManager);
    assert(srvManager);
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    srvManager_ = srvManager;

    ID3D12Device* device = dxCommon_->GetDevice();
    HRESULT hr = S_OK;

    // ルートシグネチャ
    //  [0] StructuredBuffer(t0) のSRVテーブル（VS）…板ごとのWVP・色
    //  [1] テクスチャ(t0) のSRVテーブル（PS）
    //  ※ t0 が2つあるが、見えるシェーダー(VS/PS)が違うので衝突しない
    D3D12_DESCRIPTOR_RANGE instanceRange[1] = {};
    instanceRange[0].BaseShaderRegister = 0;
    instanceRange[0].NumDescriptors = 1;
    instanceRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    instanceRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE textureRange[1] = {};
    textureRange[0].BaseShaderRegister = 0;
    textureRange[0].NumDescriptors = 1;
    textureRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.pDescriptorRanges = instanceRange;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(instanceRange);
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].DescriptorTable.pDescriptorRanges = textureRange;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(textureRange);

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);
    rootSignatureDesc.pStaticSamplers = staticSamplers;
    rootSignatureDesc.NumStaticSamplers = _countof(staticSamplers);

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(
        &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Log(reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        assert(false);
    }
    hr = device->CreateRootSignature(
        0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(rootSignature_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // 入力レイアウト（VertexData: position / texcoord / normal）
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = 0;
    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    // 通常のαブレンド。板は両面から見えるようカリング無し。透明な部分はPSでdiscardするので深度は書き込む
    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"InstancedPlane.VS.hlsl", L"vs_6_0");
    ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"InstancedPlane.PS.hlsl", L"ps_6_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = inputLayoutDesc;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.BlendState = blendDesc;
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.DSVFormat = DirectXCommon::kDsvFormat;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DirectXCommon::kRtvFormat;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pipelineState_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // 中心原点(-0.5～0.5)の1枚のクアッド(6頂点、非インデックス描画)。全ての板で共有する
    vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * 6);
    VertexData* vertexData = nullptr;
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    vertexData[0].position = { -0.5f,  0.5f, 0.0f, 1.0f }; vertexData[0].texcoord = { 0.0f, 0.0f };
    vertexData[1].position = { -0.5f, -0.5f, 0.0f, 1.0f }; vertexData[1].texcoord = { 0.0f, 1.0f };
    vertexData[2].position = { 0.5f,  0.5f, 0.0f, 1.0f }; vertexData[2].texcoord = { 1.0f, 0.0f };
    vertexData[3].position = { 0.5f,  0.5f, 0.0f, 1.0f }; vertexData[3].texcoord = { 1.0f, 0.0f };
    vertexData[4].position = { -0.5f, -0.5f, 0.0f, 1.0f }; vertexData[4].texcoord = { 0.0f, 1.0f };
    vertexData[5].position = { 0.5f, -0.5f, 0.0f, 1.0f }; vertexData[5].texcoord = { 1.0f, 1.0f };
    for (int i = 0; i < 6; ++i) {
        vertexData[i].normal = { 0.0f, 0.0f, -1.0f };
    }
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

void InstancedPlaneCommon::PreDraw()
{
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
}

//==================================================
// InstancedPlanes
//==================================================
InstancedPlanes::~InstancedPlanes()
{
    // SRVのスロットを返す（追加・削除を繰り返してもヒープが尽きないように）
    if (common_ && instanceResource_) {
        common_->GetSrvManager()->Free(srvIndex_);
    }
}

void InstancedPlanes::Initialize(InstancedPlaneCommon* common, const std::string& texturePath)
{
    assert(common);
    common_ = common;
    textureHandle_ = common_->GetTextureManager()->Load(texturePath);

    // 板ごとのWVP/色を入れるバッファ（最大 kMaxInstances 枚ぶん）
    instanceResource_ = common_->GetDxCommon()->CreateBufferResource(sizeof(PlaneForGPU) * kMaxInstances);
    instanceResource_->Map(0, nullptr, reinterpret_cast<void**>(&instanceData_));
    for (uint32_t i = 0; i < kMaxInstances; ++i) {
        instanceData_[i].WVP = Matrix4x4::MakeIdentity4x4();
        instanceData_[i].color = { 1.0f, 1.0f, 1.0f, 1.0f };
    }

    SrvManager* srvManager = common_->GetSrvManager();
    srvIndex_ = srvManager->Allocate();
    srvManager->CreateSrvForStructuredBuffer(srvIndex_, instanceResource_.Get(), kMaxInstances, sizeof(PlaneForGPU));
    instanceSrvHandleGPU_ = srvManager->GetGPUDescriptorHandle(srvIndex_);
}

void InstancedPlanes::SetTexture(const std::string& texturePath)
{
    textureHandle_ = common_->GetTextureManager()->Load(texturePath);
}

int InstancedPlanes::GetDrawCount() const
{
    return (std::min)((std::max)(settings_.count, 0), static_cast<int>(kMaxInstances));
}

Transform InstancedPlanes::GetInstanceTransform(int index) const
{
    const float i = static_cast<float>(index);
    Transform t = settings_.base;
    t.Translate = {
        settings_.base.Translate.x + settings_.spacing.x * i,
        settings_.base.Translate.y + settings_.spacing.y * i,
        settings_.base.Translate.z + settings_.spacing.z * i
    };
    t.Rotate = {
        settings_.base.Rotate.x + settings_.rotateStep.x * i,
        settings_.base.Rotate.y + settings_.rotateStep.y * i,
        settings_.base.Rotate.z + settings_.rotateStep.z * i
    };
    return t;
}

void InstancedPlanes::Update(const Matrix4x4& view, const Matrix4x4& projection)
{
    const Matrix4x4 viewProjection = Multiply(view, projection);
    const int drawCount = GetDrawCount();
    for (int i = 0; i < drawCount; ++i) {
        const Transform t = GetInstanceTransform(i);
        const Matrix4x4 world = Matrix4x4::MakeAffineMatrix(t.Scale, t.Rotate, t.Translate);
        instanceData_[i].WVP = Multiply(world, viewProjection);
        instanceData_[i].color = settings_.color;
    }
}

void InstancedPlanes::Draw()
{
    const int drawCount = GetDrawCount();
    if (drawCount <= 0) {
        return;
    }
    assert(drawCount <= static_cast<int>(kMaxInstances)); // 確保したバッファの枚数を超えないこと

    ID3D12GraphicsCommandList* commandList = common_->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootDescriptorTable(0, instanceSrvHandleGPU_);
    commandList->SetGraphicsRootDescriptorTable(1, common_->GetTextureManager()->GetSrvHandleGPU(textureHandle_));
    commandList->DrawInstanced(6, static_cast<UINT>(drawCount), 0, 0);
}