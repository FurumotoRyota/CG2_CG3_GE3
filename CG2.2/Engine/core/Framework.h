#pragma once
#include "../base/WinApp.h"
#include "../base/FrameRateController.h"
#include "../base/DirectXCommon.h"
#include "../base/SrvManager.h"
#include "../base/TextureManager.h"
#include "../base/ImGuiManager.h"
#include "../3d/ModelManager.h"
#include "../3d/Object3dCommon.h"
#include "../3d/InstancedPlanes.h"
#include "../2d/SpriteCommon.h"
#include "../input/Input.h"
#include "../audio/Audio.h"
#include "../particle/ParticleSystem.h"
#include "../scene/SceneContext.h"
#include "../scene/SceneManager.h"

/// <summary>
/// エンジン全体の土台。全システムを所有し、メインループ(Run)を回す
/// ゲームごとの処理は派生クラス(Game)の Initialize / Update / DrawImGui で書く
/// </summary>
class Framework
{
public:
    virtual ~Framework() = default;

    // 初期化 → メインループ → 終了処理
    void Run();

protected:
    // 派生クラスでオーバーライドする場合は、先頭で Framework::Initialize() を呼ぶこと
    virtual void Initialize();
    virtual void Finalize();

    // 毎フレーム、シーンのUpdateより前に呼ばれる（シーンをまたぐ処理用）
    virtual void Update() {}
    // ImGui描画（Begin/End は Framework 側で行う）
    virtual void DrawImGui() {}

    WinApp winApp_;
    FrameRateController frameRate_;
    DirectXCommon dxCommon_;
    Input input_;
    Audio audio_;
    SrvManager srvManager_;
    TextureManager textureManager_;
    ModelManager modelManager_;
    Object3dCommon object3dCommon_;
    SpriteCommon spriteCommon_;
    ParticleSystem particleSystem_;
    InstancedPlaneCommon instancedPlaneCommon_;
    ImGuiManager imguiManager_;
    SceneManager sceneManager_;

    SceneContext context_;

    // ゲーム画面(オフスクリーン)のSRV。ImGuiのViewportパネルで表示に使う
    D3D12_GPU_DESCRIPTOR_HANDLE sceneTextureHandle_{};
};
