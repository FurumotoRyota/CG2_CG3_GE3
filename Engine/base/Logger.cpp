#include "Logger.h"
#include <Windows.h>
#include <chrono>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>

namespace
{
    std::ofstream g_logStream;
    std::deque<std::string> g_history; // 画面に出す用の履歴
    std::mutex g_historyMutex;
    constexpr size_t kMaxHistory = 1000;
}

namespace Logger
{
    void Initialize()
    {
        std::filesystem::create_directory("logs");

        auto now = std::chrono::system_clock::now();
        auto nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
        std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

        std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
        g_logStream.open("logs/" + dateString + ".log");
    }

    void Log(const std::string& message)
    {
        if (g_logStream.is_open()) {
            g_logStream << message << std::endl;
        }
        OutputDebugStringA((message + "\n").c_str());
        {
            std::lock_guard<std::mutex> lock(g_historyMutex);
            g_history.push_back(message);
            if (g_history.size() > kMaxHistory) {
                g_history.pop_front();
            }
        }
    }

    std::vector<std::string> GetHistory()
    {
        std::lock_guard<std::mutex> lock(g_historyMutex);
        return std::vector<std::string>(g_history.begin(), g_history.end());
    }

    void ClearHistory()
    {
        std::lock_guard<std::mutex> lock(g_historyMutex);
        g_history.clear();
    }

    void Finalize()
    {
        if (g_logStream.is_open()) {
            g_logStream.close();
        }
    }
}
