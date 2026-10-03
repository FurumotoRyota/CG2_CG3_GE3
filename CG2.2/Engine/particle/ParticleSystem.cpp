#include "ParticleSystem.h"
#include "../base/DirectXCommon.h"
#include "../base/SrvManager.h"
#include "../base/Logger.h"
#include "../3d/VertexData.h"
#include "../math/Color.h"

#include <algorithm>
#include <cassert>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr float kDeltaTime = 1.0f / 60.0f;
    constexpr float kPi = 3.14159265358979f;

    Vector4 Lerp(const Vector4& a, const Vector4& b, float t)
    {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
    }
}

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

    // ルートシグネチャ
    //  [0] StructuredBuffer(t0) のSRVテーブル（VS）
    //  [1] 32bit定数1個(b0)：このDrawが読み始めるインスタンスの番号（VS）
    //      ※SV_InstanceIDは毎回0から始まるので、通常/加算の2回描画を区別するために使う
    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Constants.ShaderRegister = 0;
    rootParameters[1].Constants.RegisterSpace = 0;
    rootParameters[1].Constants.Num32BitValues = 1;

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

    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // ビルボードなのでカリング無し
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    ComPtr<IDxcBlob> vsBlob = dxCommon_->CompileShader(L"Engine/Shaders/Particle.VS.hlsl", L"vs_6_0");
    ComPtr<IDxcBlob> psBlob = dxCommon_->CompileShader(L"Engine/Shaders/Particle.PS.hlsl", L"ps_6_0");

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度は書き込まない（重なりを綺麗に見せるため）
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    // 通常(0)・加算(1)の2つのPSOを作る
    for (int i = 0; i < 2; ++i) {
        const bool additive = (i == 1);

        D3D12_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = additive ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

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

        ComPtr<ID3D12PipelineState>& target = additive ? additivePipelineState_ : normalPipelineState_;
        hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(target.GetAddressOf()));
        assert(SUCCEEDED(hr));
    }
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

    // パーティクルごとのWVP/色/形（StructuredBuffer）
    instanceResource_ = dxCommon_->CreateBufferResource(sizeof(ParticleForGPU) * kMaxParticles);
    instanceResource_->Map(0, nullptr, reinterpret_cast<void**>(&instanceData_));
    for (uint32_t i = 0; i < kMaxParticles; ++i) {
        instanceData_[i].WVP = Matrix4x4::MakeIdentity4x4();
        instanceData_[i].color = { 1.0f, 1.0f, 1.0f, 0.0f }; // 初期状態は透明
        instanceData_[i].params = { 0.0f, 0.0f, 0.0f, 0.0f };
    }

    const uint32_t srvIndex = srvManager->Allocate();
    srvManager->CreateSrvForStructuredBuffer(srvIndex, instanceResource_.Get(), kMaxParticles, sizeof(ParticleForGPU));
    instanceSrvHandleGPU_ = srvManager->GetGPUDescriptorHandle(srvIndex);
}

float ParticleSystem::Random(float minValue, float maxValue)
{
    if (maxValue <= minValue) {
        return minValue;
    }
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(randomEngine_);
}

// direction を中心に、spreadDeg度の円錐の中でランダムな向きを返す（180度で全方向）
Vector3 ParticleSystem::RandomDirectionInCone(const Vector3& direction, float spreadDeg)
{
    Vector3 d = direction.Normalize();
    if (d.Length() == 0.0f) {
        d = { 0.0f, 1.0f, 0.0f };
    }
    const float spread = (std::min)((std::max)(spreadDeg, 0.0f), 180.0f) * kPi / 180.0f;
    const float cosMax = std::cos(spread);
    const float cosT = Random(cosMax, 1.0f);
    const float sinT = std::sqrt((std::max)(0.0f, 1.0f - cosT * cosT));
    const float phi = Random(0.0f, 2.0f * kPi);

    // dに垂直な2本の軸u,vを作る
    const Vector3 helper = (std::fabs(d.y) < 0.99f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
    const Vector3 u = helper.Cross(d).Normalize();
    const Vector3 v = d.Cross(u);

    return u.Multiply(sinT * std::cos(phi))
        .Add(v.Multiply(sinT * std::sin(phi)))
        .Add(d.Multiply(cosT));
}

void ParticleSystem::SpawnFromEmitter(const ParticleEmitter& e)
{
    if (particles_.size() >= kMaxParticles) {
        return;
    }

    // 発生位置：半径radiusの球（emitFlatなら水平の円盤）の中のランダムな点
    Vector3 offset{ 0.0f, 0.0f, 0.0f };
    if (e.radius > 0.0f) {
        do {
            offset = { Random(-1.0f, 1.0f), e.emitFlat ? 0.0f : Random(-1.0f, 1.0f), Random(-1.0f, 1.0f) };
        } while (offset.Dot(offset) > 1.0f);
        offset = offset.Multiply(e.radius);
    }

    Particle p{};
    p.position = e.position.Add(offset);
    p.velocity = RandomDirectionInCone(e.direction, e.spreadDeg).Multiply(Random(e.speedMin, e.speedMax));
    p.lifeTime = (std::max)(Random(e.lifeMin, e.lifeMax), 0.05f);

    const float sizeScale = 1.0f + Random(-e.sizeRandom, e.sizeRandom);
    p.sizeStart = e.sizeStart * sizeScale;
    p.sizeEnd = e.sizeEnd * sizeScale;

    p.gravity = e.gravity;
    p.drag = e.drag;
    p.wobble = e.wobble;
    p.wobblePhase = Random(0.0f, 2.0f * kPi);
    p.shape = e.shape;
    p.additive = e.additive;

    if (e.rainbow) {
        const Vector4 c = HueToColor(Random(0.0f, 360.0f));
        p.colorStart = { c.x, c.y, c.z, e.colorStart.w };
        p.colorEnd = { c.x, c.y, c.z, e.colorEnd.w };
    }
    else {
        p.colorStart = e.colorStart;
        p.colorEnd = e.colorEnd;
    }

    if (e.fireworks) {
        p.rocket = true;
        p.sparkCount = e.sparkCount;
        p.sparkHue = Random(0.0f, 360.0f);
    }

    particles_.push_back(p);
}

// 花火のロケットが消えた位置から、火花を全方向に飛ばす
void ParticleSystem::SpawnSparks(const Particle& rocket, std::vector<Particle>& outParticles)
{
    for (int i = 0; i < rocket.sparkCount; ++i) {
        Particle s{};
        s.position = rocket.position;
        s.velocity = RandomDirectionInCone({ 0.0f, 1.0f, 0.0f }, 180.0f).Multiply(Random(3.0f, 8.0f));
        s.lifeTime = Random(1.0f, 1.8f);
        s.sizeStart = Random(0.4f, 0.7f);
        s.sizeEnd = 0.05f;
        s.gravity = 3.0f;
        s.drag = 1.2f;
        s.shape = ParticleShape::SoftCircle;
        s.additive = true;

        // 1発の花火は近い色でそろえる（色相±25度）
        const Vector4 c = HueToColor(rocket.sparkHue + Random(-25.0f, 25.0f));
        s.colorStart = { c.x, c.y, c.z, 1.0f };
        s.colorEnd = { c.x, c.y, c.z, 0.0f };
        outParticles.push_back(s);
    }
}

// 従来のパーティーモード用：カメラ前方の平面から発生する虹色の四角い粒
ParticleSystem::Particle ParticleSystem::MakePartyParticle(const Vector3& emitPosition)
{
    Particle p{};
    p.position = emitPosition;
    p.velocity = { Random(-3.0f, 3.0f), Random(1.5f, 4.5f), Random(-3.0f, 3.0f) };
    const Vector4 c = HueToColor(Random(0.0f, 360.0f));
    p.colorStart = { c.x, c.y, c.z, 1.0f };
    p.colorEnd = { c.x, c.y, c.z, 0.0f };
    p.lifeTime = Random(0.6f, 1.4f);
    p.sizeStart = p.sizeEnd = Random(0.15f, 0.35f);
    p.gravity = 3.0f;
    p.shape = ParticleShape::Square;
    p.additive = false;
    return p;
}

void ParticleSystem::UpdateEmitter(ParticleEmitter& emitter)
{
    auto burst = [&]() {
        for (int i = 0; i < emitter.burstCount; ++i) {
            SpawnFromEmitter(emitter);
        }
        };

    // Playボタン：有効/無効に関係なく1回バーストする
    if (emitter.playRequested) {
        emitter.playRequested = false;
        if (emitter.mode == EmitMode::Burst) {
            burst();
        }
        else {
            const int count = (std::max)(1, static_cast<int>(emitter.rate * 0.5f));
            for (int i = 0; i < count; ++i) {
                SpawnFromEmitter(emitter);
            }
        }
    }

    if (!emitter.enabled) {
        return;
    }

    if (emitter.mode == EmitMode::Continuous) {
        emitter.emitAccumulator += (std::max)(emitter.rate, 0.0f) * kDeltaTime;
        while (emitter.emitAccumulator >= 1.0f) {
            emitter.emitAccumulator -= 1.0f;
            SpawnFromEmitter(emitter);
        }
    }
    else if (emitter.burstInterval > 0.0f) {
        emitter.burstTimer += kDeltaTime;
        if (emitter.burstTimer >= emitter.burstInterval) {
            emitter.burstTimer -= emitter.burstInterval;
            burst();
        }
    }
}

void ParticleSystem::Update(const Matrix4x4& view, const Matrix4x4& projection, bool enablePartyEmit)
{
    // カメラの位置と向き(right/up/forward)を取得する
    Matrix4x4 cameraWorldMatrix = Inverse(view);
    Vector3 cameraRight = { cameraWorldMatrix.m[0][0], cameraWorldMatrix.m[0][1], cameraWorldMatrix.m[0][2] };
    Vector3 cameraUp = { cameraWorldMatrix.m[1][0], cameraWorldMatrix.m[1][1], cameraWorldMatrix.m[1][2] };
    Vector3 cameraForward = { cameraWorldMatrix.m[2][0], cameraWorldMatrix.m[2][1], cameraWorldMatrix.m[2][2] };
    Vector3 cameraPosition = { cameraWorldMatrix.m[3][0], cameraWorldMatrix.m[3][1], cameraWorldMatrix.m[3][2] };

    // パーティーモード：カメラ前方の平面上のランダムな位置（＝画面全体）から発生させる
    if (enablePartyEmit) {
        constexpr int kSpawnCountPerFrame = 6;
        constexpr float kEmitDistance = 15.0f;   // カメラからの発生距離
        constexpr float kEmitHalfWidth = 12.0f;  // 画面の横方向の広がり
        constexpr float kEmitHalfHeight = 7.0f;  // 画面の縦方向の広がり

        for (int i = 0; i < kSpawnCountPerFrame && particles_.size() < kMaxParticles; ++i) {
            const float offsetX = Random(-kEmitHalfWidth, kEmitHalfWidth);
            const float offsetY = Random(-kEmitHalfHeight, kEmitHalfHeight);

            Vector3 emitPosition = {
                cameraPosition.x + cameraForward.x * kEmitDistance + cameraRight.x * offsetX + cameraUp.x * offsetY,
                cameraPosition.y + cameraForward.y * kEmitDistance + cameraRight.y * offsetX + cameraUp.y * offsetY,
                cameraPosition.z + cameraForward.z * kEmitDistance + cameraRight.z * offsetX + cameraUp.z * offsetY
            };
            particles_.push_back(MakePartyParticle(emitPosition));
        }
    }

    // 粒の移動
    for (auto& p : particles_) {
        p.currentTime += kDeltaTime;
        p.velocity.y -= p.gravity * kDeltaTime;
        if (p.drag > 0.0f) {
            const float k = (std::max)(0.0f, 1.0f - p.drag * kDeltaTime);
            p.velocity = p.velocity.Multiply(k);
        }
        p.position = p.position.Add(p.velocity.Multiply(kDeltaTime));
        if (p.wobble != 0.0f) {
            const float t = p.currentTime * 3.0f + p.wobblePhase;
            p.position.x += std::sin(t) * p.wobble * kDeltaTime;
            p.position.z += std::cos(t * 0.7f) * p.wobble * kDeltaTime;
        }
    }

    // 寿命が来た粒を消す。花火のロケットは、消える位置から火花に弾ける
    std::vector<Particle> sparks;
    for (const auto& p : particles_) {
        if (p.currentTime >= p.lifeTime && p.rocket) {
            SpawnSparks(p, sparks);
        }
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
            [](const Particle& p) { return p.currentTime >= p.lifeTime; }),
        particles_.end());
    for (const auto& s : sparks) {
        if (particles_.size() >= kMaxParticles) {
            break;
        }
        particles_.push_back(s);
    }

    // 描画用：通常ブレンドの粒は遠い順に並べ、そのあとに加算ブレンドの粒を並べる
    std::vector<std::pair<float, const Particle*>> normalList;
    std::vector<const Particle*> additiveList;
    normalList.reserve(particles_.size());
    additiveList.reserve(particles_.size());
    for (const auto& p : particles_) {
        if (p.additive) {
            additiveList.push_back(&p);
        }
        else {
            const Vector3 diff = p.position.Subtract(cameraPosition);
            normalList.emplace_back(diff.Dot(cameraForward), &p); // カメラ前方への距離
        }
    }
    std::sort(normalList.begin(), normalList.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    // カメラの回転成分だけを取り出したビルボード行列（平行移動は各パーティクルの位置を使う）
    cameraWorldMatrix.m[3][0] = 0.0f;
    cameraWorldMatrix.m[3][1] = 0.0f;
    cameraWorldMatrix.m[3][2] = 0.0f;
    const Matrix4x4 viewProjection = Multiply(view, projection);

    uint32_t index = 0;
    auto write = [&](const Particle& p) {
        const float t = (std::min)(p.currentTime / p.lifeTime, 1.0f); // 0(誕生)～1(消滅)
        const float size = p.sizeStart + (p.sizeEnd - p.sizeStart) * t;

        Matrix4x4 scaleMatrix = Matrix4x4::MakeScaleMatrix({ size, size, size });
        Matrix4x4 translateMatrix = Matrix4x4::MakeTranslateMatrix(p.position);
        Matrix4x4 world = Multiply(scaleMatrix, Multiply(cameraWorldMatrix, translateMatrix));

        instanceData_[index].WVP = Multiply(world, viewProjection);
        instanceData_[index].color = Lerp(p.colorStart, p.colorEnd, t);
        instanceData_[index].params = { static_cast<float>(p.shape), 0.0f, 0.0f, 0.0f };
        ++index;
        };

    for (const auto& entry : normalList) {
        write(*entry.second);
    }
    normalCount_ = index;
    for (const Particle* p : additiveList) {
        write(*p);
    }
    additiveCount_ = index - normalCount_;
}

void ParticleSystem::Draw()
{
    if (normalCount_ + additiveCount_ == 0) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootDescriptorTable(0, instanceSrvHandleGPU_);

    // 通常ブレンド（バッファの先頭から）
    if (normalCount_ > 0) {
        commandList->SetPipelineState(normalPipelineState_.Get());
        commandList->SetGraphicsRoot32BitConstant(1, 0, 0);
        commandList->DrawInstanced(6, normalCount_, 0, 0);
    }
    // 加算ブレンド（通常の粒の後ろから）
    if (additiveCount_ > 0) {
        commandList->SetPipelineState(additivePipelineState_.Get());
        commandList->SetGraphicsRoot32BitConstant(1, normalCount_, 0);
        commandList->DrawInstanced(6, additiveCount_, 0, 0);
    }
}
