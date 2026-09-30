#pragma once
#include <string>

/// <summary>
/// ログ出力（logs/日時.log と Visual Studio の出力ウィンドウの両方へ出す）
/// </summary>
namespace Logger
{
    void Initialize();
    void Log(const std::string& message);
    void Finalize();
}
