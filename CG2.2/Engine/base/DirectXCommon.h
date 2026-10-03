#pragma once
#include <Windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <cstdint>
#include <string>
#include <vector>

#include "DirectXTex/DirectXTex.h"
#include "WinApp.h"

/// <summary>
/// DirectX12の共通部分（デバイス・スワップチェーン・コマンド・ヒープ・フェンス・DXC）と
/// リソース作成ヘルパーをまとめたクラス
///
/// ■コマンドリストの状態について
///   Initialize後、およびPostDraw後は常に「記録中(Reset済み)」の状態にしてある。
///   そのため、初期化中やフレームの頭でのテクスチャアップロード等は
///   そのままGetCommandList()に積めばよい。
///   初期化中に積んだアップロードは ExecuteAndWait() で確定させること。
/// </summary>
class DirectXCommon
{
public:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    static constexpr uint32_t kSwapChainBufferCount = 2;
    static constexpr uint32_t kSrvHeapSize = 128;
    static constexpr DXGI_FORMAT kRtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    static constexpr DXGI_FORMAT kDsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    void Initialize(WinApp* winApp);
    void Finalize();

    // 描画開始（バリア・RTV/DSV設定・クリア・ビューポート・SRVヒープ設定）
    void PreDraw();
    // 描画終了（バリア・コマンド実行・Present・GPU待ち・コマンドリストのReset）
    void PostDraw();

    //--------------------------------------------------
    // エディタ表示用（ゲーム画面をオフスクリーンに描いてImGuiのパネルに貼る）
    // 流れ: BeginSceneRender → シーン描画 → EndSceneRender → BeginBackBuffer → ImGui描画 → PostDraw
    //--------------------------------------------------
    void BeginSceneRender();  // ゲーム画面(オフスクリーン)への描画開始（クリア・ビューポート設定込み）
    void EndSceneRender();    // 描画終了（ImGuiでテクスチャとして読める状態にする）
    void BeginBackBuffer();   // 画面(バックバッファ)への描画開始（ImGui用。深度なし）
    // オフスクリーンのSRVを指定のディスクリプタ位置に作る（SrvManagerで確保した位置を渡す）
    void CreateOffscreenSrv(D3D12_CPU_DESCRIPTOR_HANDLE destHandle);

    // 積んだコマンドを実行してGPU完了まで待ち、コマンドリストを再度記録中に戻す
    void ExecuteAndWait();

    //--------------------------------------------------
    // リソース作成ヘルパー
    //--------------------------------------------------
    ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);
    ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
    ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);
    // アップロード用の中間リソースはこのクラスが保持し、GPU完了後に自動で解放する
    void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);
    ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

    static DirectX::ScratchImage LoadTexture(const std::string& filepath);

    static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* heap, uint32_t descriptorSize, uint32_t index);
    static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* heap, uint32_t descriptorSize, uint32_t index);

    D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index) const;

    //--------------------------------------------------
    // ゲッター
    //--------------------------------------------------
    ID3D12Device* GetDevice() const { return device_.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
    ID3D12DescriptorHeap* GetSrvDescriptorHeap() const { return srvDescriptorHeap_.Get(); }
    uint32_t GetDescriptorSizeSRV() const { return descriptorSizeSRV_; }

private:
    void InitializeDevice();
    void InitializeCommand();
    void InitializeSwapChain();
    void InitializeDescriptorHeaps();
    void InitializeRenderTargetViews();
    void InitializeDepthStencil();
    void InitializeOffscreenTarget();
    void InitializeFence();
    void InitializeViewportScissor();
    void InitializeDxc();

    void WaitForGPU();
    void ResetCommandList();

    WinApp* winApp_ = nullptr;

    ComPtr<ID3D12Debug1> debugController_;
    ComPtr<IDXGIFactory7> dxgiFactory_;
    ComPtr<ID3D12Device> device_;

    ComPtr<ID3D12CommandQueue> commandQueue_;
    ComPtr<ID3D12CommandAllocator> commandAllocator_;
    ComPtr<ID3D12GraphicsCommandList> commandList_;

    ComPtr<IDXGISwapChain4> swapChain_;
    ComPtr<ID3D12Resource> swapChainResources_[kSwapChainBufferCount];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[kSwapChainBufferCount]{};

    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
    ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
    ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;
    uint32_t descriptorSizeRTV_ = 0;
    uint32_t descriptorSizeSRV_ = 0;
    uint32_t descriptorSizeDSV_ = 0;

    ComPtr<ID3D12Resource> depthStencilResource_;

    // ゲーム画面用のオフスクリーンレンダーターゲット（RTVはRTVヒープの最後の1枠）
    ComPtr<ID3D12Resource> offscreenResource_;
    D3D12_CPU_DESCRIPTOR_HANDLE offscreenRtvHandle_{};

    ComPtr<ID3D12Fence> fence_;
    uint64_t fenceValue_ = 0;
    HANDLE fenceEvent_ = nullptr;

    D3D12_VIEWPORT viewport_{};
    D3D12_RECT scissorRect_{};

    ComPtr<IDxcUtils> dxcUtils_;
    ComPtr<IDxcCompiler3> dxcCompiler_;
    ComPtr<IDxcIncludeHandler> includeHandler_;

    // テクスチャアップロードの中間リソース（GPU完了まで生存させる）
    std::vector<ComPtr<ID3D12Resource>> intermediateResources_;

    UINT backBufferIndex_ = 0;
};
