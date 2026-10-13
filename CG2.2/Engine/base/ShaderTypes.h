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
    float padding[2];
    Matrix4x4 uvTransform; // UV変換行列
};

struct TransformationMatrix
{
    Matrix4x4 WVP;
    Matrix4x4 World;
    Matrix4x4 WorldInverseTranspose; // 法線用（非均一スケールでも法線が正しく向くようにする）
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
    float padding0;

    // 配置したライト（ポイント/スポット）の数
    int32_t lightCount;
    float padding1[3];
    LightData lights[kMaxLights];
};
