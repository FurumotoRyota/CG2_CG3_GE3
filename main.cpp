#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cassert>
#include <wrl/client.h>

#include <DbgHelp.h>
#include <strsafe.h>

// DirectX 12 関連
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>

// DXC 関連
#include <dxcapi.h>

// DirectXTex 追加
#include "externals/DirectXTex/DirectXTex.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "DirectXTex.lib")

#include "Vector3.h"
#include "Matrix4x4.h"

#ifdef USE_IMGUI
#include "externals//imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/imgui/imgui_impl_dx12.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // USE_IMGUI

#include "VertexData.h"
#include "externals/DirectXTex/d3dx12.h"
#include <vector>

#include<fstream>
#include <sstream>
#include <cstring>

#define DIRECTINPUT_VERSION 0x0800 // DirectInput 8.0 以降を使用することを指定, dinput.h より先に書くこと
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

using std::int32_t;
using std::uint32_t;
using Microsoft::WRL::ComPtr;

//==================================================
// 頂点データ用の構造体
// POSITION に float4 を渡す前提
//==================================================

struct Transform
{
    Vector3 Scale;
    Vector3 Rotate;
    Vector3 Translate;
};

struct Vertex
{
    Vector4 Position;
    Vector2 TexCoord;
    Vector3 Normal;
};

struct Material
{
    Vector4 color;
    int32_t enableLighting;
    float padding[3];      // 16byte alignment 調整
    Matrix4x4 uvTransform; // UV変換行列
};

struct TransformationMatrix
{
    Matrix4x4 WVP;
    Matrix4x4 World;
};

struct DirectionalLight
{
    Vector4 color;
    Vector3 direction;
    float intensity;
};

struct MaterialData
{
    std::string textureFilePath;
};

struct ModelData
{
    std::vector<VertexData> vertices;
    MaterialData material;
};

//==================================================
// リークチェッカー
//==================================================
struct D3DResourceLeakChecker
{
    ~D3DResourceLeakChecker()
    {
        ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(debug.GetAddressOf()))))
        {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

//==================================================
// デバッグ出力用ログ関数
//==================================================
void Log(const std::string& message) {
    OutputDebugStringA(message.c_str());
}

// ファイルにも書きたいとき用
void Log(std::ostream& os, const std::string& message) {
    os << message << std::endl;
    OutputDebugStringA((message + "\n").c_str());
}

template <class T>
void SafeRelease(T*& ptr) {
    if (ptr != nullptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

//==================================================
// キー入力判定関数
//==================================================
bool PushKey(const BYTE key[256], uint8_t keyNumber)
{
	return (key[keyNumber] & 0x80) != 0;
}

bool ReleaseKey(const BYTE key[256], uint8_t keyNumber)
{
	return (key[keyNumber] & 0x80) == 0;
}

bool TriggerKey(const BYTE key[256], uint8_t keyNumber, BYTE prevKey[256])
{
	return (key[keyNumber] & 0x80) != 0 && (prevKey[keyNumber] & 0x80) == 0;
}

bool ReleaseTriggerKey(const BYTE key[256], uint8_t keyNumber, BYTE prevKey[256])
{
	return (key[keyNumber] & 0x80) == 0 && (prevKey[keyNumber] & 0x80) != 0;
}


//==================================================
// クラッシュダンプ出力
// アプリがクラッシュした時に Dumps フォルダへ .dmp を出す
//==================================================
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
    SYSTEMTIME time{};
    GetLocalTime(&time);

    CreateDirectoryW(L"Dumps", nullptr);

    wchar_t filePath[MAX_PATH] = {};
    StringCchPrintfW(
        filePath,
        MAX_PATH,
        L"./Dumps/%04d-%02d-%02d-%02d%02d%02d.dmp",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond
    );

    HANDLE dumpFileHandle = CreateFileW(
        filePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (dumpFileHandle == INVALID_HANDLE_VALUE) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{};
    minidumpInformation.ThreadId = GetCurrentThreadId();
    minidumpInformation.ExceptionPointers = exception;
    minidumpInformation.ClientPointers = TRUE;

    MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        dumpFileHandle,
        MiniDumpNormal,
        &minidumpInformation,
        nullptr,
        nullptr
    );

    CloseHandle(dumpFileHandle);

    return EXCEPTION_EXECUTE_HANDLER;
}

//==================================================
// string -> wstring 変換
//==================================================
std::wstring ConvertString(const std::string& str) {
    if (str.empty()) {
        return std::wstring();
    }

    int sizeNeeded = MultiByteToWideChar(
        CP_UTF8,
        0,
        str.data(),
        static_cast<int>(str.size()),
        nullptr,
        0
    );

    if (sizeNeeded == 0) {
        return std::wstring();
    }

    std::wstring result(sizeNeeded, 0);

    MultiByteToWideChar(
        CP_UTF8,
        0,
        str.data(),
        static_cast<int>(str.size()),
        result.data(),
        sizeNeeded
    );

    return result;
}

//==================================================
// wstring -> string 変換
//==================================================
std::string ConvertString(const std::wstring& wstr) {
    if (wstr.empty()) {
        return std::string();
    }

    int sizeNeeded = WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.data(),
        static_cast<int>(wstr.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (sizeNeeded == 0) {
        return std::string();
    }

    std::string result(sizeNeeded, 0);

    WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.data(),
        static_cast<int>(wstr.size()),
        result.data(),
        sizeNeeded,
        nullptr,
        nullptr
    );

    return result;
}

//==================================================
// シェーダーコンパイル関数
//==================================================
ComPtr<IDxcBlob> CompileShader(
    const std::wstring& filePath,
    const wchar_t* profile,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler,
    std::ostream& logStream)
{
    Log(logStream, ConvertString(std::format(
        L"Begin CompileShader, path:{}, profile:{}",
        filePath,
        profile
    )));

    ComPtr<IDxcBlobEncoding> shaderSource;
    HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, shaderSource.GetAddressOf());
    assert(SUCCEEDED(hr));

    DxcBuffer shaderSourceBuffer{};
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E", L"main",
        L"-T", profile,
        L"-Zi", L"-Qembed_debug",
        L"-Od",
        L"-Zpr",
    };

    ComPtr<IDxcResult> shaderResult;
    hr = dxcCompiler->Compile(
        &shaderSourceBuffer,
        arguments,
        _countof(arguments),
        includeHandler,
        IID_PPV_ARGS(shaderResult.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    ComPtr<IDxcBlobUtf8> shaderError;
    hr = shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(shaderError.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Log(logStream, shaderError->GetStringPointer());
        assert(false);
    }

    ComPtr<IDxcBlob> shaderBlob;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderBlob.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    Log(logStream, ConvertString(std::format(
        L"Compile Succeeded, path:{}, profile:{}",
        filePath,
        profile
    )));

    return shaderBlob;
}

//==================================================
// バッファリソース作成関数
//==================================================
ComPtr<ID3D12Resource> CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
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
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferResourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(bufferResource.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    return bufferResource;
}

//==================================================
// ディスクリプタヒープ作成関数
//==================================================
ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
    ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    UINT numDescriptors,
    bool shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
    descriptorHeapDesc.Type = heapType;
    descriptorHeapDesc.NumDescriptors = numDescriptors;
    descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ComPtr<ID3D12DescriptorHeap> descriptorHeap;
    HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(descriptorHeap.GetAddressOf()));
    assert(SUCCEEDED(hr));

    return descriptorHeap;
}

//==================================================
// ウィンドウプロシージャ
//==================================================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
#ifdef USE_IMGUI
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }
#endif
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

//==================================================
// テクスチャファイル読み込み関数
//==================================================
DirectX::ScratchImage LoadTexture(const std::string& filepath) {
    DirectX::ScratchImage image{};
    std::wstring filePathW = ConvertString(filepath);
    HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    assert(SUCCEEDED(hr));

    DirectX::ScratchImage mipImages{};
    hr = DirectX::GenerateMipMaps(
        image.GetImages(),
        image.GetImageCount(),
        image.GetMetadata(),
        DirectX::TEX_FILTER_SRGB,
        0,
        mipImages
    );
    assert(SUCCEEDED(hr));

    return mipImages;
}

//==================================================
// テクスチャリソース作成関数
//==================================================
ComPtr<ID3D12Resource> CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata)
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
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(resource.GetAddressOf()));
    if (FAILED(hr)) {
        return {};
    }
    return resource;
}

ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height)
{
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.MipLevels = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE depthClearValue{};
    depthClearValue.DepthStencil.Depth = 1.0f;
    depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

    ComPtr<ID3D12Resource> resource;
    HRESULT hr = device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &depthClearValue,
        IID_PPV_ARGS(resource.GetAddressOf())
    );

    assert(SUCCEEDED(hr));
    return resource;
}

//==================================================
// テクスチャデータアップロード関数
//==================================================
[[nodiscard]]
ComPtr<ID3D12Resource> UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList)
{
    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
    uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
    ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(device, intermediateSize);

    UpdateSubresources(
        commandList,
        texture,
        intermediateResource.Get(),
        0, 0, UINT(subresources.size()),
        subresources.data()
    );

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);
    return intermediateResource;
}

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handleCPU.ptr += (descriptorSize * index);
    return handleCPU;
};

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    handleGPU.ptr += (descriptorSize * index);
    return handleGPU;
};

//==================================================
// マテリアルテンプレートファイル読み込み関数
//==================================================

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
    MaterialData materialData{};
    std::string line;
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "map_Kd") {
            std::string textureFilename;
            s >> textureFilename;
            materialData.textureFilePath = directoryPath + "/" + textureFilename;
        }
    }

    return materialData;
}

//==================================================
//objをよみこむ関数
//==================================================
ModelData LoadObj(const std::string& directoryPath, const std::string& fileName)
{
    ModelData modelData;
    std::vector<Vector4> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> texcoords;
    std::string line;

    std::ifstream file(directoryPath + "/" + fileName);
    assert(file.is_open());

    while (std::getline(file, line))
    {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "mtllib")
        {
            std::string materialFilename;
            s >> materialFilename;
            modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
        }
        else if (identifier == "v")
        {
            Vector4 position;
            s >> position.x >> position.y >> position.z;
            position.x *= -1.0f;
            position.w = 1.0f;
            positions.push_back(position);
        }
        else if (identifier == "vt")
        {
            Vector2 texcoord;
            s >> texcoord.x >> texcoord.y;
            texcoord.y = 1.0f - texcoord.y;
            texcoords.push_back(texcoord);
        }
        else if (identifier == "vn")
        {
            Vector3 normal;
            s >> normal.x >> normal.y >> normal.z;
            normal.x *= -1.0f;
            normals.push_back(normal);
        }
        else if (identifier == "f")
        {
            VertexData triangle[3]{};

            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex)
            {
                std::string vertexDefinition;
                s >> vertexDefinition;

                std::istringstream v(vertexDefinition);
                uint32_t elementIndices[3] = {};

                for (int32_t element = 0; element < 3; ++element)
                {
                    std::string index;
                    std::getline(v, index, '/');
                    elementIndices[element] = std::stoi(index);
                }

                Vector4 position = positions[elementIndices[0] - 1];
                Vector2 texcoord = texcoords[elementIndices[1] - 1];
                Vector3 normal = normals[elementIndices[2] - 1];

                triangle[faceVertex] = { position, texcoord, normal };
            }

            modelData.vertices.push_back(triangle[2]);
            modelData.vertices.push_back(triangle[1]);
            modelData.vertices.push_back(triangle[0]);
        }
    }

    return modelData;
}

//==================================================
// エントリーポイント
//==================================================
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
    D3DResourceLeakChecker leakChecker;

    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    assert(SUCCEEDED(hrCom));

    SetUnhandledExceptionFilter(ExportDump);

    std::filesystem::create_directory("logs");

    auto now = std::chrono::system_clock::now();
    auto nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

    std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
    std::string logFilePath = "logs/" + dateString + ".log";

    std::ofstream logStream(logFilePath);

    Log(logStream, "Application Start");

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"CG2WindowClass";
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    const int32_t kClientWidth = 1280;
    const int32_t kClientHeight = 720;

    RECT wrc = { 0, 0, kClientWidth, kClientHeight };
    AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"CG2",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wrc.right - wrc.left,
        wrc.bottom - wrc.top,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );
    assert(hwnd != nullptr);

    ShowWindow(hwnd, SW_SHOW);

    //==================================================
	//DirectInputの初期化
	//==================================================

	IDirectInput8* directInput = nullptr;
    HRESULT hr = DirectInput8Create(
        wc.hInstance,
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        (void**)&directInput, nullptr);
	assert(SUCCEEDED(hr));

	//キーボードデバイスの作成
	IDirectInputDevice8* keyboard = nullptr;
	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboard, NULL);
	assert(SUCCEEDED(hr));

	//キーボードデバイスのデータフォーマットを設定
	hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	//キーボードデバイスの協調レベルを設定
    hr = keyboard->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12Debug1> debugController;

#ifdef _DEBUG
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
        debugController->EnableDebugLayer();
        debugController->SetEnableGPUBasedValidation(TRUE);
        Log(logStream, "D3D12 Debug Layer Enabled");
    }
#endif

    ComPtr<IDXGIFactory7> dxgiFactory;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<IDXGIAdapter4> useAdapter;

    for (UINT i = 0;
        dxgiFactory->EnumAdapterByGpuPreference(
            i,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(useAdapter.GetAddressOf())
        ) != DXGI_ERROR_NOT_FOUND;
        ++i)
    {
        DXGI_ADAPTER_DESC3 adapterDesc{};
        hr = useAdapter->GetDesc3(&adapterDesc);
        assert(SUCCEEDED(hr));

        if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
            Log(logStream, ConvertString(std::format(
                L"Use Adapter: {}",
                adapterDesc.Description
            )));
            break;
        }

        useAdapter.Reset();
    }

    assert(useAdapter != nullptr);

    ComPtr<ID3D12Device> device;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0
    };

    const char* featureLevelStrings[] = {
        "12.2",
        "12.1",
        "12.0"
    };

    for (size_t i = 0; i < _countof(featureLevels); ++i) {
        hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(device.GetAddressOf()));
        if (SUCCEEDED(hr)) {
            Log(logStream, std::format("Feature Level: {}", featureLevelStrings[i]));
            break;
        }
    }

    assert(device != nullptr);
    Log(logStream, "Complete create D3D12 Device");

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf())))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

        D3D12_MESSAGE_ID denyIds[] = {
            D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
        };

        D3D12_MESSAGE_SEVERITY severities[] = {
            D3D12_MESSAGE_SEVERITY_INFO
        };

        D3D12_INFO_QUEUE_FILTER filter{};
        filter.DenyList.NumIDs = _countof(denyIds);
        filter.DenyList.pIDList = denyIds;
        filter.DenyList.NumSeverities = _countof(severities);
        filter.DenyList.pSeverityList = severities;

        infoQueue->PushStorageFilter(&filter);
    }
#endif

    ComPtr<ID3D12CommandQueue> commandQueue;
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};

    hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12CommandAllocator> commandAllocator;
    hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(commandAllocator.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12GraphicsCommandList> commandList;
    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(commandList.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    ComPtr<IDXGISwapChain4> swapChain;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = kClientWidth;
    swapChainDesc.Height = kClientHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = dxgiFactory->CreateSwapChainForHwnd(
        commandQueue.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(
        device.Get(),
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        2,
        false
    );

    ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(
        device.Get(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        128,
        true
    );

    ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(
        device.Get(),
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        1,
        false
    );

    const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    ComPtr<ID3D12Resource> swapChainResources[2];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]{};

    for (UINT i = 0; i < 2; ++i) {
        hr = swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainResources[i].GetAddressOf()));
        assert(SUCCEEDED(hr));

        rtvHandles[i] = GetCPUDescriptorHandle(rtvDescriptorHeap.Get(), descriptorSizeRTV, i);

        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        device->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc, rtvHandles[i]);
    }

    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;

    hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
    assert(SUCCEEDED(hr));

    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent != nullptr);

    ComPtr<IDxcUtils> dxcUtils;
    ComPtr<IDxcCompiler3> dxcCompiler;

    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxcUtils.GetAddressOf()));
    assert(SUCCEEDED(hr));

    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxcCompiler.GetAddressOf()));
    assert(SUCCEEDED(hr));

    ComPtr<IDxcIncludeHandler> includeHandler;
    hr = dxcUtils->CreateDefaultIncludeHandler(includeHandler.GetAddressOf());
    assert(SUCCEEDED(hr));

    D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
    descriptionRootSignature.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
    descriptorRange[0].BaseShaderRegister = 0;
    descriptorRange[0].NumDescriptors = 1;
    descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[4] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[1].Descriptor.ShaderRegister = 0;
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[3].Descriptor.ShaderRegister = 1;
    descriptionRootSignature.pParameters = rootParameters;
    descriptionRootSignature.NumParameters = _countof(rootParameters);

    D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    staticSamplers[0].ShaderRegister = 0;
    staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descriptionRootSignature.pStaticSamplers = staticSamplers;
    descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

    ComPtr<ID3D12Resource> wvpResource = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));
    TransformationMatrix* wvpData = nullptr;
    wvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
    wvpData->WVP = Matrix4x4::MakeIdentity4x4();
    wvpData->World = Matrix4x4::MakeIdentity4x4();

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;

    hr = D3D12SerializeRootSignature(
        &descriptionRootSignature,
        D3D_ROOT_SIGNATURE_VERSION_1,
        signatureBlob.GetAddressOf(),
        errorBlob.GetAddressOf()
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            Log(logStream, reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        assert(false);
    }

    ComPtr<ID3D12RootSignature> rootSignature;
    hr = device->CreateRootSignature(
        0,
        signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(),
        IID_PPV_ARGS(rootSignature.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
    inputElementDescs[0].SemanticName = "POSITION";
    inputElementDescs[0].SemanticIndex = 0;
    inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    inputElementDescs[0].AlignedByteOffset = 0;

    inputElementDescs[1].SemanticName = "TEXCOORD";
    inputElementDescs[1].SemanticIndex = 0;
    inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    inputElementDescs[2].SemanticName = "NORMAL";
    inputElementDescs[2].SemanticIndex = 0;
    inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    D3D12_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
        L"Object3D.VS.hlsl",
        L"vs_6_0",
        dxcUtils.Get(),
        dxcCompiler.Get(),
        includeHandler.Get(),
        logStream
    );

    ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
        L"Object3D.PS.hlsl",
        L"ps_6_0",
        dxcUtils.Get(),
        dxcCompiler.Get(),
        includeHandler.Get(),
        logStream
    );

    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
    graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
    graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
    graphicsPipelineStateDesc.VS = {
        vertexShaderBlob->GetBufferPointer(),
        vertexShaderBlob->GetBufferSize()
    };
    graphicsPipelineStateDesc.PS = {
        pixelShaderBlob->GetBufferPointer(),
        pixelShaderBlob->GetBufferSize()
    };
    graphicsPipelineStateDesc.BlendState = blendDesc;
    graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
    graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    graphicsPipelineStateDesc.NumRenderTargets = 1;
    graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphicsPipelineStateDesc.SampleDesc.Count = 1;
    graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    ComPtr<ID3D12PipelineState> graphicsPipelineState;
    hr = device->CreateGraphicsPipelineState(
        &graphicsPipelineStateDesc,
        IID_PPV_ARGS(graphicsPipelineState.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    ModelData modelData = LoadObj("resources", "axis.obj");

    ComPtr<ID3D12Resource> vertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * modelData.vertices.size());
    ComPtr<ID3D12Resource> materialResource = CreateBufferResource(device.Get(), sizeof(Material));
    ComPtr<ID3D12Resource> vertexResourceSprite = CreateBufferResource(device.Get(), sizeof(VertexData) * 6);
    ComPtr<ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));
    ComPtr<ID3D12Resource> indexResourceSprite = CreateBufferResource(device.Get(), sizeof(uint32_t) * 6);

    TransformationMatrix* transformationMatrixDataSprite = nullptr;
    transformationMatrixResourceSprite.Get()->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
    transformationMatrixDataSprite->WVP = Matrix4x4::MakeIdentity4x4();
    transformationMatrixDataSprite->World = Matrix4x4::MakeIdentity4x4();

    Material* materialData = nullptr;
    materialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = true;
    materialData->uvTransform = Matrix4x4::MakeIdentity4x4();

    ComPtr<ID3D12Resource> materialResourceSprite = CreateBufferResource(device.Get(), sizeof(Material));
    Material* materialDataSprite = nullptr;
    materialResourceSprite.Get()->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
    materialDataSprite->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    materialDataSprite->enableLighting = false;
    materialDataSprite->uvTransform = Matrix4x4::MakeIdentity4x4();

    ComPtr<ID3D12Resource> directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));
    DirectionalLight* directionalLightData = nullptr;
    directionalLightResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
    directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
    directionalLightData->intensity = 1.0f;

    DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
    const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
    ComPtr<ID3D12Resource> textureResource = CreateTextureResource(device.Get(), metadata);
    ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(device.Get(), kClientWidth, kClientHeight);

    DirectX::ScratchImage mipImages2 = LoadTexture(modelData.material.textureFilePath);
    const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
    ComPtr<ID3D12Resource> textureResource2 = CreateTextureResource(device.Get(), metadata2);
    ComPtr<ID3D12Resource> intermediateResource2 = UploadTextureData(textureResource2.Get(), mipImages2, device.Get(), commandList.Get());

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        GetCPUDescriptorHandle(dsvDescriptorHeap.Get(), descriptorSizeDSV, 0);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvHandle);

    ComPtr<ID3D12Resource> intermediateResource =
        UploadTextureData(textureResource.Get(), mipImages, device.Get(), commandList.Get());

    hr = commandList->Close();
    assert(SUCCEEDED(hr));

    ID3D12CommandList* commandLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, commandLists);

    ++fenceValue;
    hr = commandQueue->Signal(fence.Get(), fenceValue);
    assert(SUCCEEDED(hr));
    if (fence->GetCompletedValue() < fenceValue) {
        hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
        assert(SUCCEEDED(hr));
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    hr = commandAllocator->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList->Reset(commandAllocator.Get(), nullptr);
    assert(SUCCEEDED(hr));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = metadata.format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
    srvDesc2.Format = metadata2.format;
    srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU =
        GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU =
        GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);

    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 =
        GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 =
        GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 2);

    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);
    device->CreateShaderResourceView(textureResource2.Get(), &srvDesc2, textureSrvHandleCPU2);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
    vertexBufferView.BufferLocation = vertexResource.Get()->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
    vertexBufferView.StrideInBytes = sizeof(VertexData);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
    vertexBufferViewSprite.BufferLocation = vertexResourceSprite.Get()->GetGPUVirtualAddress();
    vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

    D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
    indexBufferViewSprite.BufferLocation = indexResourceSprite.Get()->GetGPUVirtualAddress();
    indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

    VertexData* vertexData = nullptr;
    vertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
    std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

    VertexData* vertexDataSprite = nullptr;
    vertexResourceSprite.Get()->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

    vertexDataSprite[0].position = { 0.0f,360.0f, 0.0f, 1.0f };
    vertexDataSprite[0].texcoord = { 0.0f, 1.0f };

    vertexDataSprite[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexDataSprite[1].texcoord = { 0.0f, 0.0f };

    vertexDataSprite[2].position = { 640.0f, 360.0f, 0.0f, 1.0f };
    vertexDataSprite[2].texcoord = { 1.0f, 1.0f };

    vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };
    vertexDataSprite[3].texcoord = { 0.0f, 0.0f };
    vertexDataSprite[4].position = { 640.0f, 0.0f, 0.0f, 1.0f };
    vertexDataSprite[4].texcoord = { 1.0f, 0.0f };
    vertexDataSprite[5].position = { 640.0f, 360.0f, 0.0f, 1.0f };
    vertexDataSprite[5].texcoord = { 1.0f, 1.0f };

    for (int i = 0; i < 6; ++i) {
        vertexDataSprite[i].normal = { 0.0f, 0.0f, -1.0f };
    }

    uint32_t* indexDataSprite = nullptr;
    indexResourceSprite.Get()->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
    indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
    indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(kClientWidth);
    viewport.Height = static_cast<float>(kClientHeight);
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = kClientWidth;
    scissorRect.bottom = kClientHeight;

    hr = commandList->Close();
    assert(SUCCEEDED(hr));

    Transform transform{ {1.0f,1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform cameraTransform{ {1.0f,1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };
    Transform transformSprite{ {1.0f,1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform uvTransformSprite{ {1.0f,1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

#ifdef USE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(
        device.Get(),
        swapChainDesc.BufferCount,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        srvDescriptorHeap.Get(),
        srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
    );
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Build();
#endif // USE_IMGUI

    MSG msg{};
	BYTE key[256] = {};
	BYTE prevKey[256] = {};
    bool useMonsterBallTexture = true;

    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else {
			//==================================================
			//DirectInputのキーボード情報取得
			//==================================================

			keyboard->Acquire();

			BYTE key[256] = {};
            keyboard->GetDeviceState(sizeof(key), key);

#ifdef USE_IMGUI
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("Debug Settings");

            if (ImGui::TreeNode("3D Object Transform")) {
                ImGui::DragFloat3("Scale", &transform.Scale.x, 0.01f);
                ImGui::DragFloat3("Rotate", &transform.Rotate.x, 0.01f);
                ImGui::DragFloat3("Translate", &transform.Translate.x, 0.01f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Camera Transform")) {
                ImGui::DragFloat3("Scale", &cameraTransform.Scale.x, 0.01f);
                ImGui::DragFloat3("Rotate", &cameraTransform.Rotate.x, 0.01f);
                ImGui::DragFloat3("Translate", &cameraTransform.Translate.x, 0.01f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Sprite Transform")) {
                ImGui::DragFloat3("Scale", &transformSprite.Scale.x, 0.01f);
                ImGui::DragFloat3("Rotate", &transformSprite.Rotate.x, 0.01f);
                ImGui::DragFloat3("Translate", &transformSprite.Translate.x, 1.0f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Material")) {
                ImGui::ColorEdit4("Color", &materialData->color.x);
                bool enableLighting = materialData->enableLighting != 0;
                if (ImGui::Checkbox("Enable Lighting", &enableLighting)) {
                    materialData->enableLighting = enableLighting ? 1 : 0;
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Directional Light")) {
                ImGui::ColorEdit4("Color", &directionalLightData->color.x);
                if (ImGui::DragFloat3("Direction", &directionalLightData->direction.x, 0.01f)) {
                    directionalLightData->direction = Normalize(directionalLightData->direction);
                }
                ImGui::DragFloat("Intensity", &directionalLightData->intensity, 0.01f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("UV Transform")) {
                ImGui::DragFloat2("UVTranslate", &uvTransformSprite.Translate.x, 0.01f, -10.0f, 10.0f);
                ImGui::DragFloat2("UVScale", &uvTransformSprite.Scale.x, 0.01f, -10.0f, 10.0f);
                ImGui::SliderAngle("UVRotate", &uvTransformSprite.Rotate.z);
                ImGui::TreePop();
            }

            ImGui::Checkbox("Use Monster Ball Texture", &useMonsterBallTexture);

            ImGui::End();
            ImGui::Render();
#endif // USE_IMGUI

            transform.Rotate.y += 0.03f;

            Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(transform.Scale, transform.Rotate, transform.Translate);
            Matrix4x4 cameraMatrix = Matrix4x4::MakeAffineMatrix(cameraTransform.Scale, cameraTransform.Rotate, cameraTransform.Translate);
            Matrix4x4 viewMatrix = Inverse(cameraMatrix);
            Matrix4x4 projectionMatrix = Matrix4x4::MakePerspectiveFovMatrix(
                0.45f,
                static_cast<float>(kClientWidth) / kClientHeight,
                0.1f,
                100.0f
            );
            Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
            wvpData->WVP = worldViewProjectionMatrix;
            wvpData->World = worldMatrix;

            Matrix4x4 worldMatrixSprite = Matrix4x4::MakeAffineMatrix(transformSprite.Scale, transformSprite.Rotate, transformSprite.Translate);
            Matrix4x4 viewMatrixSprite = Matrix4x4::MakeIdentity4x4();
            Matrix4x4 viewProjectionMatrixSprite = Matrix4x4::MakeOrthographicMatrix(0.0f, 0.0f, static_cast<float>(kClientWidth), static_cast<float>(kClientHeight), 0.1f, 100.0f);
            Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, viewProjectionMatrixSprite));

            transformationMatrixDataSprite->WVP = worldViewProjectionMatrixSprite;
            transformationMatrixDataSprite->World = worldMatrixSprite;

            Matrix4x4 uvTransformMatrix = Matrix4x4::MakeScaleMatrix(uvTransformSprite.Scale);
            uvTransformMatrix = Multiply(uvTransformMatrix, Matrix4x4::MakeRotateZMatrix(uvTransformSprite.Rotate.z));
            uvTransformMatrix = Multiply(uvTransformMatrix, Matrix4x4::MakeTranslateMatrix(uvTransformSprite.Translate));
            materialDataSprite->uvTransform = uvTransformMatrix;

            UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

            hr = commandAllocator->Reset();
            assert(SUCCEEDED(hr));

            hr = commandList->Reset(commandAllocator.Get(), nullptr);
            assert(SUCCEEDED(hr));

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

            commandList->ResourceBarrier(1, &barrier);

            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);

            float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };
            commandList->ClearRenderTargetView(
                rtvHandles[backBufferIndex],
                clearColor,
                0,
                nullptr
            );

            commandList->RSSetViewports(1, &viewport);
            commandList->RSSetScissorRects(1, &scissorRect);

            commandList->SetGraphicsRootSignature(rootSignature.Get());
            commandList->SetPipelineState(graphicsPipelineState.Get());
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
            commandList->SetDescriptorHeaps(1, descriptorHeaps);

            commandList->SetGraphicsRootDescriptorTable(2, useMonsterBallTexture ? textureSrvHandleGPU2 : textureSrvHandleGPU);
            commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource.Get()->GetGPUVirtualAddress());

            commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            commandList->SetGraphicsRootConstantBufferView(0, materialResource.Get()->GetGPUVirtualAddress());
            commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
            commandList->SetGraphicsRootConstantBufferView(1, wvpResource.Get()->GetGPUVirtualAddress());
            commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);

#ifdef USE_IMGUI
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif // USE_IMGUI

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

            commandList->ResourceBarrier(1, &barrier);

            hr = commandList->Close();
            assert(SUCCEEDED(hr));

            ID3D12CommandList* commandLists[] = { commandList.Get() };
            commandQueue->ExecuteCommandLists(1, commandLists);

            hr = swapChain->Present(1, 0);
            assert(SUCCEEDED(hr));

            ++fenceValue;
            hr = commandQueue->Signal(fence.Get(), fenceValue);
            assert(SUCCEEDED(hr));

            if (fence->GetCompletedValue() < fenceValue) {
                hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
                assert(SUCCEEDED(hr));

                WaitForSingleObject(fenceEvent, INFINITE);
            }
        }
    }

    ++fenceValue;
    hr = commandQueue->Signal(fence.Get(), fenceValue);
    assert(SUCCEEDED(hr));

    if (fence->GetCompletedValue() < fenceValue) {
        hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
        assert(SUCCEEDED(hr));
        WaitForSingleObject(fenceEvent, INFINITE);
    }

#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
#endif // USE_IMGUI
    Log(logStream, "Application End");

    keyboard->Unacquire();
    keyboard->Release();
    directInput->Release();

    CloseHandle(fenceEvent);

    CloseWindow(hwnd);

    CoUninitialize();

    return 0;
}