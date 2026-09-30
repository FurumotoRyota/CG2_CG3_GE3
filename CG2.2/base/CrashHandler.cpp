#include "CrashHandler.h"
#include <Windows.h>
#include <DbgHelp.h>
#include <strsafe.h>

#pragma comment(lib, "Dbghelp.lib")

namespace
{
    LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception)
    {
        SYSTEMTIME time{};
        GetLocalTime(&time);

        CreateDirectoryW(L"Dumps", nullptr);

        wchar_t filePath[MAX_PATH] = {};
        StringCchPrintfW(
            filePath, MAX_PATH, L"./Dumps/%04d-%02d-%02d-%02d%02d%02d.dmp",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);

        HANDLE dumpFileHandle = CreateFileW(
            filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (dumpFileHandle == INVALID_HANDLE_VALUE) {
            return EXCEPTION_EXECUTE_HANDLER;
        }

        MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{};
        minidumpInformation.ThreadId = GetCurrentThreadId();
        minidumpInformation.ExceptionPointers = exception;
        minidumpInformation.ClientPointers = TRUE;

        MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(), dumpFileHandle,
            MiniDumpNormal, &minidumpInformation, nullptr, nullptr);

        CloseHandle(dumpFileHandle);
        return EXCEPTION_EXECUTE_HANDLER;
    }
}

namespace CrashHandler
{
    void Install()
    {
        SetUnhandledExceptionFilter(ExportDump);
    }
}
