#pragma once
#include <cstdint>
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"

// HLSL側の ConstantBuffer と対応する構造体（Object3d.PS.hlsl / Object3d.VS.hlsl）

struct Material
{
    Vector4 color;
    int32_t lightingMode; // 0:なし 1:Lambert 2:HalfLambert
    float alphaCutoff;     // テクスチャのα値がこれ未満のピクセルは描かない（discard）。0なら無効
    float shininess;       // 鏡面反射の鋭さ（大きいほど小さく鋭いハイライト）
    int32_t specularMode;  // 0:なし 1:Phong 2:Blinn-Phong 3:PBR(GGX)
    float roughness;       // PBR：表面の粗さ（0:鏡面 ～ 1:ざらざら）
    float metallic;        // PBR：金属度（0:非金属 ～ 1:金属）
    float receiveShadow;   // 影を受けるか（0.5以上で受ける）
    float padding;
    Matrix4x4 uvTransform; // UV変換行列
};

struct TransformationMatrix
{
    Matrix4x4 WVP;
    Matrix4x4 World;
    Matrix4x4 WorldInverseTranspose; // 法線用（非均一スケールでも法線が正しく向くようにする）
    Matrix4x4 LightWVP;              // 光から見た位置（影用）= World * ライトのビュー射影
};

// 配置できるライトの最大数
inline constexpr int kMaxLights = 16;

// ポイントライト / スポットライト1つぶん（Object3d.PS.hlsl の LightData と対応）
struct LightData
{
    Vector4 color;
    Vector3 position;
    float intensity;        // 0 なら消灯
    Vector3 direction;      // スポットライトの向き
    float range;            // 光が届く最大距離
    float decay;            // 距離による減衰の強さ
    float cosAngle;         // スポット：光円錐の外側の角度のcos
    float cosFalloffStart;  // スポット：減衰を始める角度のcos
    int32_t type;           // 0:ポイント 1:スポット
};

// ライト用の定数バッファ（Object3d.PS.hlsl の LightBuffer と対応）
// 名前は DirectionalLight のままだが、配置したライトの配列・カメラ位置も含む
// intensity が 0 のライトは無効
struct DirectionalLight
{
    // ディレクショナルライト
    Vector4 color;
    Vector3 direction;
    float intensity;

    // 視線ベクトル（鏡面反射）用のカメラ位置
    Vector3 cameraPosition;
    float shadowRange;    // 影の範囲の半径（ワールド）。やわらかい影のぼかし幅の計算に使う（0ならぼかさない）

    // 配置したライト（ポイント/スポット）の数
    int32_t lightCount;
    float shadowBias;     // 影のバイアス（影のぎざぎざ/縞を防ぐ）
    float shadowStrength; // 影の濃さ（0～1）
    int32_t shadowEnabled; // 0:影なし 1:ディレクショナルライトの影
    float shadowSoftness;  // 太陽の見かけの大きさ（大きいほど、影の落ちる先がぼける）。0でくっきり
    float ambientSky;      // 空からの環境光の強さ（0なら従来どおり。PBRの環境反射の倍率にもなる：0.5で等倍）
    float groundAo;        // 壁の足もとの暗がり（0〜1。0なら無し）
    int32_t shadowQuality; // 0:3x3のぼかし 1:やわらかい影（距離でぼける）

    // 時間帯（TimeOfDay）が決める空の色と霧。skyMode が 0 なら従来どおり（キューブマップ・霧なし）
    Vector3 skyHorizon;    // 地平線の空の色（HDR）
    float fogDensity;      // 霧の濃さ（0なら霧なし）
    Vector3 skyZenith;     // 真上の空の色
    float fogStart;        // 霧が始まるカメラからの距離
    Vector3 skyGround;     // 地面側の色（下向きの映り込み）
    int32_t skyMode;       // 0:キューブマップ 1:空の色から計算（時間帯）
    Vector3 fogColor;      // 霧の色
    float skyPadding;

    // 雨で濡れた見た目（TimeOfDay / RenderLook が書く）
    float wetness;         // 濡れ具合（0〜1）。暗く・つやが強くなる
    float puddleAmount;    // 水たまりのできやすさ（0〜1。0で水たまりなし）
    float rainTime;        // 経過秒（水たまりの波紋用）
    float rainAmount;      // いま降っている雨の量（0〜1。水たまりの波紋の強さ）

    LightData lights[kMaxLights];
};
