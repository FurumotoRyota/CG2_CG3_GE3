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
#include "DebugCamera.h"

#ifdef USE_IMGUI
#include "externals//imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/imgui/imgui_impl_dx12.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // USE_IMGUI

#include "VertexData.h"
#include "externals/DirectXTex/d3dx12.h"
#include <vector>
#include <unordered_map>

#include<fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <random>
#include <algorithm>

#define DIRECTINPUT_VERSION 0x0800 // DirectInput 8.0 以降を使用することを指定, dinput.h より先に書くこと
#include <dinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")

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
    int32_t lightingMode; // 0:なし 1:Lambert 2:HalfLambert
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

//==================================================
// 1メッシュぶんの頂点データ（objの o/g、または usemtl の切り替わりで区切られる）
//==================================================
struct MeshData
{
    std::vector<VertexData> vertices;
    std::string materialName; // このメッシュが使うマテリアル名（MaterialDataのキー）
};

struct ModelData
{
    std::vector<MeshData> meshes;
    std::unordered_map<std::string, MaterialData> materials; // マテリアル名 -> テクスチャ情報
};

//==================================================
// 1メッシュぶんのGPUリソース（頂点バッファ + 専用テクスチャ）
//==================================================
struct SubMesh
{
    size_t vertexCount = 0;
    ComPtr<ID3D12Resource> vertexResource;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

    ComPtr<ID3D12Resource> textureResource;
    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU{};
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU{};
};

//==================================================
// 1体の3Dオブジェクトが必要とするGPUリソースをまとめた構造体
// これをvector<Object3D>で複数持つことで、複数オブジェクトの描画を可能にする
// 1つのオブジェクトはMultiMesh/MultiMaterial対応のため複数のSubMeshを持つ
//==================================================
struct Object3D
{
    std::string name;
    Transform transform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform uvTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} }; // このオブジェクトのテクスチャUVに適用する変換

    int objIndex = 0;       // ImGuiのComboで選ばれている objFileList 内のインデックス
    int loadedObjIndex = 0; // 実際にGPUへロード済みのインデックス（objIndexと一致していれば再構築不要）

    ModelData modelData;
    std::vector<SubMesh> subMeshes; // modelData.meshesと1対1対応

    int srvSlotBase = 0; // このオブジェクトが使うSRVスロットの開始番号（サブメッシュごとに連番で使う）

    ComPtr<ID3D12Resource> materialResource;
    Material* materialData = nullptr;
    Vector4 konamiOriginalColor{ 1.0f, 1.0f, 1.0f, 1.0f }; // パーティーモード開始前の色を退避しておく

    ComPtr<ID3D12Resource> wvpResource;
    TransformationMatrix* wvpData = nullptr;
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
// ゲームパッドのボタン判定関数（キーボードのTriggerKeyに相当）
// buttonIndex: DIJOYSTATE2::rgbButtons の添字（コントローラーにより並びが異なる場合がある）
//==================================================
bool GamepadButtonTrigger(const DIJOYSTATE2& state, const DIJOYSTATE2& prevState, int buttonIndex)
{
    return (state.rgbButtons[buttonIndex] & 0x80) != 0 && (prevState.rgbButtons[buttonIndex] & 0x80) == 0;
}

//==================================================
// コナミコマンド（↑↑↓↓←→←→BA）の入力判定
// キーボードの矢印/B/Aキーと、ゲームパッドの十字キー(POV)/ボタンの両方に対応する
// 呼び出すたびに内部の進捗(static)を更新し、コマンドが成立した瞬間だけtrueを返す
//==================================================
enum class KonamiInput { Up, Down, Left, Right, ButtonB, ButtonA };

// POVハットの値(1/100度単位。0=上,9000=右,18000=下,27000=左。0xFFFFFFFFは未入力)が
// 指定方向とみなせるか判定する（±45度まで許容し、4方向・8方向どちらのPOVにも対応）
bool PovMatchesDirection(DWORD pov, DWORD directionCentidegrees)
{
    if (pov == 0xFFFFFFFF) {
        return false;
    }
    DWORD diff = (pov > directionCentidegrees) ? (pov - directionCentidegrees) : (directionCentidegrees - pov);
    if (diff > 18000) {
        diff = 36000 - diff;
    }
    return diff <= 4500;
}

bool IsKonamiInputTriggered(
    KonamiInput input,
    const BYTE key[256], BYTE prevKey[256],
    const DIJOYSTATE2* joyState, const DIJOYSTATE2* prevJoyState)
{
    auto povTriggered = [&](DWORD directionCentidegrees) {
        if (joyState == nullptr || prevJoyState == nullptr) {
            return false;
        }
        return PovMatchesDirection(joyState->rgdwPOV[0], directionCentidegrees)
            && !PovMatchesDirection(prevJoyState->rgdwPOV[0], directionCentidegrees);
        };
    auto buttonTriggered = [&](int buttonIndex) {
        if (joyState == nullptr || prevJoyState == nullptr) {
            return false;
        }
        return GamepadButtonTrigger(*joyState, *prevJoyState, buttonIndex);
        };

    switch (input) {
    case KonamiInput::Up:      return TriggerKey(key, DIK_UP, prevKey) || povTriggered(0);
    case KonamiInput::Down:    return TriggerKey(key, DIK_DOWN, prevKey) || povTriggered(18000);
    case KonamiInput::Left:    return TriggerKey(key, DIK_LEFT, prevKey) || povTriggered(27000);
    case KonamiInput::Right:   return TriggerKey(key, DIK_RIGHT, prevKey) || povTriggered(9000);
    case KonamiInput::ButtonB: return TriggerKey(key, DIK_B, prevKey) || buttonTriggered(1); // 1=Bボタン想定
    case KonamiInput::ButtonA: return TriggerKey(key, DIK_A, prevKey) || buttonTriggered(0); // 0=Aボタン想定
    }
    return false;
}

bool CheckKonamiCode(
    const BYTE key[256], BYTE prevKey[256],
    const DIJOYSTATE2* joyState, const DIJOYSTATE2* prevJoyState)
{
    static const KonamiInput kSequence[] = {
        KonamiInput::Up, KonamiInput::Up, KonamiInput::Down, KonamiInput::Down,
        KonamiInput::Left, KonamiInput::Right, KonamiInput::Left, KonamiInput::Right,
        KonamiInput::ButtonB, KonamiInput::ButtonA
    };
    static const KonamiInput kAllInputs[] = {
        KonamiInput::Up, KonamiInput::Down, KonamiInput::Left, KonamiInput::Right,
        KonamiInput::ButtonB, KonamiInput::ButtonA
    };
    constexpr size_t kSequenceLength = sizeof(kSequence) / sizeof(kSequence[0]);
    static size_t progress = 0;

    for (KonamiInput input : kAllInputs) {
        if (!IsKonamiInputTriggered(input, key, prevKey, joyState, prevJoyState)) {
            continue; // このフレームで発生した入力ではない
        }

        // このフレームで発生した入力(通常1つ)が見つかった
        if (input == kSequence[progress]) {
            ++progress;
            if (progress >= kSequenceLength) {
                progress = 0;
                return true; // コナミコマンド成立
            }
        }
        else {
            // 間違った入力。それがコマンドの先頭(UP)ならそこから再スタート、それ以外は最初からやり直し
            progress = (input == kSequence[0]) ? 1 : 0;
        }
        break; // 1フレームにつき1入力ぶん処理すれば十分
    }

    return false;
}

//==================================================
// 色相(0～360度)からRGBを生成する（彩度・明度は最大固定）
//==================================================
Vector4 HueToColor(float hueDegrees)
{
    float h = std::fmodf(hueDegrees, 360.0f) / 60.0f;
    if (h < 0.0f) {
        h += 6.0f;
    }
    float c = 1.0f;
    float x = c * (1.0f - std::fabs(std::fmodf(h, 2.0f) - 1.0f));

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 1.0f) { r = c; g = x; b = 0.0f; }
    else if (h < 2.0f) { r = x; g = c; b = 0.0f; }
    else if (h < 3.0f) { r = 0.0f; g = c; b = x; }
    else if (h < 4.0f) { r = 0.0f; g = x; b = c; }
    else if (h < 5.0f) { r = x; g = 0.0f; b = c; }
    else { r = c; g = 0.0f; b = x; }

    return Vector4{ r, g, b, 1.0f };
}

//==================================================
// パーティーモード用パーティクル
//==================================================
constexpr uint32_t kMaxParticles = 512;

struct Particle
{
    Vector3 position;
    Vector3 velocity;
    Vector4 color;
    float lifeTime = 1.0f;
    float currentTime = 0.0f;
    float scale = 0.3f;
};

// GPUに渡す1パーティクルぶんのデータ（ワールド計算込みのWVPと色）
struct ParticleForGPU
{
    Matrix4x4 WVP;
    Vector4 color;
};

// 指定位置から虹色のパーティクルをランダムに1個生成する
Particle MakeRandomPartyParticle(const Vector3& emitPosition, std::mt19937& rng)
{
    std::uniform_real_distribution<float> distVelXZ(-3.0f, 3.0f);
    std::uniform_real_distribution<float> distVelY(1.5f, 4.5f);
    std::uniform_real_distribution<float> distLife(0.6f, 1.4f);
    std::uniform_real_distribution<float> distScale(0.15f, 0.35f);
    std::uniform_real_distribution<float> distHue(0.0f, 360.0f);

    Particle particle{};
    particle.position = emitPosition;
    particle.velocity = { distVelXZ(rng), distVelY(rng), distVelXZ(rng) };
    particle.color = HueToColor(distHue(rng));
    particle.lifeTime = distLife(rng);
    particle.currentTime = 0.0f;
    particle.scale = distScale(rng);
    return particle;
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
// 1つのmtlファイルに複数のnewmtl(マテリアル)が定義されている場合に対応する
//==================================================

std::unordered_map<std::string, MaterialData> LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
    std::unordered_map<std::string, MaterialData> materials;
    std::string currentMaterialName;
    std::string line;
    std::ifstream file(directoryPath + "/" + filename);
    assert(file.is_open());

    while (std::getline(file, line)) {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "newmtl") {
            s >> currentMaterialName;
            materials[currentMaterialName] = MaterialData{};
        }
        else if (identifier == "map_Kd" && !currentMaterialName.empty()) {
            std::string textureFilename;
            s >> textureFilename;
            materials[currentMaterialName].textureFilePath = directoryPath + "/" + textureFilename;
        }
    }

    return materials;
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

    std::string currentMaterialName;
    MeshData* currentMesh = nullptr;
    bool needNewMesh = true; // trueの間はまだメッシュを確定させていない（空メッシュを作らないための遅延生成フラグ）

    // 実際に面(f)が来たときだけ新しいメッシュを確定させる
    // （o/g/usemtlが連続で出てきても、間に面が無ければ空メッシュを作らない）
    auto EnsureMesh = [&]() {
        if (needNewMesh) {
            modelData.meshes.push_back(MeshData{});
            currentMesh = &modelData.meshes.back();
            currentMesh->materialName = currentMaterialName;
            needNewMesh = false;
        }
        };

    while (std::getline(file, line))
    {
        std::string identifier;
        std::istringstream s(line);
        s >> identifier;

        if (identifier == "mtllib")
        {
            std::string materialFilename;
            s >> materialFilename;
            std::unordered_map<std::string, MaterialData> loaded = LoadMaterialTemplateFile(directoryPath, materialFilename);
            for (auto& [name, mat] : loaded) {
                modelData.materials[name] = mat;
            }
        }
        else if (identifier == "usemtl")
        {
            s >> currentMaterialName;
            // マテリアルが変わったら新しいメッシュとして区切る（実際の作成は次のfで行う）
            needNewMesh = true;
        }
        else if (identifier == "o" || identifier == "g")
        {
            // objファイル内で明示的にオブジェクト/グループが区切られている場合も新しいメッシュにする
            // （o/gが連続しても、面が無ければメッシュは作らない）
            needNewMesh = true;
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
            EnsureMesh();

            VertexData triangle[3]{};

            // objの面インデックスは絶対値(1始まり)と相対値(負の値、末尾からの相対位置)の
            // 両方があり得るため、両方を解決できるようにする
            auto ResolveIndex = [](int64_t rawIndex, size_t count) -> uint32_t {
                if (rawIndex < 0) {
                    // 相対index。-1は「直前(最後)に定義された要素」を指す
                    return static_cast<uint32_t>(static_cast<int64_t>(count) + rawIndex);
                }
                // 絶対index(1始まり) -> 0始まりに変換
                return static_cast<uint32_t>(rawIndex - 1);
                };

            for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex)
            {
                std::string vertexDefinition;
                s >> vertexDefinition;

                std::istringstream v(vertexDefinition);
                std::string indexStr;

                // 位置番号（必須）
                std::getline(v, indexStr, '/');
                int64_t posIndexRaw = std::stoll(indexStr);
                uint32_t posIndex = ResolveIndex(posIndexRaw, positions.size());

                // UV番号（無いobjもある: "v//vn" 形式）
                bool hasTexcoord = false;
                uint32_t texIndex = 0;
                if (std::getline(v, indexStr, '/') && !indexStr.empty()) {
                    texIndex = ResolveIndex(std::stoll(indexStr), texcoords.size());
                    hasTexcoord = true;
                }

                // 法線番号（無い場合も一応ケア）
                bool hasNormal = false;
                uint32_t normIndex = 0;
                if (std::getline(v, indexStr, '/') && !indexStr.empty()) {
                    normIndex = ResolveIndex(std::stoll(indexStr), normals.size());
                    hasNormal = true;
                }

                // 範囲外インデックスへの保険（壊れたobjでもクラッシュさせない）
                assert(posIndex < positions.size());
                Vector4 position = posIndex < positions.size() ? positions[posIndex] : Vector4{ 0.0f, 0.0f, 0.0f, 1.0f };
                Vector2 texcoord = (hasTexcoord && texIndex < texcoords.size()) ? texcoords[texIndex] : Vector2{ 0.0f, 0.0f };
                Vector3 normal = (hasNormal && normIndex < normals.size()) ? normals[normIndex] : Vector3{ 0.0f, 1.0f, 0.0f };

                triangle[faceVertex] = { position, texcoord, normal };
            }

            currentMesh->vertices.push_back(triangle[2]);
            currentMesh->vertices.push_back(triangle[1]);
            currentMesh->vertices.push_back(triangle[0]);
        }
    }

    // 面が1つも無い異常なobjへの保険
    if (modelData.meshes.empty()) {
        modelData.meshes.push_back(MeshData{});
    }

    // テクスチャが存在しないメッシュ（UVが無い/マテリアル未指定/mtllibが無い等）はuvCheckerで代用する
    for (auto& mesh : modelData.meshes) {
        MaterialData& mat = modelData.materials[mesh.materialName]; // 未登録ならデフォルト構築される
        if (mat.textureFilePath.empty()) {
            mat.textureFilePath = "resources/uvChecker.png";
        }
    }

    return modelData;
}

//==================================================
// 球を頂点計算で生成する関数（objファイルは使わない）
// kSubdivision: 緯度・経度それぞれの分割数
//==================================================
ModelData GenerateSphere(uint32_t kSubdivision)
{
    ModelData modelData;
    modelData.meshes.push_back(MeshData{});
    MeshData& mesh = modelData.meshes.back();
    mesh.materialName = "sphereMaterial";

    const float pi = 3.141592653589793f;
    const float kLonEvery = pi * 2.0f / float(kSubdivision); // 経度分割1つ分の角度
    const float kLatEvery = pi / float(kSubdivision);        // 緯度分割1つ分の角度

    mesh.vertices.resize(size_t(kSubdivision) * size_t(kSubdivision) * 6);

    // 緯度theta, 経度phiから球面上の1点を作る（w=1を忘れない）
    auto MakeVertex = [](float theta, float phi, float u, float v) {
        VertexData vertex{};
        float x = std::cosf(theta) * std::cosf(phi);
        float y = std::sinf(theta);
        float z = std::cosf(theta) * std::sinf(phi);
        vertex.position = { x, y, z, 1.0f };
        vertex.texcoord = { u, v };
        vertex.normal = { x, y, z }; // 原点中心の単位球なので位置=法線
        return vertex;
        };

    // 緯度の方向に分割 -pi/2 ～ pi/2
    for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex)
    {
        float lat = -pi / 2.0f + kLatEvery * float(latIndex); // theta

        // 経度の方向に分割
        for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex)
        {
            uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;
            float lon = float(lonIndex) * kLonEvery; // phi

            float u0 = float(lonIndex) / float(kSubdivision);
            float u1 = float(lonIndex + 1) / float(kSubdivision);
            float v0 = 1.0f - float(latIndex) / float(kSubdivision);
            float v1 = 1.0f - float(latIndex + 1) / float(kSubdivision);

            VertexData a = MakeVertex(lat, lon, u0, v0);
            VertexData b = MakeVertex(lat + kLatEvery, lon, u0, v1);
            VertexData c = MakeVertex(lat, lon + kLonEvery, u1, v0);
            VertexData d = MakeVertex(lat + kLatEvery, lon + kLonEvery, u1, v1);

            // 四角形を三角形2枚(abcとcbd)に分割
            mesh.vertices[start + 0] = a;
            mesh.vertices[start + 1] = b;
            mesh.vertices[start + 2] = c;

            mesh.vertices[start + 3] = c;
            mesh.vertices[start + 4] = b;
            mesh.vertices[start + 5] = d;
        }
    }

    modelData.materials[mesh.materialName].textureFilePath = "resources/uvChecker.png";
    return modelData;
}

//==================================================
// Object3D生成関数
// モデル読み込み・頂点バッファ・マテリアル用CBV・WVP用CBV・テクスチャ/SRVを
// まとめて1つのObject3Dとして作る
// srvIndex: このオブジェクト専用のテクスチャをsrvDescriptorHeapの何番目に置くか
// intermediateKeepAlive: テクスチャアップロード完了(フェンス待ち)まで生存させる中間リソースの置き場
//==================================================
//==================================================
// 1オブジェクトあたりに予約するSRVスロット数（サブメッシュ数の上限）
// multiMesh.obj/multiMaterial.obj等、メッシュ数が多いobjを使う場合はここを増やす
//==================================================
constexpr uint32_t kMaxSubMeshesPerObject = 8;

//==================================================
// modelData.meshesの内容をもとにSubMesh(頂点バッファ+専用テクスチャ)を構築する
// object.srvSlotBaseから連番でSRVスロットを使う
//==================================================
void BuildSubMeshes(
    Object3D& object,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12DescriptorHeap* srvDescriptorHeap,
    uint32_t descriptorSizeSRV,
    std::vector<ComPtr<ID3D12Resource>>& intermediateKeepAlive)
{
    assert(object.modelData.meshes.size() <= kMaxSubMeshesPerObject);

    object.subMeshes.clear();
    object.subMeshes.resize(object.modelData.meshes.size());

    for (size_t i = 0; i < object.modelData.meshes.size(); ++i)
    {
        const MeshData& mesh = object.modelData.meshes[i];
        SubMesh& subMesh = object.subMeshes[i];
        subMesh.vertexCount = mesh.vertices.size();

        // 頂点0のメッシュはGPUリソースを作らずスキップする（0バイトバッファ作成はクラッシュの原因になるため）
        if (mesh.vertices.empty()) {
            continue;
        }

        subMesh.vertexResource = CreateBufferResource(device, sizeof(VertexData) * mesh.vertices.size());
        VertexData* vertexData = nullptr;
        subMesh.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
        std::memcpy(vertexData, mesh.vertices.data(), sizeof(VertexData) * mesh.vertices.size());
        subMesh.vertexResource->Unmap(0, nullptr);

        subMesh.vertexBufferView.BufferLocation = subMesh.vertexResource->GetGPUVirtualAddress();
        subMesh.vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * mesh.vertices.size());
        subMesh.vertexBufferView.StrideInBytes = sizeof(VertexData);

        std::string texturePath = "resources/uvChecker.png";
        auto it = object.modelData.materials.find(mesh.materialName);
        if (it != object.modelData.materials.end() && !it->second.textureFilePath.empty()) {
            texturePath = it->second.textureFilePath;
        }

        DirectX::ScratchImage mipImages = LoadTexture(texturePath);
        const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
        subMesh.textureResource = CreateTextureResource(device, metadata);
        intermediateKeepAlive.push_back(UploadTextureData(subMesh.textureResource.Get(), mipImages, device, commandList));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = metadata.format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

        uint32_t srvSlot = uint32_t(object.srvSlotBase) + uint32_t(i);
        subMesh.textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, srvSlot);
        subMesh.textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, srvSlot);
        device->CreateShaderResourceView(subMesh.textureResource.Get(), &srvDesc, subMesh.textureSrvHandleCPU);
    }
}

//==================================================
// Object3D生成関数
// モデル読み込み・サブメッシュ(頂点バッファ+テクスチャ)一式・マテリアル用CBV・WVP用CBVを
// まとめて1つのObject3Dとして作る
// srvSlotBase: このオブジェクトが使うSRVスロットの開始番号（kMaxSubMeshesPerObject個ぶん連続で予約される）
// intermediateKeepAlive: テクスチャアップロード完了(フェンス待ち)まで生存させる中間リソースの置き場
//==================================================
Object3D CreateObject3D(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    ID3D12DescriptorHeap* srvDescriptorHeap,
    uint32_t descriptorSizeSRV,
    uint32_t srvSlotBase,
    const std::string& directoryPath,
    const std::string& fileName,
    bool isProceduralSphere,
    int initialObjIndex,
    std::vector<ComPtr<ID3D12Resource>>& intermediateKeepAlive)
{
    Object3D object;
    object.name = fileName;
    object.objIndex = initialObjIndex;
    object.loadedObjIndex = initialObjIndex;
    object.srvSlotBase = int(srvSlotBase);

    object.modelData = isProceduralSphere ? GenerateSphere(16) : LoadObj(directoryPath, fileName);

    BuildSubMeshes(object, device, commandList, srvDescriptorHeap, descriptorSizeSRV, intermediateKeepAlive);

    object.materialResource = CreateBufferResource(device, sizeof(Material));
    object.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&object.materialData));
    object.materialData->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    object.materialData->lightingMode = 2;
    object.materialData->uvTransform = Matrix4x4::MakeIdentity4x4();

    object.wvpResource = CreateBufferResource(device, sizeof(TransformationMatrix));
    object.wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&object.wvpData));
    object.wvpData->WVP = Matrix4x4::MakeIdentity4x4();
    object.wvpData->World = Matrix4x4::MakeIdentity4x4();

    return object;
}

//==================================================
// ゲームパッド(ジョイスティック)関連のコールバック
//==================================================

// 接続されているゲームコントローラーを列挙し、最初の1台のGUIDを取得する
BOOL CALLBACK EnumJoysticksCallback(const DIDEVICEINSTANCE* instance, VOID* context)
{
    GUID* foundGuid = reinterpret_cast<GUID*>(context);
    *foundGuid = instance->guidInstance;
    return DIENUM_STOP; // 最初に見つかった1台だけを使う
}

// 各軸の値の範囲を-32768～32767に設定する
BOOL CALLBACK EnumAxesCallback(const DIDEVICEOBJECTINSTANCE* instance, VOID* context)
{
    IDirectInputDevice8* joystick = reinterpret_cast<IDirectInputDevice8*>(context);

    DIPROPRANGE propRange{};
    propRange.diph.dwSize = sizeof(DIPROPRANGE);
    propRange.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    propRange.diph.dwHow = DIPH_BYID;
    propRange.diph.dwObj = instance->dwType;
    propRange.lMin = -32768;
    propRange.lMax = 32767;

    joystick->SetProperty(DIPROP_RANGE, &propRange.diph);
    return DIENUM_CONTINUE;
}

//==================================================
// Sound(XAudio2)関連
//==================================================

struct ChunkHeader
{
    char id[4];   // チャンク毎のID
    int32_t size; // チャンクサイズ
};

struct RiffHeader
{
    ChunkHeader chunk; // "RIFF"
    char type[4];      // "WAVE"
};

struct FormatChunk
{
    ChunkHeader chunk; // "fmt "
    WAVEFORMATEX fmt;  // 波形フォーマット
};

struct SoundData
{
    WAVEFORMATEX wfex;
    BYTE* pBuffer = nullptr;
    unsigned int bufferSize = 0;
};

//==================================================
// WAVファイル読み込み関数
// "fmt "/"data"以外のチャンク(LIST等のメタ情報)は読み飛ばす
//==================================================
SoundData SoundLoadWave(const std::string& filename)
{
    std::ifstream file(filename, std::ios_base::binary);
    assert(file.is_open());

    RiffHeader riff{};
    file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
    assert(strncmp(riff.chunk.id, "RIFF", 4) == 0);
    assert(strncmp(riff.type, "WAVE", 4) == 0);

    FormatChunk format{};
    bool formatFound = false;
    ChunkHeader dataChunkHeader{};
    bool dataFound = false;

    while (!dataFound) {
        ChunkHeader chunk{};
        file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
        assert(!file.eof());

        if (strncmp(chunk.id, "fmt ", 4) == 0) {
            format.chunk = chunk;
            assert(chunk.size <= sizeof(format.fmt));
            file.read(reinterpret_cast<char*>(&format.fmt), chunk.size);
            formatFound = true;
        }
        else if (strncmp(chunk.id, "data", 4) == 0) {
            dataChunkHeader = chunk;
            dataFound = true;
        }
        else {
            // LIST/INFO/JUNKなど未対応チャンクは読み飛ばす（奇数サイズは1byteパディングがある）
            file.seekg(chunk.size + (chunk.size % 2), std::ios_base::cur);
        }
    }

    assert(formatFound);

    char* pBuffer = new char[dataChunkHeader.size];
    file.read(pBuffer, dataChunkHeader.size);

    file.close();

    SoundData soundData{};
    soundData.wfex = format.fmt;
    soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
    soundData.bufferSize = dataChunkHeader.size;
    return soundData;
}

//==================================================
// サウンドデータ解放関数
//==================================================
void SoundUnload(SoundData* soundData)
{
    delete[] soundData->pBuffer;
    soundData->pBuffer = nullptr;
    soundData->bufferSize = 0;
    soundData->wfex = {};
}

//==================================================
// ループ再生用のソースボイスを作成し再生を開始する
//==================================================
IXAudio2SourceVoice* SoundPlayLoop(IXAudio2* xAudio2, const SoundData& soundData)
{
    IXAudio2SourceVoice* sourceVoice = nullptr;
    HRESULT hr = xAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfex);
    assert(SUCCEEDED(hr));

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = soundData.pBuffer;
    buffer.AudioBytes = soundData.bufferSize;
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = XAUDIO2_LOOP_INFINITE; // BGMなのでループ再生

    hr = sourceVoice->SubmitSourceBuffer(&buffer);
    assert(SUCCEEDED(hr));

    hr = sourceVoice->Start();
    assert(SUCCEEDED(hr));

    return sourceVoice;
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

    //==================================================
    // ゲームパッド(ジョイスティック)の初期化
    // 接続されていない場合はjoystick == nullptrのままとし、以降のポーリングをスキップする
    //==================================================
    IDirectInputDevice8* joystick = nullptr;
    {
        GUID joystickGuid = GUID_NULL;
        directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumJoysticksCallback, &joystickGuid, DIEDFL_ATTACHEDONLY);

        if (joystickGuid != GUID_NULL) {
            HRESULT joyHr = directInput->CreateDevice(joystickGuid, &joystick, nullptr);
            if (SUCCEEDED(joyHr)) {
                joyHr = joystick->SetDataFormat(&c_dfDIJoystick2);
                assert(SUCCEEDED(joyHr));

                joyHr = joystick->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
                assert(SUCCEEDED(joyHr));

                joystick->EnumObjects(EnumAxesCallback, joystick, DIDFT_AXIS);

                Log(logStream, "GamePad connected");
            }
            else {
                joystick = nullptr;
                Log(logStream, "GamePad found but failed to create device");
            }
        }
        else {
            Log(logStream, "GamePad not found (keyboard only)");
        }
    }

    //==================================================
    // XAudio2の初期化とBGM再生
    //==================================================
    ComPtr<IXAudio2> xAudio2;
    hr = XAudio2Create(xAudio2.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(hr));

    IXAudio2MasteringVoice* masterVoice = nullptr;
    hr = xAudio2->CreateMasteringVoice(&masterVoice);
    assert(SUCCEEDED(hr));

    SoundData bgmSoundData = SoundLoadWave("resources/BGM.wav");
    IXAudio2SourceVoice* bgmVoice = SoundPlayLoop(xAudio2.Get(), bgmSoundData);
    bool bgmPlaying = true; // 起動時はBGM再生中

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

    //==================================================
    // パーティクル用ルートシグネチャ・PSO
    // StructuredBuffer(t0)からインスタンスごとにWVP/色を取得するだけなので、
    // テクスチャ/サンプラーは使わずディスクリプタテーブル1個(SRV)のみで構成する
    //==================================================
    D3D12_DESCRIPTOR_RANGE particleDescriptorRange[1] = {};
    particleDescriptorRange[0].BaseShaderRegister = 0;
    particleDescriptorRange[0].NumDescriptors = 1;
    particleDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    particleDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER particleRootParameters[1] = {};
    particleRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    particleRootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    particleRootParameters[0].DescriptorTable.pDescriptorRanges = particleDescriptorRange;
    particleRootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(particleDescriptorRange);

    D3D12_ROOT_SIGNATURE_DESC particleRootSignatureDesc{};
    particleRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    particleRootSignatureDesc.pParameters = particleRootParameters;
    particleRootSignatureDesc.NumParameters = _countof(particleRootParameters);

    ComPtr<ID3DBlob> particleSignatureBlob;
    ComPtr<ID3DBlob> particleErrorBlob;
    hr = D3D12SerializeRootSignature(
        &particleRootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        particleSignatureBlob.GetAddressOf(),
        particleErrorBlob.GetAddressOf()
    );
    if (FAILED(hr)) {
        if (particleErrorBlob) {
            Log(logStream, reinterpret_cast<const char*>(particleErrorBlob->GetBufferPointer()));
        }
        assert(false);
    }

    ComPtr<ID3D12RootSignature> particleRootSignature;
    hr = device->CreateRootSignature(
        0,
        particleSignatureBlob->GetBufferPointer(),
        particleSignatureBlob->GetBufferSize(),
        IID_PPV_ARGS(particleRootSignature.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    D3D12_BLEND_DESC particleBlendDesc{};
    particleBlendDesc.RenderTarget[0].BlendEnable = TRUE;
    particleBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    particleBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    particleBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    particleBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    particleBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    particleBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    particleBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC particleRasterizerDesc{};
    particleRasterizerDesc.CullMode = D3D12_CULL_MODE_NONE; // ビルボードなのでカリング無し
    particleRasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

    ComPtr<IDxcBlob> particleVertexShaderBlob = CompileShader(
        L"Particle.VS.hlsl",
        L"vs_6_0",
        dxcUtils.Get(),
        dxcCompiler.Get(),
        includeHandler.Get(),
        logStream
    );

    ComPtr<IDxcBlob> particlePixelShaderBlob = CompileShader(
        L"Particle.PS.hlsl",
        L"ps_6_0",
        dxcUtils.Get(),
        dxcCompiler.Get(),
        includeHandler.Get(),
        logStream
    );

    D3D12_GRAPHICS_PIPELINE_STATE_DESC particlePipelineStateDesc{};
    particlePipelineStateDesc.pRootSignature = particleRootSignature.Get();
    particlePipelineStateDesc.InputLayout = inputLayoutDesc; // POSITION/TEXCOORD/NORMALレイアウトを流用(NORMALは未使用)
    particlePipelineStateDesc.VS = {
        particleVertexShaderBlob->GetBufferPointer(),
        particleVertexShaderBlob->GetBufferSize()
    };
    particlePipelineStateDesc.PS = {
        particlePixelShaderBlob->GetBufferPointer(),
        particlePixelShaderBlob->GetBufferSize()
    };
    particlePipelineStateDesc.BlendState = particleBlendDesc;
    particlePipelineStateDesc.RasterizerState = particleRasterizerDesc;

    D3D12_DEPTH_STENCIL_DESC particleDepthStencilDesc{};
    particleDepthStencilDesc.DepthEnable = true;
    particleDepthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // 深度は書き込まない（重なりを綺麗に見せるため）
    particleDepthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    particlePipelineStateDesc.DepthStencilState = particleDepthStencilDesc;
    particlePipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    particlePipelineStateDesc.NumRenderTargets = 1;
    particlePipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    particlePipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    particlePipelineStateDesc.SampleDesc.Count = 1;
    particlePipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

    ComPtr<ID3D12PipelineState> particlePipelineState;
    hr = device->CreateGraphicsPipelineState(
        &particlePipelineStateDesc,
        IID_PPV_ARGS(particlePipelineState.GetAddressOf())
    );
    assert(SUCCEEDED(hr));

    ComPtr<ID3D12Resource> vertexResourceSprite = CreateBufferResource(device.Get(), sizeof(VertexData) * 6);
    ComPtr<ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));
    ComPtr<ID3D12Resource> indexResourceSprite = CreateBufferResource(device.Get(), sizeof(uint32_t) * 6);

    TransformationMatrix* transformationMatrixDataSprite = nullptr;
    transformationMatrixResourceSprite.Get()->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
    transformationMatrixDataSprite->WVP = Matrix4x4::MakeIdentity4x4();
    transformationMatrixDataSprite->World = Matrix4x4::MakeIdentity4x4();

    ComPtr<ID3D12Resource> materialResourceSprite = CreateBufferResource(device.Get(), sizeof(Material));
    Material* materialDataSprite = nullptr;
    materialResourceSprite.Get()->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
    materialDataSprite->color = Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
    materialDataSprite->lightingMode = 0;
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

    //==================================================
    // ImGuiでオブジェクトごとにobjを切り替えるためのリスト
    // ※resourcesフォルダに存在するobjファイル名を追加すれば選択肢が増える
    // ※球のみobjファイルではなく、コードで頂点を生成する（kSphereProceduralTag）
    //==================================================
    const std::string kSphereProceduralTag = "sphere";
    std::vector<std::string> objFileList = {
        "axis.obj", "plane.obj", kSphereProceduralTag, "suzanne.obj", "bunny.obj", "teapot.obj",
        "multiMesh.obj", "multiMaterial.obj"
    };
    std::vector<const char*> objFileListCStr;
    for (const auto& name : objFileList) {
        objFileListCStr.push_back(name.c_str());
    }

    //==================================================
    // 複数の3Dオブジェクトを生成する
    // srvDescriptorHeapの 0=ImGuiフォント, 1=uvChecker(Sprite用) が使用済みのため
    // オブジェクト用テクスチャは 2番以降 を使う
    // 1オブジェクトあたりkMaxSubMeshesPerObject個ぶんのスロットを予約する
    // （MultiMesh/MultiMaterialのobjは複数サブメッシュ＝複数テクスチャを持つため）
    //==================================================
    std::vector<ComPtr<ID3D12Resource>> intermediateResourcesKeepAlive;

    std::vector<Object3D> objects;
    objects.push_back(CreateObject3D(
        device.Get(), commandList.Get(), srvDescriptorHeap.Get(), descriptorSizeSRV,
        2 + 0 * kMaxSubMeshesPerObject, "resources", "sphere", true, 2, intermediateResourcesKeepAlive)); // objFileList[2] = kSphereProceduralTag
    objects.back().transform.Translate = { -2.0f, 0.0f, 0.0f };

    objects.push_back(CreateObject3D(
        device.Get(), commandList.Get(), srvDescriptorHeap.Get(), descriptorSizeSRV,
        2 + 1 * kMaxSubMeshesPerObject, "resources", "suzanne.obj", false, 3, intermediateResourcesKeepAlive)); // objFileList[3] = "suzanne.obj"
    objects.back().transform.Translate = { 2.0f, 0.0f, 0.0f };

    //==================================================
    // パーティクル用リソース
    // ・頂点バッファ：中心原点(-0.5～0.5)の1枚のクアッド(6頂点、非インデックス描画)
    // ・StructuredBuffer：パーティクルごとのWVP/色。srvDescriptorHeapの20番を使用
    //   （0=ImGuiフォント, 1=uvChecker, 2～2+2*kMaxSubMeshesPerObjectはオブジェクト用のため、
    //    十分離れた20番を割り当てている。オブジェクト数を増やす場合はここも調整すること）
    //==================================================
    ComPtr<ID3D12Resource> vertexResourceParticle = CreateBufferResource(device.Get(), sizeof(VertexData) * 6);
    VertexData* vertexDataParticle = nullptr;
    vertexResourceParticle->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataParticle));
    vertexDataParticle[0].position = { -0.5f,  0.5f, 0.0f, 1.0f }; vertexDataParticle[0].texcoord = { 0.0f, 0.0f };
    vertexDataParticle[1].position = { -0.5f, -0.5f, 0.0f, 1.0f }; vertexDataParticle[1].texcoord = { 0.0f, 1.0f };
    vertexDataParticle[2].position = { 0.5f,  0.5f, 0.0f, 1.0f }; vertexDataParticle[2].texcoord = { 1.0f, 0.0f };
    vertexDataParticle[3].position = { 0.5f,  0.5f, 0.0f, 1.0f }; vertexDataParticle[3].texcoord = { 1.0f, 0.0f };
    vertexDataParticle[4].position = { -0.5f, -0.5f, 0.0f, 1.0f }; vertexDataParticle[4].texcoord = { 0.0f, 1.0f };
    vertexDataParticle[5].position = { 0.5f, -0.5f, 0.0f, 1.0f }; vertexDataParticle[5].texcoord = { 1.0f, 1.0f };
    for (int i = 0; i < 6; ++i) {
        vertexDataParticle[i].normal = { 0.0f, 0.0f, -1.0f };
    }

    D3D12_VERTEX_BUFFER_VIEW vertexBufferViewParticle{};
    vertexBufferViewParticle.BufferLocation = vertexResourceParticle->GetGPUVirtualAddress();
    vertexBufferViewParticle.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferViewParticle.StrideInBytes = sizeof(VertexData);

    ComPtr<ID3D12Resource> particleResource = CreateBufferResource(device.Get(), sizeof(ParticleForGPU) * kMaxParticles);
    ParticleForGPU* particleForGPUData = nullptr;
    particleResource->Map(0, nullptr, reinterpret_cast<void**>(&particleForGPUData));
    for (uint32_t i = 0; i < kMaxParticles; ++i) {
        particleForGPUData[i].WVP = Matrix4x4::MakeIdentity4x4();
        particleForGPUData[i].color = { 1.0f, 1.0f, 1.0f, 0.0f }; // 初期状態は透明
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC particleSrvDesc{};
    particleSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    particleSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    particleSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    particleSrvDesc.Buffer.FirstElement = 0;
    particleSrvDesc.Buffer.NumElements = kMaxParticles;
    particleSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
    particleSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    constexpr uint32_t kParticleSrvSlot = 20;
    D3D12_CPU_DESCRIPTOR_HANDLE particleSrvHandleCPU =
        GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, kParticleSrvSlot);
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrvHandleGPU =
        GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, kParticleSrvSlot);
    device->CreateShaderResourceView(particleResource.Get(), &particleSrvDesc, particleSrvHandleCPU);

    std::vector<Particle> particles;
    std::mt19937 particleRandomEngine{ std::random_device{}() };

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

    D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU =
        GetCPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU =
        GetGPUDescriptorHandle(srvDescriptorHeap.Get(), descriptorSizeSRV, 1);

    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, textureSrvHandleCPU);

    D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
    vertexBufferViewSprite.BufferLocation = vertexResourceSprite.Get()->GetGPUVirtualAddress();
    vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
    vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

    D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};
    indexBufferViewSprite.BufferLocation = indexResourceSprite.Get()->GetGPUVirtualAddress();
    indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
    indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

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

    Transform cameraTransform{ {1.0f,1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };
    Transform transformSprite{ {1.0f,1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
    Transform uvTransformSprite{ {1.0f,1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    DebugCamera debugCamera;
    debugCamera.Initialize(static_cast<float>(kClientWidth) / kClientHeight);

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
    DIJOYSTATE2 joyState{};
    DIJOYSTATE2 prevJoyState{};
    bool hasGamepad = false;

    bool konamiPartyMode = false; // コナミコマンドで切り替わるアニメーション演出モード
    float konamiTime = 0.0f;      // パーティーモード中の演出用タイマー

    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else {
            //==================================================
            //DirectInputのキーボード情報取得
            //==================================================
            memcpy(prevKey, key, sizeof(key));
            keyboard->Acquire();
            keyboard->GetDeviceState(sizeof(key), key);

            //==================================================
            // ゲームパッドの情報取得（接続されている場合のみ）
            //==================================================
            prevJoyState = joyState;
            hasGamepad = false;
            if (joystick != nullptr) {
                joystick->Poll();
                HRESULT joyHr = joystick->GetDeviceState(sizeof(DIJOYSTATE2), &joyState);
                if (FAILED(joyHr)) {
                    // フォーカスが外れる等でデバイスをロストした場合は再取得を試みる
                    joystick->Acquire();
                    joyHr = joystick->GetDeviceState(sizeof(DIJOYSTATE2), &joyState);
                }
                hasGamepad = SUCCEEDED(joyHr);
            }

            debugCamera.Update(key, prevKey, hasGamepad ? &joyState : nullptr);

            keyboard->Acquire();
            keyboard->GetDeviceState(sizeof(key), key);

            //==================================================
            // BGMのON/OFF切り替え（SPACEキー or ゲームパッドBボタン）
            // ※SPACEキーはDebugCameraの上移動にも使われているため、
            //   カメラを上に動かすと同時にBGMも切り替わる点に注意
            //==================================================
            bool bgmTogglePressed = TriggerKey(key, DIK_SPACE, prevKey);
            if (hasGamepad) {
                // Bボタンの並びはコントローラーにより異なる場合がある。反応しない場合は
                // ImGuiの"GamePad"パネルで実際に押した時のボタン番号を確認して差し替えること
                bgmTogglePressed = bgmTogglePressed || GamepadButtonTrigger(joyState, prevJoyState, 1);
            }
            if (bgmTogglePressed) {
                bgmPlaying = !bgmPlaying;
                if (bgmPlaying) {
                    bgmVoice->Start();
                }
                else {
                    bgmVoice->Stop();
                }
            }

            //==================================================
            // コナミコマンド（↑↑↓↓←→←→BA）判定
            // 成立するたびに全オブジェクトの「パーティーモード」演出をトグルする
            //==================================================
            if (CheckKonamiCode(key, prevKey, hasGamepad ? &joyState : nullptr, hasGamepad ? &prevJoyState : nullptr)) {
                konamiPartyMode = !konamiPartyMode;
                Log(logStream, konamiPartyMode ? "Konami Code accepted! Party mode ON" : "Konami Code accepted! Party mode OFF");

                if (konamiPartyMode) {
                    // ON: 現在の色を退避しておく（OFFにした時に戻すため）
                    for (auto& object : objects) {
                        object.konamiOriginalColor = object.materialData->color;
                    }
                }
                else {
                    // OFF: 退避しておいた色に戻す
                    for (auto& object : objects) {
                        object.materialData->color = object.konamiOriginalColor;
                    }
                }
            }

#ifdef USE_IMGUI
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("Debug Settings");

            if (ImGui::TreeNode("Objects")) {
                for (size_t i = 0; i < objects.size(); ++i) {
                    std::string label = std::format("[{}] {}", i, objFileList[objects[i].objIndex]);
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::TreeNode(label.c_str())) {
                        ImGui::Combo("Obj File", &objects[i].objIndex, objFileListCStr.data(), static_cast<int>(objFileListCStr.size()));
                        ImGui::DragFloat3("Scale", &objects[i].transform.Scale.x, 0.01f);
                        ImGui::DragFloat3("Rotate", &objects[i].transform.Rotate.x, 0.01f);
                        ImGui::DragFloat3("Translate", &objects[i].transform.Translate.x, 0.01f);
                        ImGui::ColorEdit4("Color", &objects[i].materialData->color.x);
                        const char* lightingModes[] = { "normal", "Lambert", "Half Lambert" };
                        ImGui::Combo("Lighting Mode", &objects[i].materialData->lightingMode, lightingModes, IM_ARRAYSIZE(lightingModes));
                        if (ImGui::TreeNode("UV Transform")) {
                            ImGui::DragFloat2("UVTranslate", &objects[i].uvTransform.Translate.x, 0.01f, -10.0f, 10.0f);
                            ImGui::DragFloat2("UVScale", &objects[i].uvTransform.Scale.x, 0.01f, -10.0f, 10.0f);
                            ImGui::SliderAngle("UVRotate", &objects[i].uvTransform.Rotate.z);
                            ImGui::TreePop();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("GamePad")) {
                if (hasGamepad) {
                    ImGui::Text("Connected");
                    ImGui::Text("Left Stick  (lX, lY)  : %ld, %ld", joyState.lX, joyState.lY);
                    ImGui::Text("Right Stick (lRx, lRy): %ld, %ld", joyState.lRx, joyState.lRy);
                    ImGui::Text("Z / Rz                : %ld, %ld", joyState.lZ, joyState.lRz);
                    ImGui::Text("POV(D-Pad)            : %lu", joyState.rgdwPOV[0]);
                    std::string buttons;
                    for (int i = 0; i < 32; ++i) {
                        if (joyState.rgbButtons[i] & 0x80) {
                            buttons += std::format("[{}] ", i);
                        }
                    }
                    ImGui::Text("Buttons: %s", buttons.c_str());
                }
                else {
                    ImGui::Text("Not connected (keyboard only)");
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Sound")) {
                ImGui::Text("BGM: %s", bgmPlaying ? "Playing" : "Stopped");
                ImGui::Text("Toggle: SPACE key / GamePad B button");
                if (ImGui::Button(bgmPlaying ? "Stop BGM" : "Play BGM")) {
                    bgmPlaying = !bgmPlaying;
                    if (bgmPlaying) {
                        bgmVoice->Start();
                    }
                    else {
                        bgmVoice->Stop();
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Command")) {
                ImGui::Text("Party Mode: %s", konamiPartyMode ? "ON" : "OFF");
                ImGui::Text("Particles: %zu / %u", particles.size(), kMaxParticles);
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

            ImGui::End();
            ImGui::Render();
#endif // USE_IMGUI

            Matrix4x4 viewMatrix = debugCamera.GetViewMatrix();
            Matrix4x4 projectionMatrix = debugCamera.GetProjectionMatrix();

            //==================================================
            // 各オブジェクトのWVPを更新する
            //==================================================
            if (konamiPartyMode) {
                konamiTime += 0.05f;
            }

            for (auto& object : objects) {
                object.transform.Rotate.y += 0.03f;

                Vector3 renderScale = object.transform.Scale;
                Vector3 renderRotate = object.transform.Rotate;

                if (konamiPartyMode) {
                    // パーティーモード：高速回転＋軸ごとに位相をずらした派手な拡縮パルス＋虹色サイクル
                    renderRotate.x += konamiTime * 4.0f;
                    renderRotate.y += konamiTime * 6.0f;

                    const float kPulseAmplitude = 0.6f;
                    const float kPulseSpeed = 5.0f;
                    float pulseX = 1.0f + kPulseAmplitude * std::sinf(konamiTime * kPulseSpeed);
                    float pulseY = 1.0f + kPulseAmplitude * std::sinf(konamiTime * kPulseSpeed + 2.094f); // +120度ずらす
                    float pulseZ = 1.0f + kPulseAmplitude * std::sinf(konamiTime * kPulseSpeed + 4.188f); // +240度ずらす
                    renderScale = {
                        object.transform.Scale.x * pulseX,
                        object.transform.Scale.y * pulseY,
                        object.transform.Scale.z * pulseZ
                    };

                    float hue = std::fmodf(konamiTime * 90.0f, 360.0f);
                    object.materialData->color = HueToColor(hue);
                }

                Matrix4x4 worldMatrix = Matrix4x4::MakeAffineMatrix(renderScale, renderRotate, object.transform.Translate);
                Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
                object.wvpData->WVP = worldViewProjectionMatrix;
                object.wvpData->World = worldMatrix;

                Matrix4x4 objectUvTransformMatrix = Matrix4x4::MakeScaleMatrix(object.uvTransform.Scale);
                objectUvTransformMatrix = Multiply(objectUvTransformMatrix, Matrix4x4::MakeRotateZMatrix(object.uvTransform.Rotate.z));
                objectUvTransformMatrix = Multiply(objectUvTransformMatrix, Matrix4x4::MakeTranslateMatrix(object.uvTransform.Translate));
                object.materialData->uvTransform = objectUvTransformMatrix;
            }

            //==================================================
            // パーティクルの更新（パーティーモードのON/OFFに関わらず、生存中のものは常に更新する）
            //==================================================
            {
                constexpr float kParticleDeltaTime = 1.0f / 60.0f;
                constexpr float kGravity = 3.0f;

                // カメラの位置と向き(right/up/forward)を取得する
                Matrix4x4 cameraWorldMatrix = Inverse(viewMatrix);
                Vector3 cameraRight = { cameraWorldMatrix.m[0][0], cameraWorldMatrix.m[0][1], cameraWorldMatrix.m[0][2] };
                Vector3 cameraUp = { cameraWorldMatrix.m[1][0], cameraWorldMatrix.m[1][1], cameraWorldMatrix.m[1][2] };
                Vector3 cameraForward = { cameraWorldMatrix.m[2][0], cameraWorldMatrix.m[2][1], cameraWorldMatrix.m[2][2] };
                Vector3 cameraPosition = { cameraWorldMatrix.m[3][0], cameraWorldMatrix.m[3][1], cameraWorldMatrix.m[3][2] };

                // パーティーモード中は、カメラ前方の平面上のランダムな位置（＝画面全体）からパーティクルを発生させる
                if (konamiPartyMode) {
                    constexpr int kSpawnCountPerFrame = 6;
                    constexpr float kEmitDistance = 15.0f;   // カメラからの発生距離
                    constexpr float kEmitHalfWidth = 12.0f;  // 画面の横方向の広がり
                    constexpr float kEmitHalfHeight = 7.0f;  // 画面の縦方向の広がり
                    std::uniform_real_distribution<float> distOffsetX(-kEmitHalfWidth, kEmitHalfWidth);
                    std::uniform_real_distribution<float> distOffsetY(-kEmitHalfHeight, kEmitHalfHeight);

                    for (int i = 0; i < kSpawnCountPerFrame && particles.size() < kMaxParticles; ++i) {
                        float offsetX = distOffsetX(particleRandomEngine);
                        float offsetY = distOffsetY(particleRandomEngine);

                        Vector3 emitPosition = {
                            cameraPosition.x + cameraForward.x * kEmitDistance + cameraRight.x * offsetX + cameraUp.x * offsetY,
                            cameraPosition.y + cameraForward.y * kEmitDistance + cameraRight.y * offsetX + cameraUp.y * offsetY,
                            cameraPosition.z + cameraForward.z * kEmitDistance + cameraRight.z * offsetX + cameraUp.z * offsetY
                        };

                        particles.push_back(MakeRandomPartyParticle(emitPosition, particleRandomEngine));
                    }
                }

                for (auto& particle : particles) {
                    particle.currentTime += kParticleDeltaTime;
                    particle.velocity.y -= kGravity * kParticleDeltaTime;
                    particle.position = Add(particle.position, {
                        particle.velocity.x * kParticleDeltaTime,
                        particle.velocity.y * kParticleDeltaTime,
                        particle.velocity.z * kParticleDeltaTime
                        });
                }

                particles.erase(
                    std::remove_if(particles.begin(), particles.end(),
                        [](const Particle& p) { return p.currentTime >= p.lifeTime; }),
                    particles.end());

                // カメラの回転成分だけを取り出したビルボード行列（平行移動は各パーティクルの位置を使う）
                cameraWorldMatrix.m[3][0] = 0.0f;
                cameraWorldMatrix.m[3][1] = 0.0f;
                cameraWorldMatrix.m[3][2] = 0.0f;

                for (size_t i = 0; i < particles.size(); ++i) {
                    const Particle& particle = particles[i];
                    float lifeRatio = particle.currentTime / particle.lifeTime; // 0(誕生)～1(消滅)
                    float alpha = 1.0f - lifeRatio; // だんだん透明に

                    Matrix4x4 scaleMatrix = Matrix4x4::MakeScaleMatrix({ particle.scale, particle.scale, particle.scale });
                    Matrix4x4 translateMatrix = Matrix4x4::MakeTranslateMatrix(particle.position);
                    Matrix4x4 worldMatrixParticle = Multiply(scaleMatrix, Multiply(cameraWorldMatrix, translateMatrix));
                    Matrix4x4 wvpParticle = Multiply(worldMatrixParticle, Multiply(viewMatrix, projectionMatrix));

                    particleForGPUData[i].WVP = wvpParticle;
                    particleForGPUData[i].color = { particle.color.x, particle.color.y, particle.color.z, alpha };
                }
            }

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

            //==================================================
            // ImGuiでobjが切り替えられたオブジェクトの頂点バッファ・テクスチャを再構築する
            // （直前フレームのGPU処理はループ末尾のfence waitで完了済みのためここで安全に差し替え可能）
            // テクスチャは生成時に確保したSRVスロット(textureSrvHandleCPU/GPU)を使い回す
            //==================================================
            std::vector<ComPtr<ID3D12Resource>> intermediateResourcesReload; // アップロード完了まで生存させる必要がある
            for (auto& object : objects) {
                if (object.objIndex == object.loadedObjIndex) {
                    continue;
                }

                object.modelData = (objFileList[object.objIndex] == kSphereProceduralTag)
                    ? GenerateSphere(16)
                    : LoadObj("resources", objFileList[object.objIndex]);

                BuildSubMeshes(object, device.Get(), commandList.Get(), srvDescriptorHeap.Get(), descriptorSizeSRV, intermediateResourcesReload);

                object.loadedObjIndex = object.objIndex;
            }

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

            commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource.Get()->GetGPUVirtualAddress());

            commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            //==================================================
            // 複数オブジェクトの描画（MultiMesh/MultiMaterial対応: オブジェクトごとに複数サブメッシュを描画）
            // マテリアル(色・ライティング)とWVPはオブジェクト単位、テクスチャ・頂点バッファはサブメッシュ単位で差し替える
            //==================================================
            for (auto& object : objects) {
                commandList->SetGraphicsRootConstantBufferView(0, object.materialResource->GetGPUVirtualAddress());
                commandList->SetGraphicsRootConstantBufferView(1, object.wvpResource->GetGPUVirtualAddress());
                for (auto& subMesh : object.subMeshes) {
                    if (subMesh.vertexCount == 0) {
                        continue; // 空メッシュ(GPUリソース未生成)はスキップ
                    }
                    commandList->SetGraphicsRootDescriptorTable(2, subMesh.textureSrvHandleGPU);
                    commandList->IASetVertexBuffers(0, 1, &subMesh.vertexBufferView);
                    commandList->DrawInstanced(UINT(subMesh.vertexCount), 1, 0, 0);
                }
            }

            //==================================================
            // Sprite描画
            //==================================================
            commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite.Get()->GetGPUVirtualAddress());
            commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
            commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite.Get()->GetGPUVirtualAddress());
            commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
            commandList->DrawInstanced(6, 1, 0, 0);

            //==================================================
            // パーティクル描画（専用のルートシグネチャ/PSOに切り替える。以降Sprite/Objectの描画はないため戻す必要はない）
            //==================================================
            if (!particles.empty()) {
                commandList->SetGraphicsRootSignature(particleRootSignature.Get());
                commandList->SetPipelineState(particlePipelineState.Get());
                commandList->IASetVertexBuffers(0, 1, &vertexBufferViewParticle);
                commandList->SetGraphicsRootDescriptorTable(0, particleSrvHandleGPU);
                commandList->DrawInstanced(6, UINT(particles.size()), 0, 0);
            }

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

    bgmVoice->Stop();
    bgmVoice->DestroyVoice();
    SoundUnload(&bgmSoundData);
    masterVoice->DestroyVoice();

    keyboard->Unacquire();
    keyboard->Release();
    if (joystick != nullptr) {
        joystick->Unacquire();
        joystick->Release();
    }
    directInput->Release();

    CloseHandle(fenceEvent);

    CloseWindow(hwnd);

    CoUninitialize();

    return 0;
}