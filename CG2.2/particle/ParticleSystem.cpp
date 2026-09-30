#include "ParticleSystem.h"
#include "../base/DirectXCommon.h"
#include "../base/SrvManager.h"
#include "../base/Logger.h"
#include "../3d/VertexData.h"
#include "../math/Color.h"

#include <algorithm>
#include <cassert>

using Microsoft::WRL::ComPtr;

void ParticleSystem::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager)
{
    assert(dxCommon);
    assert(srvManager);
    dxCommon_ = dxCommon;

    CreatePipeline();
    CreateResources(srvManager);
}

void ParticleSystem::CreatePipeline()
{
    ID3D12Device* device = dxCommon_->GetDevice();
    HRESULT hr = S_OK;

    // 入力レイアウト（POSITION/TEXCOORD/NORMAL。NORMALは未使用）
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

    // ルートシグネチャ：StructuredBuffer(t0)のSRVテーブル1個のみ
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[1] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumParameters = _countof(rootParameters);

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

    // ブレンド：通常のアルファブレンド
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
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // ビルボードなのでカリング無し
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Particle.VS.hlsl", L"vs_6_0");
    ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Particle.PS.hlsl", L"ps_6_0");

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度は書き込まない（重なりを綺麗に見せるため）
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

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
}

void ParticleSystem::CreateResources(SrvManager* srvManager)
{
    // 中心原点(-0.5～0.5)の1枚のクアッド(6頂点、非インデックス描画)
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

    // パーティクルごとのWVP/色（StructuredBuffer）
    instanceResource_ = dxCommon_->CreateBufferResource(sizeof(ParticleForGPU) * kMaxParticles);
    instanceResource_->Map(0, nullptr, reinterpret_cast<void**>(&instanceData_));
    for (uint32_t i = 0; i < kMaxParticles; ++i) {
        instanceData_[i].WVP = Matrix4x4::MakeIdentity4x4();
        instanceData_[i].color = { 1.0f, 1.0f, 1.0f, 0.0f }; // 初期状態は透明
    }

    const uint32_t srvIndex = srvManager->Allocate();
    srvManager->CreateSrvForStructuredBuffer(srvIndex, instanceResource_.Get(), kMaxParticles, sizeof(ParticleForGPU));
    instanceSrvHandleGPU_ = srvManager->GetGPUDescriptorHandle(srvIndex);
}

ParticleSystem::Particle ParticleSystem::MakeRandomPartyParticle(const Vector3& emitPosition)
{
    std::uniform_real_distribution<float> distVelXZ(-3.0f, 3.0f);
    std::uniform_real_distribution<float> distVelY(1.5f, 4.5f);
    std::uniform_real_distribution<float> distLife(0.6f, 1.4f);
    std::uniform_real_distribution<float> distScale(0.15f, 0.35f);
    std::uniform_real_distribution<float> distHue(0.0f, 360.0f);

    Particle particle{};
    particle.position = emitPosition;
    particle.velocity = { distVelXZ(randomEngine_), distVelY(randomEngine_), distVelXZ(randomEngine_) };
    particle.color = HueToColor(distHue(randomEngine_));
    particle.lifeTime = distLife(randomEngine_);
    particle.currentTime = 0.0f;
    particle.scale = distScale(randomEngine_);
    return particle;
}

void ParticleSystem::Update(const Matrix4x4& view, const Matrix4x4& projection, bool enableEmit)
{
    constexpr float kDeltaTime = 1.0f / 60.0f;
    constexpr float kGravity = 3.0f;

    // カメラの位置と向き(right/up/forward)を取得する
    Matrix4x4 cameraWorldMatrix = Inverse(view);
    Vector3 cameraRight = { cameraWorldMatrix.m[0][0], cameraWorldMatrix.m[0][1], cameraWorldMatrix.m[0][2] };
    Vector3 cameraUp = { cameraWorldMatrix.m[1][0], cameraWorldMatrix.m[1][1], cameraWorldMatrix.m[1][2] };
    Vector3 cameraForward = { cameraWorldMatrix.m[2][0], cameraWorldMatrix.m[2][1], cameraWorldMatrix.m[2][2] };
    Vector3 cameraPosition = { cameraWorldMatrix.m[3][0], cameraWorldMatrix.m[3][1], cameraWorldMatrix.m[3][2] };

    // カメラ前方の平面上のランダムな位置（＝画面全体）から発生させる
    if (enableEmit) {
        constexpr int kSpawnCountPerFrame = 6;
        constexpr float kEmitDistance = 15.0f;   // カメラからの発生距離
        constexpr float kEmitHalfWidth = 12.0f;  // 画面の横方向の広がり
        constexpr float kEmitHalfHeight = 7.0f;  // 画面の縦方向の広がり
        std::uniform_real_distribution<float> distOffsetX(-kEmitHalfWidth, kEmitHalfWidth);
        std::uniform_real_distribution<float> distOffsetY(-kEmitHalfHeight, kEmitHalfHeight);

        for (int i = 0; i < kSpawnCountPerFrame && particles_.size() < kMaxParticles; ++i) {
            float offsetX = distOffsetX(randomEngine_);
            float offsetY = distOffsetY(randomEngine_);

            Vector3 emitPosition = {
                cameraPosition.x + cameraForward.x * kEmitDistance + cameraRight.x * offsetX + cameraUp.x * offsetY,
                cameraPosition.y + cameraForward.y * kEmitDistance + cameraRight.y * offsetX + cameraUp.y * offsetY,
                cameraPosition.z + cameraForward.z * kEmitDistance + cameraRight.z * offsetX + cameraUp.z * offsetY
            };
            particles_.push_back(MakeRandomPartyParticle(emitPosition));
        }
    }

    for (auto& particle : particles_) {
        particle.currentTime += kDeltaTime;
        particle.velocity.y -= kGravity * kDeltaTime;
        particle.position = Add(particle.position, {
            particle.velocity.x * kDeltaTime,
            particle.velocity.y * kDeltaTime,
            particle.velocity.z * kDeltaTime
            });
    }

    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
            [](const Particle& p) { return p.currentTime >= p.lifeTime; }),
        particles_.end());

    // カメラの回転成分だけを取り出したビルボード行列（平行移動は各パーティクルの位置を使う）
    cameraWorldMatrix.m[3][0] = 0.0f;
    cameraWorldMatrix.m[3][1] = 0.0f;
    cameraWorldMatrix.m[3][2] = 0.0f;

    for (size_t i = 0; i < particles_.size(); ++i) {
        const Particle& particle = particles_[i];
        float lifeRatio = particle.currentTime / particle.lifeTime; // 0(誕生)～1(消滅)
        float alpha = 1.0f - lifeRatio; // だんだん透明に

        Matrix4x4 scaleMatrix = Matrix4x4::MakeScaleMatrix({ particle.scale, particle.scale, particle.scale });
        Matrix4x4 translateMatrix = Matrix4x4::MakeTranslateMatrix(particle.position);
        Matrix4x4 world = Multiply(scaleMatrix, Multiply(cameraWorldMatrix, translateMatrix));

        instanceData_[i].WVP = Multiply(world, Multiply(view, projection));
        instanceData_[i].color = { particle.color.x, particle.color.y, particle.color.z, alpha };
    }
}

void ParticleSystem::Draw()
{
    if (particles_.empty()) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootDescriptorTable(0, instanceSrvHandleGPU_);
    commandList->DrawInstanced(6, UINT(particles_.size()), 0, 0);
}
