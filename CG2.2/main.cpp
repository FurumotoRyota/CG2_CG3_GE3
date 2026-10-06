#include <Windows.h>

#include "base/D3DResourceLeakChecker.h"
#include "core/Game.h"

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    D3DResourceLeakChecker leakChecker;

    Game game;
    game.Run();

    return 0;
}
