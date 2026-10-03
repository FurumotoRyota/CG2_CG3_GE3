#pragma once
#include <cstdint>
#include <string>

#include "../3d/ModelData.h"

/// <summary>
/// モデルデータ(CPU側)の読み込み・生成
/// </summary>
namespace ModelLoader
{
    // objファイルではなくコードで頂点を生成する球のモデル名
    inline constexpr const char* kSphereTag = "sphere";

    // objファイルを読み込む（mtlのマルチマテリアル、o/g/usemtlによるマルチメッシュに対応）
    ModelData LoadObj(const std::string& directoryPath, const std::string& fileName);

    // 球を頂点計算で生成する。kSubdivision: 緯度・経度それぞれの分割数
    ModelData GenerateSphere(uint32_t kSubdivision);
}
