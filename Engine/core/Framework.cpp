#include "Framework.h"
#include "../base/Logger.h"
#include "../base/CrashHandler.h"
#include "../editor/EditorLayout.h"
#include "../3d/EnvironmentMap.h"
#include "../3d/ShadowMap.h"
#include "FrameStats.h"

void Framework::Initialize()
{
    winApp_.Initialize(WindowTitle());
    frameRate_.Initialize(60); // fixed 60 FPS
    dxCommon_.Initialize(&winApp_);

    input_.Initialize(&winApp_);
    audio_.Initialize();

    srvManager_.Initialize(&dxCommon_);
    textureManager_.Initialize(&dxCommon_, &srvManager_);
    EnvironmentMap::Instance().Initialize(&dxCommon_, &srvManager_); // 環境キューブマップ（スカイボックス・PBR反射）
    ShadowMap::Instance().Initialize(&dxCommon_, &srvManager_);       // 影（シャドウマッピング）
    modelManager_.Initialize(&dxCommon_, &textureManager_);
    object3dCommon_.Initialize(&dxCommon_, &textureManager_, &modelManager_);
    spriteCommon_.Initialize(&dxCommon_, &textureManager_);
    particleSystem_.Initialize(&dxCommon_, &srvManager_, &textureManager_);
    instancedPlaneCommon_.Initialize(&dxCommon_, &textureManager_, &srvManager_);
    postProcess_.Initialize(&dxCommon_, &srvManager_); // ポストエフェクト（SSAO・ブルーム・トーンマップ）

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
    context_.postProcess = &postProcess_;

    sceneManager_.Initialize(&context_);
}

void Framework::Finalize()
{
    sceneManager_.Finalize(); // GPU使用中のリソースを持つシーンを先に破棄する

    postProcess_.Finalize();
    ShadowMap::Instance().Finalize();
    EnvironmentMap::Instance().Finalize(); // シングルトンはデバイスより後まで生き残るので、先にGPUリソースを解放する
    dxCommon_.Finalize();
    imguiManager_.Finalize();
    Logger::Log("Application End");

    audio_.Finalize();
    input_.Finalize();
    frameRate_.Finalize();
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
        FrameStats::BeginFrame(); // 前のフレームの描画量・時間をまとめて、数え直す

        input_.Update();
        audio_.Update();

        Update();               // Game側の処理
        sceneManager_.Update(); // シーン切り替えの反映 → シーンのUpdate

        {
            FrameStats::ScopedMs uiTimer(FrameStats::Cur().uiMs);
            imguiManager_.Begin();
            DrawImGui();
#ifdef USE_IMGUI
            EditorLayout::DrawViewport(sceneTextureHandle_);
#endif
            sceneManager_.DrawImGui();
            imguiManager_.End();
        }

        // シーンはHDRの画面に描き、ポストエフェクトで最終の画面へ書き出す
        dxCommon_.BeginSceneRender();
        sceneManager_.Draw();
        DrawOverlay();          // Game側の重ね描き（デバッグ描画など）
        dxCommon_.EndSceneRender();

        dxCommon_.BeginPostTarget();
        postProcess_.Execute(dxCommon_.GetCommandList(), dxCommon_.GetPostTargetRtv(), dxCommon_.GetViewport(), dxCommon_.GetScissorRect());
        dxCommon_.EndPostTarget();

#ifdef USE_IMGUI
        // エディタ表示：ゲーム画面(オフスクリーン)はImGuiのパネルに貼る → 画面にはImGuiだけを描く
        dxCommon_.BeginBackBuffer();
        imguiManager_.Draw();
#endif
        {
            FrameStats::ScopedMs presentTimer(FrameStats::Cur().presentMs);
            dxCommon_.PostDraw();
        }

        {
            FrameStats::ScopedMs sleepTimer(FrameStats::Cur().sleepMs);
            frameRate_.Update(); // wait until the frame time has passed (fixed FPS)
        }
    }

    Finalize();
    Logger::Finalize();
}
