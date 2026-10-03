#include "Framework.h"
#include "../base/Logger.h"
#include "../base/CrashHandler.h"
#include "../editor/EditorLayout.h"

void Framework::Initialize()
{
    winApp_.Initialize(L"CG2");
    dxCommon_.Initialize(&winApp_);

    input_.Initialize(&winApp_);
    audio_.Initialize();

    srvManager_.Initialize(&dxCommon_);
    textureManager_.Initialize(&dxCommon_, &srvManager_);
    modelManager_.Initialize(&dxCommon_, &textureManager_);
    object3dCommon_.Initialize(&dxCommon_, &textureManager_, &modelManager_);
    spriteCommon_.Initialize(&dxCommon_, &textureManager_);
    particleSystem_.Initialize(&dxCommon_, &srvManager_);
    instancedPlaneCommon_.Initialize(&dxCommon_, &textureManager_, &srvManager_);

#ifdef USE_IMGUI
    // ゲーム画面(オフスクリーン)をImGuiのテクスチャとして使うためのSRV
    const uint32_t sceneSrvIndex = srvManager_.Allocate();
    dxCommon_.CreateOffscreenSrv(srvManager_.GetCPUDescriptorHandle(sceneSrvIndex));
    sceneTextureHandle_ = srvManager_.GetGPUDescriptorHandle(sceneSrvIndex);
#endif
    imguiManager_.Initialize(&winApp_, &dxCommon_);

    // シーンに渡すエンジン機能の一覧
    context_.winApp = &winApp_;
    context_.dxCommon = &dxCommon_;
    context_.input = &input_;
    context_.audio = &audio_;
    context_.srvManager = &srvManager_;
    context_.textureManager = &textureManager_;
    context_.modelManager = &modelManager_;
    context_.object3dCommon = &object3dCommon_;
    context_.spriteCommon = &spriteCommon_;
    context_.particleSystem = &particleSystem_;
    context_.instancedPlaneCommon = &instancedPlaneCommon_;
    context_.sceneManager = &sceneManager_;

    sceneManager_.Initialize(&context_);
}

void Framework::Finalize()
{
    sceneManager_.Finalize(); // GPU使用中のリソースを持つシーンを先に破棄する

    dxCommon_.Finalize();
    imguiManager_.Finalize();
    Logger::Log("Application End");

    audio_.Finalize();
    input_.Finalize();
    winApp_.Finalize();
}

void Framework::Run()
{
    CrashHandler::Install();
    Logger::Initialize();
    Logger::Log("Application Start");

    Initialize();

    while (true) {
        if (winApp_.ProcessMessage()) {
            break;
        }

        input_.Update();
        audio_.Update();

        Update();               // Game側の処理
        sceneManager_.Update(); // シーン切り替えの反映 → シーンのUpdate

        imguiManager_.Begin();
        DrawImGui();
#ifdef USE_IMGUI
        EditorLayout::DrawViewport(sceneTextureHandle_);
#endif
        sceneManager_.DrawImGui();
        imguiManager_.End();

#ifdef USE_IMGUI
        // エディタ表示：ゲーム画面をオフスクリーンに描く → 画面にはImGuiだけを描く
        dxCommon_.BeginSceneRender();
        sceneManager_.Draw();
        dxCommon_.EndSceneRender();

        dxCommon_.BeginBackBuffer();
        imguiManager_.Draw();
        dxCommon_.PostDraw();
#else
        // ImGuiなし：ゲーム画面をそのままウィンドウに描く
        dxCommon_.PreDraw();
        sceneManager_.Draw();
        dxCommon_.PostDraw();
#endif
    }

    Finalize();
    Logger::Finalize();
}
