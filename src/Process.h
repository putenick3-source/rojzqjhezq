// Process.h - External process attach + memory read helpers.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <type_traits>

#pragma comment(lib, "psapi.lib")

class Process {
public:
    Process() = default;

    ~Process() {
        detach();
    }

    // Finds a running process by executable name (case-insensitive) and opens
    // a handle with read + query rights. Returns true on success.
    bool attach(const wchar_t* exeName) {
        detach();

        const DWORD pid = findPid(exeName);
        if (!pid) return false;

        HANDLE h = OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
            FALSE,
            pid);
        if (!h) return false;

        pid_    = pid;
        handle_ = h;

        if (!resolveModule(exeName, moduleBase_, moduleSize_)) {
            detach();
            return false;
        }
        return true;
    }

    void detach() {
        if (handle_) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
        pid_        = 0;
        moduleBase_ = 0;
        moduleSize_ = 0;
    }

    bool isAttached() const { return handle_ != nullptr; }
    DWORD pid()        const { return pid_; }
    HANDLE handle()    const { return handle_; }
    uintptr_t base()   const { return moduleBase_; }
    size_t    size()   const { return moduleSize_; }

    // Confirms the process is still running. Call periodically from the main
    // loop so the overlay can re-attach when the game exits / restarts.
    bool stillAlive() const {
        if (!handle_) return false;
        DWORD code = 0;
        if (!GetExitCodeProcess(handle_, &code)) return false;
        return code == STILL_ACTIVE;
    }

    // --- Typed memory reads ---

    template <typename T>
    bool read(uintptr_t address, T& out) const {
        static_assert(std::is_trivially_copyable_v<T>, "T must be POD-like");
        if (!handle_ || !address) return false;
        SIZE_T n = 0;
        return ReadProcessMemory(handle_, (LPCVOID)address, &out, sizeof(T), &n)
            && n == sizeof(T);
    }

    template <typename T>
    T readOr(uintptr_t address, T fallback = {}) const {
        T v{};
        return read(address, v) ? v : fallback;
    }

    bool readRaw(uintptr_t address, void* buffer, size_t bytes) const {
        if (!handle_ || !address || !buffer) return false;
        SIZE_T n = 0;
        return ReadProcessMemory(handle_, (LPCVOID)address, buffer, bytes, &n)
            && n == bytes;
    }

    // Walks a pointer chain: base -> +offset[0] -> +offset[1] -> ...
    // Returns the final address (NOT the value at it) or 0 on failure.
    uintptr_t chain(uintptr_t base, const std::vector<uintptr_t>& offsets) const {
        uintptr_t addr = base;
        for (size_t i = 0; i < offsets.size(); ++i) {
            if (i + 1 < offsets.size()) {
                if (!read<uintptr_t>(addr + offsets[i], addr) || !addr) return 0;
            } else {
                addr += offsets[i];
            }
        }
        return addr;
    }

    // Reads a fixed-size inline char string at address (NUL-terminated or
    // padded). Safe against unreadable memory.
    std::string readInlineString(uintptr_t address, size_t maxLen = 32) const {
        if (!handle_ || !address) return {};
        std::vector<char> buf(maxLen + 1, 0);
        if (!readRaw(address, buf.data(), maxLen)) return {};
        buf[maxLen] = '\0';
        return std::string(buf.data());
    }

private:
    static DWORD findPid(const wchar_t* exeName) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe{ sizeof(pe) };
        DWORD pid = 0;
        if (Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return pid;
    }

    bool resolveModule(const wchar_t* moduleName,
                       uintptr_t& base,
                       size_t& size) const {
        base = 0;
        size = 0;

        HMODULE mods[1024];
        DWORD needed = 0;
        if (!EnumProcessModulesEx(handle_, mods, sizeof(mods), &needed,
                                  LIST_MODULES_ALL)) {
            return false;
        }
        const DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; ++i) {
            wchar_t name[MAX_PATH]{};
            if (!GetModuleBaseNameW(handle_, mods[i], name, _countof(name))) continue;
            if (_wcsicmp(name, moduleName) == 0) {
                MODULEINFO info{};
                if (GetModuleInformation(handle_, mods[i], &info, sizeof(info))) {
                    base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
                    size = info.SizeOfImage;
                    return true;
                }
            }
        }
        return false;
    }

    DWORD     pid_        = 0;
    HANDLE    handle_     = nullptr;
    uintptr_t moduleBase_ = 0;
    size_t    moduleSize_ = 0;
};
