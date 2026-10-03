#include "ModelManager.h"
#include "../3d/ModelLoader.h"

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

    ModelData modelData = (modelName == ModelLoader::kSphereTag)
        ? ModelLoader::GenerateSphere(16)
        : ModelLoader::LoadObj("resources", modelName);

    auto model = std::make_unique<Model>();
    model->Initialize(dxCommon_, textureManager_, modelData);

    Model* result = model.get();
    models_.emplace(modelName, std::move(model));
    return result;
}
