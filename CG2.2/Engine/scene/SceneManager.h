#pragma once
#include <memory>

#include "Scene.h"

/// <summary>
/// 現在のシーンの保持と切り替え
/// ChangeScene() は予約だけで、実際の切り替えは次フレームの頭（Update内）で行う
/// </summary>
class SceneManager
{
public:
    void Initialize(SceneContext* context);
    void Finalize();

    void ChangeScene(std::unique_ptr<Scene> next);

    // 使い方: sceneManager->ChangeScene<GameScene>();
    template <class T>
    void ChangeScene() { ChangeScene(std::make_unique<T>()); }

    void Update();
    void DrawImGui();
    void Draw();

private:
    SceneContext* ctx_ = nullptr;
    std::unique_ptr<Scene> current_;
    std::unique_ptr<Scene> next_;
};
