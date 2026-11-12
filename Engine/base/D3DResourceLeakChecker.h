#pragma once
#include <Windows.h> // must come before dxgidebug.h (defines the SDK version macros)
#include <d3d12.h> // DXGI_DEBUG_D3D12 comes from the D3D12 headers
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

#pragma comment(lib, "dxguid.lib") // GUIDs for DXGI debug

/// <summary>
/// Reports leaked DirectX objects to the Visual Studio output when the app exits.
/// Declare it before the game object so it is destroyed after everything else is released.
/// </summary>
struct D3DResourceLeakChecker
{
    ~D3DResourceLeakChecker()
    {
        Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(debug.GetAddressOf())))) {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};
