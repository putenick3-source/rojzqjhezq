// Version.cpp - GameAssembly.dll version detection.

#include "Version.h"
#include "Offsets.h"
#include "SDK.h"
#include "Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winver.h>

#include <vector>

#pragma comment(lib, "version.lib")

namespace {

    std::wstring imagePath(const Process& proc) {
        if (!proc.isAttached()) return {};

        std::vector<wchar_t> buf(32768, L'\0');
        DWORD len = (DWORD)buf.size();
        if (!QueryFullProcessImageNameW(proc.handle(), 0, buf.data(), &len)) {
            return {};
        }
        return std::wstring(buf.data(), len);
    }

    std::wstring siblingModule(const std::wstring& image, const wchar_t* name) {
        const size_t slash = image.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return {};
        std::wstring path = image.substr(0, slash + 1);
        path += name;
        return path;
    }

    std::wstring fileVersion(const std::wstring& path) {
        if (path.empty()) return {};

        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size == 0) return {};

        std::vector<BYTE> data(size, 0);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};

        VS_FIXEDFILEINFO* info = nullptr;
        UINT infoLen = 0;
        if (!VerQueryValueW(data.data(), L"\\",
                            reinterpret_cast<void**>(&info), &infoLen)) {
            return {};
        }
        if (!info || info->dwSignature != 0xFEEF04BD) return {};

        wchar_t buf[64]{};
        swprintf_s(buf, L"%u.%u.%u.%u",
                   (unsigned)HIWORD(info->dwFileVersionMS),
                   (unsigned)LOWORD(info->dwFileVersionMS),
                   (unsigned)HIWORD(info->dwFileVersionLS),
                   (unsigned)LOWORD(info->dwFileVersionLS));
        return buf;
    }

}

namespace buildguard {

    Status check(const Process& proc) {
        Status status;
        status.modulePath = siblingModule(imagePath(proc), sdk::kAnchorModule);
        if (status.modulePath.empty() ||
            GetFileAttributesW(status.modulePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            diag::log(L"buildguard: %s introuvable, ESP coupe", sdk::kAnchorModule);
            return status;
        }

        status.current = fileVersion(status.modulePath);
        if (status.current.empty()) {
            diag::log(L"buildguard: version illisible dans %s, ESP coupe",
                      status.modulePath.c_str());
            return status;
        }

        status.compatible =
            _wcsicmp(status.current.c_str(), sdk::kDumpedVersion) == 0;
        if (!status.compatible) {
            diag::log(L"buildguard: build %s != %s, ESP coupe",
                      status.current.c_str(), sdk::kDumpedVersion);
        }
        return status;
    }

    void record(const Status& status) {
        const std::wstring ini = offsets::iniPath();
        WritePrivateProfileStringW(L"Build", L"DumpedVersion",
                                   sdk::kDumpedVersion, ini.c_str());
        WritePrivateProfileStringW(L"Build", L"CurrentVersion",
                                   status.current.empty() ? L"unknown"
                                                          : status.current.c_str(),
                                   ini.c_str());

        if (status.compatible) {
            diag::log(L"buildguard: build %s conforme", status.current.c_str());
        }
    }

}
