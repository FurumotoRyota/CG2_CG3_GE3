#include "BasicPipeline.h"
#include "../base/DirectXCommon.h"
#include "../base/Logger.h"

#include <cassert>

void BasicPipeline::Initialize(DirectXCommon* dxCommon, bool useDepth)
{
    assert(dxCommon);
    ID3D12Device* device = dxCommon->GetDevice();
    HRESULT hr;

    //--------------------------------------------------
    // ルートシグネチャ
    //--------------------------------------------------
    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[kRootMaterial].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootMaterial].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootMaterial].Descriptor.ShaderRegister = 0;
    rootParameters[kRootTransform].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootTransform].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[kRootTransform].Descriptor.ShaderRegister = 0;
    rootParameters[kRootTexture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[kRootTexture].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootTexture].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[kRootTexture].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);
    rootParameters[kRootLight].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[kRootLight].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[kRootLight].Descriptor.ShaderRegister = 1;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    hr = D3D12SerializeRootSignature(
        &descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1,
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

    //--------------------------------------------------
    // 入力レイアウト（VertexData: position / texcoord / normal）
    //--------------------------------------------------
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = 0;

    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    //--------------------------------------------------
    // PSO
    //--------------------------------------------------
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxCommon->CompileShader(L"Engine/Shaders/Object3D.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = dxCommon->CompileShader(L"Engine/Shaders/Object3D.PS.hlsl", L"ps_6_0");

    // ブレンドモードごとにPSOを作る
    // 前半 kBlendModeCount 個は裏面カリングあり、後半は両面描画（カリングなし）
    for (int index = 0; index < kBlendModeCount * 2; ++index) {
        const BlendMode mode = static_cast<BlendMode>(index % kBlendModeCount);
        rasterizerDesc.CullMode = (index < kBlendModeCount) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;

        D3D12_BLEND_DESC blendDesc{};
        D3D12_RENDER_TARGET_BLEND_DESC& rt = blendDesc.RenderTarget[0];
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        rt.BlendEnable = (mode != BlendMode::None);
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        // α値の書き込みは常に「描く側のα」をそのまま使う
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;

        switch (mode) {
        case BlendMode::Normal:   // 描く色*α + 背景*(1-α)
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            break;
        case BlendMode::Add:      // 背景 + 描く色*α
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_ONE;
            break;
        case BlendMode::Subtract: // 背景 - 描く色*α
            rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend = D3D12_BLEND_ONE;
            rt.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
            break;
        case BlendMode::Multiply: // 背景 * 描く色
            rt.SrcBlend = D3D12_BLEND_ZERO;
            rt.DestBlend = D3D12_BLEND_SRC_COLOR;
            break;
        case BlendMode::Screen:   // 背景 + 描く色*(1-背景)
            rt.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
            rt.DestBlend = D3D12_BLEND_ONE;
            break;
        default:                  // None（BlendEnable=falseなので値は使われない）
            rt.SrcBlend = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_ZERO;
            break;
        }

        // 深度：半透明の物が後ろの物を隠さないよう、ブレンドありの描画は深度を書き込まない（テストだけ行う）
        D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
        depthStencilDesc.DepthEnable = useDepth;
        depthStencilDesc.DepthWriteMask = (mode == BlendMode::None) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc{};
        pipelineStateDesc.pRootSignature = rootSignature_.Get();
        pipelineStateDesc.InputLayout = inputLayoutDesc;
        pipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
        pipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
        pipelineStateDesc.BlendState = blendDesc;
        pipelineStateDesc.RasterizerState = rasterizerDesc;
        pipelineStateDesc.DepthStencilState = depthStencilDesc;
        pipelineStateDesc.DSVFormat = DirectXCommon::kDsvFormat;
        pipelineStateDesc.NumRenderTargets = 1;
        pipelineStateDesc.RTVFormats[0] = DirectXCommon::kRtvFormat;
        pipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineStateDesc.SampleDesc.Count = 1;
        pipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

        hr = device->CreateGraphicsPipelineState(&pipelineStateDesc, IID_PPV_ARGS(pipelineStates_[index].GetAddressOf()));
        assert(SUCCEEDED(hr));
    }
}

void BasicPipeline::Bind(ID3D12GraphicsCommandList* commandList) const
{
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineStates_[static_cast<int>(BlendMode::None)].Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void BasicPipeline::SetBlendMode(ID3D12GraphicsCommandList* commandList, BlendMode mode, bool doubleSided) const
{
    const int index = (doubleSided ? kBlendModeCount : 0) + static_cast<int>(mode);
    commandList->SetPipelineState(pipelineStates_[index].Get());
}
