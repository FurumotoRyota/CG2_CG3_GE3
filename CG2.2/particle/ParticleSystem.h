#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <random>
#include <vector>

#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"

class DirectXCommon;
class SrvManager;

/// <summary>
/// ビルボードパーティクルの生成・更新・描画（GPUインスタンシング）
/// 専用のルートシグネチャ/PSOを持つため、Draw() は Sprite など他の描画の後に呼ぶこと
/// </summary>
class ParticleSystem
{
public:
    static constexpr uint32_t kMaxParticles = 512;

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    // enableEmit が true の間、カメラ前方の平面上（＝画面全体）から虹色パーティクルを発生させる
    // 生存中のパーティクルは enableEmit に関わらず常に更新する
    void Update(const Matrix4x4& view, const Matrix4x4& projection, bool enableEmit);

    void Draw();

    // 生存中のパーティクルを全て消す（シーン切り替え時用）
    void Clear() { particles_.clear(); }

    size_t GetCount() const { return particles_.size(); }

private:
    struct Particle
    {
        Vector3 position;
        Vector3 velocity;
        Vector4 color;
        float lifeTime = 1.0f;
        float currentTime = 0.0f;
        float scale = 0.3f;
    };

    // GPUに渡す1パーティクルぶんのデータ（ワールド計算込みのWVPと色）
    struct ParticleForGPU
    {
        Matrix4x4 WVP;
        Vector4 color;
    };

    void CreatePipeline();
    void CreateResources(SrvManager* srvManager);
    Particle MakeRandomPartyParticle(const Vector3& emitPosition);

    DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
    ParticleForGPU* instanceData_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE instanceSrvHandleGPU_{};

    std::vector<Particle> particles_;
    std::mt19937 randomEngine_{ std::random_device{}() };
};
