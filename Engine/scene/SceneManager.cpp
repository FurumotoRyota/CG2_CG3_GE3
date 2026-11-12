#include "SceneManager.h"
#include "../base/DirectXCommon.h"
#include "../base/Logger.h"

#include <cassert>

void SceneManager::Initialize(SceneContext* context)
{
    assert(context);
    ctx_ = context;
}

void SceneManager::Finalize()
{
    if (current_) {
        current_->Finalize();
        current_.reset();
    }
    next_.reset();
}

void SceneManager::ChangeScene(std::unique_ptr<Scene> next)
{
    assert(next);
    next_ = std::move(next);
}

void SceneManager::Update()
{
    if (next_) {
        // 前フレームのPostDrawでGPU完了を待っているので、旧シーンのリソースはここで解放して安全
        if (current_) {
            current_->Finalize();
        }
        current_ = std::move(next_);
        current_->SetContext(ctx_);
        current_->Initialize();
        // 新シーンの初期化で積んだテクスチャアップロード等をここで確定させる
        ctx_->dxCommon->ExecuteAndWait();
        Logger::Log("Scene changed");
    }

    if (current_) {
        current_->Update();
    }
}

void SceneManager::DrawImGui()
{
    if (current_) {
        current_->DrawImGui();
    }
}

void SceneManager::Draw()
{
    if (current_) {
        current_->Draw();
    }
}
