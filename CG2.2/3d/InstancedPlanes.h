#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

#include "../math/Matrix4x4.h"
#include "../math/Transform.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

class DirectXCommon;
class SrvManager;
class TextureManager;

/// <summary>
/// 板ポリ(四角形)を大量に描くための共通部分（ルートシグネチャ・PSO・1枚ぶんの頂点）
/// InstancedPlanes が全員で共有する。Frameworkが1つだけ持つ
/// </summary>
class InstancedPlaneCommon
{
public:
    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, SrvManager* srvManager);

    // 板ポリ描画の前に1回呼ぶ（パイプラインと頂点バッファを設定する）
    void PreDraw();

    DirectXCommon* GetDxCommon() const { return dxCommon_; }
    TextureManager* GetTextureManager() const { return textureManager_; }
    SrvManager* GetSrvManager() const { return srvManager_; }

private:
    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
};

/// <summary>
/// 同じテクスチャの板ポリを count 枚まとめて、DrawInstanced 1回で描く
/// 1枚ごとの位置は「基準位置 + spacing * 番号」。WVPと色を StructuredBuffer に入れて SV_InstanceID で読む
/// ライティングなし・テクスチャ付き
/// </summary>
class InstancedPlanes
{
public:
    static constexpr uint32_t kMaxInstances = 256;

    // エディタで編集する設定（コピーして保存・復元できる）
    struct Settings
    {
        int count = 10;
        Transform base{ { 2.0f, 2.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } }; // 1枚目の大きさ・向き・位置
        Vector3 spacing{ 2.5f, 0.0f, 0.0f };     // 1枚ごとにずらす量（位置）
        Vector3 rotateStep{ 0.0f, 0.0f, 0.0f };  // 1枚ごとに足す回転（ラジアン）
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    ~InstancedPlanes();

    void Initialize(InstancedPlaneCommon* common, const std::string& texturePath);
    void SetTexture(const std::string& texturePath);

    // 全ての板のWVPを計算して StructuredBuffer に書き込む
    void Update(const Matrix4x4& view, const Matrix4x4& projection);
    void Draw();

    Settings& GetSettings() { return settings_; }
    const Settings& GetSettings() const { return settings_; }

    // i枚目のTransform（ピッキングや選択枠の表示に使う）
    Transform GetInstanceTransform(int index) const;
    int GetDrawCount() const;

private:
    InstancedPlaneCommon* common_ = nullptr;
    uint32_t textureHandle_ = 0;

    // GPUに渡す1枚ぶんのデータ（HLSL側の PlaneForGPU と同じ並び）
    struct PlaneForGPU
    {
        Matrix4x4 WVP;
        Vector4 color;
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
    PlaneForGPU* instanceData_ = nullptr;
    uint32_t srvIndex_ = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE instanceSrvHandleGPU_{};

    Settings settings_;
};
