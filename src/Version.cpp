// Version.cpp - Build version detection (EXE first, anchor module fallback).

#include "Version.h"
#include "Config.h"
#include "SDK.h"
#include "Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winver.h>

#include <cstring>
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

    void logVersionFailure(const wchar_t* what, const std::wstring& path) {
        if (path.empty()) {
            diag::log(L"buildguard: chemin %s introuvable, verdict Unknown", what);
        } else {
            diag::log(L"buildguard: aucune ressource de version dans %s, verdict Unknown",
                      path.c_str());
        }
    }

}

namespace buildguard {

    const wchar_t* verdictName(Verdict verdict) {
        switch (verdict) {
        case Verdict::Match:    return L"Match";
        case Verdict::Mismatch: return L"Mismatch";
        case Verdict::Unknown:
        default:                return L"Unknown";
        }
    }

    Status check(const Process& proc) {
        Status status;

        const std::wstring exe = imagePath(proc);
        status.current = fileVersion(exe);
        status.sourcePath = exe;

        if (status.current.empty()) {
            const std::wstring module = siblingModule(exe, sdk::kAnchorModule);
            status.current = fileVersion(module);
            status.sourcePath = module;

            if (!status.current.empty()) {
                diag::log(L"buildguard: version lue en repli dans %s",
                          module.c_str());
            } else {
                logVersionFailure(L"exe/module", exe);
            }
        }

        if (status.current.empty()) {
            status.verdict = Verdict::Unknown;
            return status;
        }

        // Compare only "major.minor.build"; the revision is ignored.
        const size_t prefixLen = std::wcslen(sdk::kDumpedVersion);
        const bool prefixMatch =
            status.current.rfind(sdk::kDumpedVersion, 0) == 0 &&
            (status.current.size() == prefixLen ||
             status.current[prefixLen] == L'.');

        status.verdict = prefixMatch ? Verdict::Match : Verdict::Mismatch;

        if (status.verdict == Verdict::Match) {
            diag::log(L"buildguard: build %s conforme (%s ignoree)",
                      status.current.c_str(), sdk::kDumpedVersion);
        } else {
            diag::log(L"buildguard: build %s != %s, ESP coupe",
                      status.current.c_str(), sdk::kDumpedVersion);
        }
        return status;
    }

    void record(const Status& status) {
        const std::wstring ini = config::iniPath();
        WritePrivateProfileStringW(L"Build", L"DumpedVersion",
                                   sdk::kDumpedVersion, ini.c_str());
        WritePrivateProfileStringW(L"Build", L"CurrentVersion",
                                   status.current.empty() ? L"unknown"
                                                          : status.current.c_str(),
                                   ini.c_str());
        WritePrivateProfileStringW(L"Build", L"Verdict",
                                   verdictName(status.verdict), ini.c_str());
    }

}

