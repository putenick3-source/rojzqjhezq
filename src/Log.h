// Log.h - Minimal file/debug logger for the overlay itself.
//
// Diagnostics go to OutputDebugString and to RobRunOverlay.log next to the
// executable. The game process is never written to.

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace diag {

    // Directory containing the overlay executable (with trailing separator).
    inline std::wstring exeDir() {
        wchar_t path[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, path, _countof(path));
        std::wstring dir(path, n);
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return L".\\";
        dir.resize(slash + 1);
        return dir;
    }

    inline std::wstring logPath() { return exeDir() + L"RobRunOverlay.log"; }

    // printf-style, wide. Always NUL-terminates; silently truncates.
    inline void log(const wchar_t* fmt, ...) {
        wchar_t msg[1024]{};
        va_list args;
        va_start(args, fmt);
        _vsnwprintf_s(msg, _countof(msg), _TRUNCATE, fmt, args);
        va_end(args);

        SYSTEMTIME st{};
        GetLocalTime(&st);
        wchar_t line[1152]{};
        swprintf_s(line, L"[%02u:%02u:%02u] %s\r\n",
                   st.wHour, st.wMinute, st.wSecond, msg);

        OutputDebugStringW(line);

        HANDLE h = CreateFileW(logPath().c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;

        // UTF-8 keeps the log readable for non-ASCII paths / values.
        char utf8[2304]{};
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1,
                                              utf8, (int)sizeof(utf8),
                                              nullptr, nullptr);
        DWORD written = 0;
        if (bytes > 1) {
            WriteFile(h, utf8, (DWORD)(bytes - 1), &written, nullptr);
        }
        CloseHandle(h);
    }

}
