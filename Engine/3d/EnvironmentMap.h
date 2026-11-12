#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"

class DirectXCommon;
class SrvManager;
struct SkyConstants;

/// <summary>
/// 空の見た目（時間帯 TimeOfDay が毎フレーム作って渡す）。procedural が false なら従来のキューブマップを使う
/// </summary>
struct SkyParams
{
    bool procedural = false;
    Vector3 horizon = { 0.78f, 0.86f, 1.00f };   // 地平線の色（HDR）
    Vector3 zenith = { 0.16f, 0.36f, 0.85f };    // 真上の色
    Vector3 ground = { 0.30f, 0.27f, 0.24f };    // 地面側の色
    Vector3 toSun = { 0.5f, 0.65f, 0.45f };      // 太陽のある方向（正規化）
    Vector3 discColor = { 0.0f, 0.0f, 0.0f };    // 太陽の円盤の色（HDR。0なら描かない）
    Vector3 glowColor = { 0.0f, 0.0f, 0.0f };    // 太陽まわりのにじみ（夕焼けの赤み）
    // 空の演出（TimeOfDay が作る）
    float cloudCover = 0.0f;                     // 雲の量（0〜1）
    float starAmount = 0.0f;                     // 星の見え方（0〜1）
    float cloudOffsetX = 0.0f;                   // 雲の流れた量
    float cloudOffsetZ = 0.0f;
    float time = 0.0f;                           // 経過秒（星のまたたき用）
    Vector3 cloudLit = { 1.0f, 1.0f, 1.0f };     // 雲の明るい側の色
    Vector3 cloudShade = { 0.5f, 0.55f, 0.65f }; // 雲の暗い側の色
    Vector3 toMoon = { 0.0f, -1.0f, 0.0f };      // 月のある方向
    Vector3 moonColor = { 0.0f, 0.0f, 0.0f };    // 月の明るさ（HDR。0なら描かない）
};

/// <summary>
/// 環境キューブマップ（スカイボックス描画 と PBRの反射用）
/// resources/skybox.dds（キューブマップ）があればそれを使い、無ければ空をプログラムで生成する
/// </summary>
class EnvironmentMap
{
public:
    static EnvironmentMap& Instance();

    // 読み込み/生成してSRVを作る（SrvManager初期化後に1回呼ぶ）
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    // GPUリソースを解放する（デバイスの終了処理より前に呼ぶ。呼ばないと終了時にリーク扱いになる）
    void Finalize();

    // Object3d / Sprite 共通のルートシグネチャのキューブマップ用テーブルを設定する（各PreDrawが呼ぶ）
    void Bind(ID3D12GraphicsCommandList* commandList) const;

    // 背景としてスカイボックスを描く。3Dオブジェクトより先に呼ぶこと
    void DrawSkybox(ID3D12GraphicsCommandList* commandList, const Matrix4x4& view, const Matrix4x4& projection);

    // 時間帯の空を設定する（procedural=false を渡すと、キューブマップの空に戻る）。
    // 3Dオブジェクト側の環境光・反射は、DirectionalLight の skyMode / skyHorizon... で別に渡す（TimeOfDay が両方に反映する）
    void SetSky(const SkyParams& sky) { sky_ = sky; }
    const SkyParams& GetSky() const { return sky_; }

    bool skyboxVisible = true; // falseなら背景は描かない（反射は使われる）

private:
    EnvironmentMap() = default;

    void CreateSkyboxPipeline(DirectXCommon* dxCommon);

    Microsoft::WRL::ComPtr<ID3D12Resource> cubeResource_;
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle_{};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> skyRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> skyPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> skyConstantResource_;
    SkyConstants* skyConstants_ = nullptr;
    SkyParams sky_;
};
