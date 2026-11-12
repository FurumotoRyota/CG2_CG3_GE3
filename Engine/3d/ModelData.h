#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../3d/SkinData.h"
#include "../3d/VertexData.h"
#include "../math/Matrix4x4.h"

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

// モデルのノード階層（assimpのaiNodeに対応）
struct Node
{
    Matrix4x4 localMatrix{ { {1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1} } };
    std::string name;
    std::vector<Node> children;
};

struct ModelData
{
    std::vector<MeshData> meshes;
    std::unordered_map<std::string, MaterialData> materials; // マテリアル名 -> テクスチャ情報
    Node rootNode; // ノード階層（各ノードのtransformは頂点に焼き込み済み。確認・表示用）
    std::shared_ptr<SkinData> skin; // 骨つきモデルのときだけ入る（骨・アニメーション・頂点ごとの骨の影響）
};
