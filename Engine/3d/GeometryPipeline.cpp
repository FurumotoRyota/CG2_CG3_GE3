#include "GeometryPipeline.h"

#include <cassert>

#include "../base/DirectXCommon.h"
#include "../base/Logger.h"

void GeometryPipeline::Initialize(DirectXCommon* dxCommon)
{
    assert(dxCommon);
    ID3D12Device* device = dxCommon->GetDevice();
    HRESULT hr;

    //--------------------------------------------------
    // ルートシグネチャ
    //--------------------------------------------------
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3] = {};
    params[kRootParams].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kRootParams].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // VS・GS・PSすべてが読む
    params[kRootParams].Descriptor.ShaderRegister = 0;
    params[kRootSpare].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kRootSpare].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[kRootSpare].Descriptor.ShaderRegister = 1;
    params[kRootTexture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootTexture].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[kRootTexture].DescriptorTable.pDescriptorRanges = &range;
    params[kRootTexture].DescriptorTable.NumDescriptorRanges = 1;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
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
    hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(rootSignature_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    //--------------------------------------------------
    // シェーダー（頂点 / ジオメトリ / ピクセルをそれぞれコンパイルする）
    //--------------------------------------------------
    auto compile = [&](const wchar_t* path, const wchar_t* profile) {
        return dxCommon->CompileShader(path, profile);
        };
    Microsoft::WRL::ComPtr<IDxcBlob> modelVs = compile(L"Engine/Shaders/GeometryModel.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> modelGs = compile(L"Engine/Shaders/GeometryModel.GS.hlsl", L"gs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> modelPs = compile(L"Engine/Shaders/GeometryModel.PS.hlsl", L"ps_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pointVs = compile(L"Engine/Shaders/GeometryPoint.VS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pointGs = compile(L"Engine/Shaders/GeometryPoint.GS.hlsl", L"gs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pointPs = compile(L"Engine/Shaders/GeometryPoint.PS.hlsl", L"ps_6_0");

    // 共通のPSO設定
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 加工で裏面が見えるので両面描く
    desc.DepthStencilState.DepthEnable = TRUE;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.DSVFormat = DirectXCommon::kDsvFormat;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = DirectXCommon::kSceneFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    //--------------------------------------------------
    // モデル加工：入力は VertexData(位置・UV・法線)、三角形リスト
    //--------------------------------------------------
    D3D12_INPUT_ELEMENT_DESC modelInput[3] = {};
    modelInput[0].SemanticName = "POSITION";
    modelInput[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    modelInput[1].SemanticName = "TEXCOORD";
    modelInput[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    modelInput[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    modelInput[2].SemanticName = "NORMAL";
    modelInput[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    modelInput[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC modelDesc = desc;
    modelDesc.InputLayout = { modelInput, _countof(modelInput) };
    modelDesc.VS = { modelVs->GetBufferPointer(), modelVs->GetBufferSize() };
    modelDesc.GS = { modelGs->GetBufferPointer(), modelGs->GetBufferSize() }; // ← ジオメトリシェーダーを追加
    modelDesc.PS = { modelPs->GetBufferPointer(), modelPs->GetBufferSize() };
    modelDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    modelDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    hr = device->CreateGraphicsPipelineState(&modelDesc, IID_PPV_ARGS(modelPso_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    //--------------------------------------------------
    // 点から図形：入力は GeometryPointVertex、点リスト。半透明（アルファブレンド）で深度は書かない
    //--------------------------------------------------
    D3D12_INPUT_ELEMENT_DESC pointInput[4] = {};
    pointInput[0].SemanticName = "POSITION";
    pointInput[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    pointInput[1].SemanticName = "TEXCOORD";
    pointInput[1].SemanticIndex = 0;
    pointInput[1].Format = DXGI_FORMAT_R32_FLOAT; // size
    pointInput[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    pointInput[2].SemanticName = "COLOR";
    pointInput[2].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    pointInput[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
    pointInput[3].SemanticName = "TEXCOORD";
    pointInput[3].SemanticIndex = 1;
    pointInput[3].Format = DXGI_FORMAT_R32_FLOAT; // seed
    pointInput[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pointDesc = desc;
    pointDesc.InputLayout = { pointInput, _countof(pointInput) };
    pointDesc.VS = { pointVs->GetBufferPointer(), pointVs->GetBufferSize() };
    pointDesc.GS = { pointGs->GetBufferPointer(), pointGs->GetBufferSize() };
    pointDesc.PS = { pointPs->GetBufferPointer(), pointPs->GetBufferSize() };
    pointDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pointDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = pointDesc.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_ZERO;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateGraphicsPipelineState(&pointDesc, IID_PPV_ARGS(pointPso_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    //--------------------------------------------------
    // スライドのサンプル：入力は三角形、GSの出力の種類（点 / 線 / 三角形）だけが違う
    // PSOのPrimitiveTopologyTypeは「GSに入ってくる図形」の種類なので、全部TRIANGLE（出力が点・線でも変わらない）
    //--------------------------------------------------
    const wchar_t* samplePaths[kSampleCount] = {
        L"Engine/Shaders/GeometrySample0_Point.GS.hlsl",
        L"Engine/Shaders/GeometrySample1_Append.GS.hlsl",
        L"Engine/Shaders/GeometrySample2_Line.GS.hlsl",
        L"Engine/Shaders/GeometrySample3_LineStrip.GS.hlsl",
        L"Engine/Shaders/GeometrySample4_RestartStrip.GS.hlsl",
        L"Engine/Shaders/GeometrySample5_Duplicate.GS.hlsl",
        L"Engine/Shaders/GeometrySample6_UvTile.GS.hlsl",
    };
    Microsoft::WRL::ComPtr<IDxcBlob> samplePs = compile(L"Engine/Shaders/GeometrySample.PS.hlsl", L"ps_6_0");
    for (int i = 0; i < kSampleCount; i++) {
        Microsoft::WRL::ComPtr<IDxcBlob> gs = compile(samplePaths[i], L"gs_6_0");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC sampleDesc = modelDesc;
        sampleDesc.GS = { gs->GetBufferPointer(), gs->GetBufferSize() };
        sampleDesc.PS = { samplePs->GetBufferPointer(), samplePs->GetBufferSize() };
        hr = device->CreateGraphicsPipelineState(&sampleDesc, IID_PPV_ARGS(samplePso_[i].GetAddressOf()));
        assert(SUCCEEDED(hr));
    }
}

void GeometryPipeline::BindModel(ID3D12GraphicsCommandList* commandList, D3D12_GPU_VIRTUAL_ADDRESS paramsAddress) const
{
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(modelPso_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(kRootParams, paramsAddress);
    commandList->SetGraphicsRootConstantBufferView(kRootSpare, paramsAddress);
}

void GeometryPipeline::BindPoints(ID3D12GraphicsCommandList* commandList, D3D12_GPU_VIRTUAL_ADDRESS paramsAddress) const
{
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pointPso_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    commandList->SetGraphicsRootConstantBufferView(kRootParams, paramsAddress);
    commandList->SetGraphicsRootConstantBufferView(kRootSpare, paramsAddress);
}

void GeometryPipeline::BindSample(ID3D12GraphicsCommandList* commandList, D3D12_GPU_VIRTUAL_ADDRESS paramsAddress, int sampleIndex) const
{
    assert(sampleIndex >= 0 && sampleIndex < kSampleCount);
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(samplePso_[sampleIndex].Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->SetGraphicsRootConstantBufferView(kRootParams, paramsAddress);
    commandList->SetGraphicsRootConstantBufferView(kRootSpare, paramsAddress);
}

void GeometryPipeline::Finalize()
{
    for (auto& pso : samplePso_) {
        pso.Reset();
    }
    pointPso_.Reset();
    modelPso_.Reset();
    rootSignature_.Reset();
}
