#include "DirectXCommon.h"
#include "Logger.h"
#include "StringUtility.h"
#include "DirectXTex/d3dx12.h"

#include <cassert>
#include <format>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

using StringUtility::ConvertString;

//==================================================
// 初期化・終了
//==================================================
void DirectXCommon::Initialize(WinApp* winApp)
{
    assert(winApp);
    winApp_ = winApp;

    InitializeDevice();
    InitializeCommand();
    InitializeSwapChain();
    InitializeDescriptorHeaps();
    InitializeRenderTargetViews();
    InitializeDepthStencil();
    InitializeOffscreenTarget();
    InitializeSceneTarget();
    InitializeFence();
    InitializeGpuTimer();
    InitializeViewportScissor();
    InitializeDxc();
}

void DirectXCommon::Finalize()
{
    // 最後のGPU処理が終わってから解放できるようにする
    if (commandQueue_ && fence_) {
        WaitForGPU();
    }
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

void DirectXCommon::InitializeDevice()
{
    HRESULT hr;

#ifdef _DEBUG
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController_.GetAddressOf())))) {
        debugController_->EnableDebugLayer();
        debugController_->SetEnableGPUBasedValidation(FALSE); // 极端に重いため無効化（デバッグレイヤー自体は有効）
        Logger::Log("D3D12 Debug Layer Enabled");
    }
#endif

    hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<IDXGIAdapter4> useAdapter;
    for (UINT i = 0;
        dxgiFactory_->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(useAdapter.GetAddressOf())) != DXGI_ERROR_NOT_FOUND;
        ++i)
    {
        DXGI_ADAPTER_DESC3 adapterDesc{};
        hr = useAdapter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr));

        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            Logger::Log(ConvertString(std::format(L"Use Adapter: {}", adapterDesc.Description)));
            break;
        }
        useAdapter.Reset();
    }
    assert(useAdapter != nullptr);

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0
    };
    const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };

    for (size_t i = 0; i < _countof(featureLevels); ++i) {
        hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(device_.GetAddressOf()));
        if (SUCCEEDED(hr)) {
            Logger::Log(std::format("Feature Level: {}", featureLevelStrings[i]));
            break;
        }
    }
    assert(device_ != nullptr);
    Logger::Log("Complete create D3D12 Device");

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf())))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

        D3D12_MESSAGE_ID denyIds[] = {
            D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
        };
        D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };

        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = _countof(denyIds);
        filter.DenyList.pIDList = denyIds;
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;
        infoQueue->PushStorageFilter(&filter);
    }
#endif
}

void DirectXCommon::InitializeCommand()
{
    HRESULT hr;

    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
    hr = device_->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    // 作成直後は記録中の状態
    hr = device_->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr,
        IID_PPV_ARGS(commandList_.GetAddressOf()));
    assert(SUCCEEDED(hr));
}

void DirectXCommon::InitializeSwapChain()
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = WinApp::kClientWidth;
    swapChainDesc.Height = WinApp::kClientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = kSwapChainBufferCount;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HRESULT hr = dxgiFactory_->CreateSwapChainForHwnd(
        commandQueue_.Get(),
        winApp_->GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        reinterpret_cast<IDXGISwapChain1**>(swapChain_.GetAddressOf()));
    assert(SUCCEEDED(hr));
}

void DirectXCommon::InitializeDescriptorHeaps()
{
    rtvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kSwapChainBufferCount + 2, false);
    srvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvHeapSize, true);
    dsvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

    descriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    descriptorSizeRTV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    descriptorSizeDSV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
}

void DirectXCommon::InitializeRenderTargetViews()
{
    for (UINT i = 0; i < kSwapChainBufferCount; ++i) {
        HRESULT hr = swapChain_->GetBuffer(i, IID_PPV_ARGS(swapChainResources_[i].GetAddressOf()));
        assert(SUCCEEDED(hr));

        rtvHandles_[i] = GetCPUDescriptorHandle(rtvDescriptorHeap_.Get(), descriptorSizeRTV_, i);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = kRtvFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device_->CreateRenderTargetView(swapChainResources_[i].Get(), &rtvDesc, rtvHandles_[i]);
    }
}

void DirectXCommon::InitializeDepthStencil()
{
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = WinApp::kGameWidth;
    resourceDesc.Height = WinApp::kGameHeight;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS; // 深度を描く(D24_S8)のと、ポストエフェクトで読む(R24_X8)の両方に使う
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = kDsvFormat;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue,
        IID_PPV_ARGS(depthStencilResource_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = kDsvFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(
        depthStencilResource_.Get(), &dsvDesc,
        dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
}

void DirectXCommon::InitializeOffscreenTarget()
{
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = WinApp::kGameWidth;
    resourceDesc.Height = WinApp::kGameHeight;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = kRtvFormat;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    // クリア色はBeginSceneRenderのClearRenderTargetViewと同じ値にする（違うとデバッグレイヤーが警告する）
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kRtvFormat;
    clearValue.Color[0] = 0.1f;
    clearValue.Color[1] = 0.25f;
    clearValue.Color[2] = 0.5f;
    clearValue.Color[3] = 1.0f;

    // 初期状態は「ImGuiが読める状態」。BeginSceneRenderでRENDER_TARGETへ遷移する
    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(offscreenResource_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    offscreenRtvHandle_ = GetCPUDescriptorHandle(rtvDescriptorHeap_.Get(), descriptorSizeRTV_, kSwapChainBufferCount);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = kRtvFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(offscreenResource_.Get(), &rtvDesc, offscreenRtvHandle_);
}

void DirectXCommon::InitializeSceneTarget()
{
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = WinApp::kGameWidth;
    resourceDesc.Height = WinApp::kGameHeight;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = kSceneFormat;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    // クリア色は BeginSceneRender の ClearRenderTargetView と同じ値にする
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = kSceneFormat;
    clearValue.Color[0] = 0.1f;
    clearValue.Color[1] = 0.25f;
    clearValue.Color[2] = 0.5f;
    clearValue.Color[3] = 1.0f;

    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(sceneResource_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    sceneRtvHandle_ = GetCPUDescriptorHandle(rtvDescriptorHeap_.Get(), descriptorSizeRTV_, kSwapChainBufferCount + 1);
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = kSceneFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(sceneResource_.Get(), &rtvDesc, sceneRtvHandle_);
}

void DirectXCommon::CreateSceneSrv(D3D12_CPU_DESCRIPTOR_HANDLE destHandle)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = kSceneFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(sceneResource_.Get(), &srvDesc, destHandle);
}

void DirectXCommon::CreateDepthSrv(D3D12_CPU_DESCRIPTOR_HANDLE destHandle)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; // 深度の部分だけを読む
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(depthStencilResource_.Get(), &srvDesc, destHandle);
}

void DirectXCommon::CreateOffscreenSrv(D3D12_CPU_DESCRIPTOR_HANDLE destHandle)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = kRtvFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(offscreenResource_.Get(), &srvDesc, destHandle);
}

void DirectXCommon::InitializeFence()
{
    HRESULT hr = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent_ != nullptr);
}

void DirectXCommon::InitializeGpuTimer()
{
    // タイムスタンプを入れる箱と、結果をCPUで読むための箱
    D3D12_QUERY_HEAP_DESC qd{};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = kMaxGpuMarks;
    HRESULT hr = device_->CreateQueryHeap(&qd, IID_PPV_ARGS(queryHeap_.GetAddressOf()));
    if (FAILED(hr)) {
        queryHeap_.Reset();
        return; // 計測できない環境では何もしない
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = sizeof(uint64_t) * kMaxGpuMarks;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr = device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(queryReadback_.GetAddressOf()));
    if (FAILED(hr)) {
        queryHeap_.Reset();
        return;
    }
    commandQueue_->GetTimestampFrequency(&gpuFrequency_);
    GpuMark("frame");
}

void DirectXCommon::GpuMark(const char* name)
{
    if (!queryHeap_ || gpuMarkCount_ >= kMaxGpuMarks) {
        return;
    }
    commandList_->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, gpuMarkCount_);
    gpuMarkNames_[gpuMarkCount_] = name;
    ++gpuMarkCount_;
}

void DirectXCommon::ReadGpuTimings()
{
    // 印と印の間のGPU時間を出す（GPUの処理が終わったあとに呼ぶ）
    gpuTimings_.clear();
    if (!queryReadback_ || gpuMarkCount_ < 2 || gpuFrequency_ == 0) {
        return;
    }
    D3D12_RANGE range{ 0, sizeof(uint64_t) * gpuMarkCount_ };
    uint64_t* data = nullptr;
    if (FAILED(queryReadback_->Map(0, &range, reinterpret_cast<void**>(&data)))) {
        return;
    }
    for (uint32_t i = 1; i < gpuMarkCount_; ++i) {
        const float ms = static_cast<float>(static_cast<double>(data[i] - data[i - 1]) * 1000.0 / static_cast<double>(gpuFrequency_));
        gpuTimings_.push_back({ gpuMarkNames_[i], ms });
    }
    D3D12_RANGE written{ 0, 0 };
    queryReadback_->Unmap(0, &written);
}

void DirectXCommon::InitializeViewportScissor()
{
    // シーン描画用（＝ゲーム解像度）
    viewport_.Width = static_cast<float>(WinApp::kGameWidth);
    viewport_.Height = static_cast<float>(WinApp::kGameHeight);
    viewport_.TopLeftX = 0.0f;
    viewport_.TopLeftY = 0.0f;
    viewport_.MinDepth = 0.0f;
    viewport_.MaxDepth = 1.0f;

    scissorRect_.left = 0;
    scissorRect_.top = 0;
    scissorRect_.right = WinApp::kGameWidth;
    scissorRect_.bottom = WinApp::kGameHeight;
}

void DirectXCommon::InitializeDxc()
{
    HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxcUtils_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxcCompiler_.GetAddressOf()));
    assert(SUCCEEDED(hr));

    hr = dxcUtils_->CreateDefaultIncludeHandler(includeHandler_.GetAddressOf());
    assert(SUCCEEDED(hr));
}

//==================================================
// 描画
//==================================================
void DirectXCommon::PreDraw()
{
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex_], FALSE, &dsvHandle);

    float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex_], clearColor, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
    commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}

void DirectXCommon::BeginSceneRender()
{
    D3D12_RESOURCE_BARRIER barriers[2]{};
    UINT count = 0;
    barriers[count].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[count].Transition.pResource = sceneResource_.Get();
    barriers[count].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[count].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[count].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ++count;
    if (depthReadable_) {
        barriers[count].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[count].Transition.pResource = depthStencilResource_.Get();
        barriers[count].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[count].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[count].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        ++count;
        depthReadable_ = false;
    }
    commandList_->ResourceBarrier(count, barriers);

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    commandList_->OMSetRenderTargets(1, &sceneRtvHandle_, FALSE, &dsvHandle);

    float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
    commandList_->ClearRenderTargetView(sceneRtvHandle_, clearColor, 0, nullptr);
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList_->RSSetViewports(1, &viewport_);
    commandList_->RSSetScissorRects(1, &scissorRect_);

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
    commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}

void DirectXCommon::EndSceneRender()
{
    // HDR画面と深度を、ポストエフェクトが読める状態にする
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = sceneResource_.Get();
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = depthStencilResource_.Get();
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_->ResourceBarrier(2, barriers);
    depthReadable_ = true;
}

void DirectXCommon::BeginPostTarget()
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
#ifdef USE_IMGUI
    // ImGuiのビューポートに貼るゲーム画面（オフスクリーン）へ書き出す
    barrier.Transition.pResource = offscreenResource_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
#else
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
    barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
#endif
    commandList_->ResourceBarrier(1, &barrier);
}

void DirectXCommon::EndPostTarget()
{
#ifdef USE_IMGUI
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = offscreenResource_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList_->ResourceBarrier(1, &barrier);
#endif
    // ImGuiなしのときは、バックバッファのまま PostDraw で Present の状態へ戻す
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetPostTargetRtv() const
{
#ifdef USE_IMGUI
    return offscreenRtvHandle_;
#else
    return rtvHandles_[backBufferIndex_];
#endif
}

void DirectXCommon::BeginBackBuffer()
{
    backBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &barrier);

    commandList_->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex_], FALSE, nullptr);

    float clearColor[] = { 0.08f, 0.08f, 0.09f, 1.0f };
    commandList_->ClearRenderTargetView(rtvHandles_[backBufferIndex_], clearColor, 0, nullptr);

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap_.Get() };
    commandList_->SetDescriptorHeaps(1, descriptorHeaps);
}

void DirectXCommon::PostDraw()
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = swapChainResources_[backBufferIndex_].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &barrier);

    GpuMark("imgui"); // ここまでが、ポスト処理のあとのImGui
    if (queryHeap_ && gpuMarkCount_ > 0) {
        commandList_->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, gpuMarkCount_, queryReadback_.Get(), 0);
    }

    HRESULT hr = commandList_->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);

    hr = swapChain_->Present(1, 0);
    assert(SUCCEEDED(hr));

    WaitForGPU();
    ReadGpuTimings();
    ResetCommandList();
}

void DirectXCommon::ExecuteAndWait()
{
    HRESULT hr = commandList_->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);

    WaitForGPU();
    ResetCommandList();
}

void DirectXCommon::WaitForGPU()
{
    ++fenceValue_;
    HRESULT hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
    assert(SUCCEEDED(hr));

    if (fence_->GetCompletedValue() < fenceValue_) {
        hr = fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
        assert(SUCCEEDED(hr));
        WaitForSingleObject(fenceEvent_, INFINITE);
    }
}

void DirectXCommon::ResetCommandList()
{
    HRESULT hr = commandAllocator_->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
    assert(SUCCEEDED(hr));

    // GPU処理は完了済みなので中間リソースを解放してよい
    intermediateResources_.clear();

    // 計測の印は、新しいフレームの先頭からやり直す
    gpuMarkCount_ = 0;
    GpuMark("frame");
}

//==================================================
// リソース作成ヘルパー
//==================================================
DirectXCommon::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes)
{
    D3D12_HEAP_PROPERTIES uploadHeapProperties{};
    uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferResourceDesc{};
    bufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferResourceDesc.Width = sizeInBytes;
    bufferResourceDesc.Height = 1;
    bufferResourceDesc.DepthOrArraySize = 1;
    bufferResourceDesc.MipLevels = 1;
    bufferResourceDesc.SampleDesc.Count = 1;
    bufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> bufferResource;
    HRESULT hr = device_->CreateCommittedResource(
        &uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferResourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(bufferResource.GetAddressOf()));
    assert(SUCCEEDED(hr));
    return bufferResource;
}

DirectXCommon::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(
    D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = heapType;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(descriptorHeap.GetAddressOf()));
    assert(SUCCEEDED(hr));
    return descriptorHeap;
}

DirectXCommon::ComPtr<ID3D12Resource> DirectXCommon::CreateTextureResource(const DirectX::TexMetadata& metadata)
{
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension);
    resourceDesc.Width = UINT(metadata.width);
    resourceDesc.Height = UINT(metadata.height);
    resourceDesc.MipLevels = UINT16(metadata.mipLevels);
    resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
    resourceDesc.Format = metadata.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    ComPtr<ID3D12Resource> resource;
    HRESULT hr = device_->CreateCommittedResource(
        &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(resource.GetAddressOf()));
    if (FAILED(hr)) {
        return {};
    }
    return resource;
}

void DirectXCommon::UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages)
{
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    DirectX::PrepareUpload(device_.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
    ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(static_cast<size_t>(intermediateSize));

    UpdateSubresources(
        commandList_.Get(), texture, intermediateResource.Get(),
        0, 0, UINT(subresources.size()), subresources.data());

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList_->ResourceBarrier(1, &barrier);

    intermediateResources_.push_back(intermediateResource);
}

DirectXCommon::ComPtr<IDxcBlob> DirectXCommon::CompileShader(const std::wstring& filePath, const wchar_t* profile, const wchar_t* entry)
{
    Logger::Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile)));

    ComPtr<IDxcBlobEncoding> shaderSource;
    HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, shaderSource.GetAddressOf());
    assert(SUCCEEDED(hr));

    DxcBuffer shaderSourceBuffer{};
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E", entry,
        L"-T", profile,
        L"-Zi", L"-Qembed_debug",
        L"-O3", // 最適化する（-Od だとシェーダーが遅い。デバッグでステップ実行したいときだけ -Od に戻す）
        L"-Zpr",
    };

    ComPtr<IDxcResult> shaderResult;
    hr = dxcCompiler_->Compile(
        &shaderSourceBuffer, arguments, _countof(arguments), includeHandler_.Get(),
        IID_PPV_ARGS(shaderResult.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<IDxcBlobUtf8> shaderError;
    hr = shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(shaderError.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Logger::Log(shaderError->GetStringPointer());
        assert(false);
    }

    ComPtr<IDxcBlob> shaderBlob;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderBlob.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    Logger::Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile)));
    return shaderBlob;
}

DirectX::ScratchImage DirectXCommon::LoadTexture(const std::string& filepath)
{
    DirectX::ScratchImage image{};
    std::wstring filePathW = ConvertString(filepath);
    HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    assert(SUCCEEDED(hr));

    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(), image.GetImageCount(), image.GetMetadata(),
        DirectX::TEX_FILTER_SRGB, 0, mipImages);
    assert(SUCCEEDED(hr));

    return mipImages;
}

//==================================================
// ディスクリプタハンドル
//==================================================
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(ID3D12DescriptorHeap* heap, uint32_t descriptorSize, uint32_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorSize) * index;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(ID3D12DescriptorHeap* heap, uint32_t descriptorSize, uint32_t index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(descriptorSize) * index;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetSRVCPUDescriptorHandle(uint32_t index) const
{
    return GetCPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, index);
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetSRVGPUDescriptorHandle(uint32_t index) const
{
    return GetGPUDescriptorHandle(srvDescriptorHeap_.Get(), descriptorSizeSRV_, index);
}
