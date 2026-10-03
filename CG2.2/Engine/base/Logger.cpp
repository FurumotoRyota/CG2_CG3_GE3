#include "Logger.h"
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>

namespace
{
    std::ofstream g_logStream;
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
    }

    void Finalize()
    {
        if (g_logStream.is_open()) {
            g_logStream.close();
        }
    }
}
