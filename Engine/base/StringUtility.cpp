#include "StringUtility.h"
#include <Windows.h>

namespace StringUtility
{
    std::wstring ConvertString(const std::string& str)
    {
        if (str.empty()) {
            return std::wstring();
        }

        int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
        if (sizeNeeded == 0) {
            return std::wstring();
        }

        std::wstring result(sizeNeeded, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded);
        return result;
    }

    std::string ConvertString(const std::wstring& wstr)
    {
        if (wstr.empty()) {
            return std::string();
        }

        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        if (sizeNeeded == 0) {
            return std::string();
        }

        std::string result(sizeNeeded, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), sizeNeeded, nullptr, nullptr);
        return result;
    }
}
