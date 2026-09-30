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
    float padding[2];      // 16byte alignment 調整
    Matrix4x4 uvTransform; // UV変換行列
};

struct TransformationMatrix
{
    Matrix4x4 WVP;
    Matrix4x4 World;
};

struct DirectionalLight
{
    Vector4 color;
    Vector3 direction;
    float intensity;
};
