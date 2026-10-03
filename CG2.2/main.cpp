#include <Windows.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

#include "core/Game.h"

#pragma comment(lib, "dxguid.lib") // DXGIデバッグ用GUID（リークチェッカー）

// アプリ終了時に解放漏れのDirectXオブジェクトをVisual Studioの出力に報告する
// ※Gameより先に宣言しているので、Gameが全て解放された後に呼ばれる
struct D3DResourceLeakChecker
{
    ~D3DResourceLeakChecker()
    {
        Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(debug.GetAddressOf()))))
        {
            debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
            debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
        }
    }
};

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    D3DResourceLeakChecker leakChecker;

    Game game;
    game.Run();

    return 0;
}
