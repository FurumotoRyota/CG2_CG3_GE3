#pragma once
#include <string>

namespace StringUtility
{
    // string(UTF-8) -> wstring
    std::wstring ConvertString(const std::string& str);
    // wstring -> string(UTF-8)
    std::string ConvertString(const std::wstring& wstr);
}
