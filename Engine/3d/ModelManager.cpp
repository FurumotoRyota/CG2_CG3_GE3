#include "ModelManager.h"
#include "../3d/ModelLoader.h"
#include "../base/Logger.h"

#include <cassert>

void ModelManager::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager)
{
    assert(dxCommon);
    assert(textureManager);
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
}

Model* ModelManager::Load(const std::string& modelName)
{
    auto it = models_.find(modelName);
    if (it != models_.end()) {
        return it->second.get();
    }

    ModelData modelData;
    if (modelName == ModelLoader::kSphereTag) {
        modelData = ModelLoader::GenerateSphere(16);
    }
    else if (ModelLoader::LoadModelFile("resources", modelName, modelData)) {
        // assimpで読み込めた (obj / gltf / glb / fbx ...)
        Logger::Log("Model loaded: " + modelName + (modelData.skin ? " (skinned)" : "") + "\n");
    }
    else if (modelName.size() > 4 && modelName.compare(modelName.size() - 4, 4, ".fbx") == 0 &&
        ModelLoader::LoadModelFile("resources", modelName.substr(0, modelName.size() - 4) + ".gltf", modelData)) {
        // FBX が読めない assimp でも、同じ名前の .gltf（同じ骨・アニメーション）があれば、そちらを使う
        Logger::Log("Model fallback to gltf: " + modelName + "\n");
    }
    else {
        Logger::Log("Model load FAILED (assimp): " + modelName + "\n");
        // assimp未配置・読み込み失敗時は従来のobjローダーで読む
        modelData = ModelLoader::LoadObj("resources", modelName);
    }

    auto model = std::make_unique<Model>();
    model->Initialize(dxCommon_, textureManager_, modelData);

    Model* result = model.get();
    models_.emplace(modelName, std::move(model));
    return result;
}

Model* ModelManager::Register(const std::string& name, const ModelData& data)
{
    auto it = models_.find(name);
    if (it != models_.end()) {
        return it->second.get();
    }

    auto model = std::make_unique<Model>();
    model->Initialize(dxCommon_, textureManager_, data);

    Model* result = model.get();
    models_.emplace(name, std::move(model));
    return result;
}
