#include "GameObject.h"

void GameObject::Initialize(Object3dCommon* common, const std::string& modelName, const Vector3& translate)
{
    object_.Initialize(common, modelName);
    object_.GetTransform().Translate = translate;
}
