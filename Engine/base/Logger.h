#pragma once
#include <string>
#include <vector>

/// <summary>
/// ログ出力（logs/日時.log と Visual Studio の出力ウィンドウの両方へ出す）
/// </summary>
namespace Logger
{
    void Initialize();
    void Log(const std::string& message);
    void Finalize();

    // 画面に出す用に、最近のログ（最大1000行）を覚えておく
    std::vector<std::string> GetHistory(); // コピーを返す
    void ClearHistory();
}
