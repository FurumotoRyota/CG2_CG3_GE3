#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "../3d/VertexData.h"

struct MaterialData
{
    std::string textureFilePath;
};

// 1メッシュぶんの頂点データ（objの o/g、または usemtl の切り替わりで区切られる）
struct MeshData
{
    std::vector<VertexData> vertices;
    std::string materialName; // このメッシュが使うマテリアル名（MaterialDataのキー）
};

struct ModelData
{
    std::vector<MeshData> meshes;
    std::unordered_map<std::string, MaterialData> materials; // マテリアル名 -> テクスチャ情報
};
