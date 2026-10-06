#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <random>
#include <vector>

#include "ParticleEmitter.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"

class DirectXCommon;
class SrvManager;
class TextureManager;

/// <summary>
/// ビルボードパーティクルの生成・更新・描画（GPUインスタンシング）
///  ・ParticleEmitter（シーンに置く設定）から粒を発生させる
///  ・粒ごとのWVP/色/形は StructuredBuffer(t0) に書き、SV_InstanceID で読む
///  ・通常ブレンドと加算ブレンドの2回に分けて DrawInstanced する
/// 専用のルートシグネチャ/PSOを持つため、Draw() は Sprite など他の描画の後に呼ぶこと
/// </summary>
class ParticleSystem
{
public:
    static constexpr uint32_t kMaxParticles = 4096;

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, TextureManager* textureManager);

    // エミッターを1フレーム進めて、必要なら粒を発生させる（Update()より前に呼ぶ）
    void UpdateEmitter(ParticleEmitter& emitter);

    // enablePartyEmit が true の間、カメラ前方の平面上（＝画面全体）から虹色パーティクルを発生させる
    // 生存中のパーティクルは enablePartyEmit に関わらず常に更新する
    void Update(const Matrix4x4& view, const Matrix4x4& projection, bool enablePartyEmit);

    void Draw();

    // 生存中のパーティクルを全て消す（シーン切り替え時用）
    void Clear() { particles_.clear(); drawRuns_.clear(); normalCount_ = 0; additiveCount_ = 0; }

    size_t GetCount() const { return particles_.size(); }

private:
    struct Particle
    {
        Vector3 position{};
        Vector3 velocity{};
        Vector4 colorStart{ 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 colorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
        float lifeTime = 1.0f;
        float currentTime = 0.0f;
        float sizeStart = 0.3f;
        float sizeEnd = 0.3f;
        float gravity = 0.0f;
        float drag = 0.0f;
        float wobble = 0.0f;
        float wobblePhase = 0.0f;
        ParticleShape shape = ParticleShape::SoftCircle;
        int32_t texture = -1; // TextureManagerのハンドル。-1ならテクスチャなし(shapeで描く)
        bool additive = false;

        // 花火のロケット：消えるときに火花へ弾ける
        bool rocket = false;
        int sparkCount = 0;
        float sparkHue = 0.0f;
    };

    // GPUに渡す1パーティクルぶんのデータ（HLSL側の ParticleForGPU と同じ並び）
    struct ParticleForGPU
    {
        Matrix4x4 WVP;
        Vector4 color;
        Vector4 params; // x: 形(0:SoftCircle 1:Circle 2:Square)  y: テクスチャを使うか(1:使う)
    };

    void CreatePipeline();
    void CreateResources(SrvManager* srvManager);
    float Random(float minValue, float maxValue);
    Vector3 RandomDirectionInCone(const Vector3& direction, float spreadDeg);
    void SpawnFromEmitter(const ParticleEmitter& emitter);
    void SpawnSparks(const Particle& rocket, std::vector<Particle>& outParticles);
    Particle MakePartyParticle(const Vector3& emitPosition);

    // 同じブレンド・同じテクスチャが連続している粒のかたまり。1つにつき1回 DrawInstanced する
    struct DrawRun
    {
        bool additive = false;
        int32_t texture = -1;
        uint32_t offset = 0; // instanceData_ の何番目から
        uint32_t count = 0;
    };

    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    uint32_t dummyTexture_ = 0; // テクスチャなしの粒を描くときに、ルートシグネチャを満たすためだけに繋ぐ画像

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> normalPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> additivePipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
    ParticleForGPU* instanceData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE instanceSrvHandleGPU_{};

    std::vector<Particle> particles_;
    std::vector<DrawRun> drawRuns_;
    uint32_t normalCount_ = 0;   // instanceData_ の前半：通常ブレンド(遠い順)
    uint32_t additiveCount_ = 0; // instanceData_ の後半：加算ブレンド
    std::mt19937 randomEngine_{ std::random_device{}() };
};
