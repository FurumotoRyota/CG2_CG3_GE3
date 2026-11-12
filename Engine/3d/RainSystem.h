#pragma once
#include <d3d12.h>
#include <wrl/client.h>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"

class DirectXCommon;
struct RainConstants;

/// <summary>
/// 3Dの雨。雨粒の細長い筋を、カメラのまわりの空間に降らせる。地面には、着地した所の波紋としぶきを出す。
/// 位置はすべてGPUが「番号と時間」から計算するので、粒ごとのデータをCPUで持たない（1回の描画命令で全部描く）。
/// 建物や壁の後ろに回った雨は、深度テストで隠れる。HDRの画面に加算で重ねる。
///
/// 使い方: Initialize で作り、3Dオブジェクトを描いたあとに Draw を呼ぶ（RenderLook を使うなら RenderLook がやる）
/// </summary>
class RainSystem
{
public:
    struct Params
    {
        float intensity = 1.0f;     // 雨の量（0〜1）。0なら何も描かない
        float wind = 0.3f;          // 風（0〜1）。雨の傾き
        float windAngleDeg = 20.0f; // 風の向き（度。0で奥(+z)、90で右(+x)）
        Vector3 color = { 0.6f, 0.65f, 0.75f }; // 雨の色（HDR）
        float time = 0.0f;          // 経過秒
        float groundY = 0.0f;       // 地面の高さ（波紋を出す高さ）
        float fallSpeed = 16.0f;    // 落ちる速さ(m/s)
        float streakLength = 0.9f;  // 筋の長さ
        float streakWidth = 0.025f; // 筋の太さ
        float boxSize = 44.0f;      // 雨が降る範囲（XZの一辺）
        float boxHeight = 18.0f;    // 雨が降る高さ
        float rippleRadius = 0.45f; // 波紋の最大半径
        float ripplePeriod = 0.7f;  // 波紋が広がって消えるまで(秒)
    };

    void Initialize(DirectXCommon* dxCommon);
    void Finalize(); // デバイスの終了処理より前に呼ぶ

    // focus: 雨を降らせる中心（注目している地面の点）
    void Draw(ID3D12GraphicsCommandList* commandList, const Matrix4x4& view, const Matrix4x4& projection,
        const Vector3& focus, const Params& params);

private:
    static constexpr unsigned kMaxStreaks = 3500;
    static constexpr unsigned kMaxRipples = 450;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> streakPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ripplePso_;
    Microsoft::WRL::ComPtr<ID3D12Resource> constantResource_;
    RainConstants* constants_ = nullptr;
};
