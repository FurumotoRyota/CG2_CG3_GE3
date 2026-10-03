#include "ImGuiManager.h"
#include "../base/WinApp.h"
#include "../base/DirectXCommon.h"

#include <cassert>

#ifdef USE_IMGUI
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#endif // USE_IMGUI

void ImGuiManager::Initialize([[maybe_unused]] WinApp* winApp, DirectXCommon* dxCommon)
{
    assert(dxCommon);
    dxCommon_ = dxCommon;

#ifdef USE_IMGUI
    assert(winApp);
    ID3D12DescriptorHeap* srvHeap = dxCommon_->GetSrvDescriptorHeap();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr; // パネル配置はコードで固定するのでiniは使わない
    ImGui_ImplWin32_Init(winApp->GetHwnd());
    // SRVヒープの0番をフォント用に使う（SrvManagerは1番から払い出す）
    ImGui_ImplDX12_Init(
        dxCommon_->GetDevice(),
        DirectXCommon::kSwapChainBufferCount,
        DirectXCommon::kRtvFormat,
        srvHeap,
        srvHeap->GetCPUDescriptorHandleForHeapStart(),
        srvHeap->GetGPUDescriptorHandleForHeapStart());
    ImGui::GetIO().Fonts->Build();
#endif // USE_IMGUI
}

void ImGuiManager::Finalize()
{
#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
#endif // USE_IMGUI
}

void ImGuiManager::Begin()
{
#ifdef USE_IMGUI
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
#endif // USE_IMGUI
}

void ImGuiManager::End()
{
#ifdef USE_IMGUI
    ImGui::Render();
#endif // USE_IMGUI
}

void ImGuiManager::Draw()
{
#ifdef USE_IMGUI
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon_->GetCommandList());
#endif // USE_IMGUI
}
