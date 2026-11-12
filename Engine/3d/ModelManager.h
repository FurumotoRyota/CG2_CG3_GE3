#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "../3d/Model.h"

class DirectXCommon;
class TextureManager;

/// <summary>
/// モデルの読み込みとキャッシュ
/// 同じ名前のモデルは1度しか読み込まず、複数のObject3dで共有する
/// </summary>
class ModelManager
{
public:
    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager);

    // "resources"フォルダのobjファイル名、または ModelLoader::kSphereTag を指定する
    Model* Load(const std::string& modelName);

    // 手続き生成したModelDataを名前付きで登録する (既にある名前は既存モデルを返す)
    Model* Register(const std::string& name, const ModelData& data);

private:
    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    std::unordered_map<std::string, std::unique_ptr<Model>> models_;
};
