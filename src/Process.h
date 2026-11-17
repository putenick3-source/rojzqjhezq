// Process.h - External process attach + memory read helpers.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <type_traits>

#include "DriverClient.h"

#pragma comment(lib, "psapi.lib")

class Process {
public:
    Process() = default;

    ~Process() {
        detach();
    }

    // Finds a running process by executable name and anchor module. The driver
    // is preferred when loaded; RPM remains the fallback. `anchorModule` is the
    // module offsets are relative to (defaults to the executable itself).
    bool attach(const wchar_t* exeName, const wchar_t* anchorModule = nullptr) {
        detach();

        const DWORD pid = findPid(exeName);
        if (!pid) return false;

        pid_ = pid;
        // Kept even when the driver answers: VirtualQueryEx backs regions().
        handle_    = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                                 FALSE, pid);
        useDriver_ = drv_.open(pid);
        if (!handle_ && !useDriver_) {
            detach();
            return false;
        }

        const wchar_t* anchor =
            (anchorModule && *anchorModule) ? anchorModule : exeName;

        bool resolved = false;
        if (useDriver_) {
            uintptr_t base = 0;
            size_t    size = 0;
            if (drv_.moduleBase(anchor, base, size)) {
                moduleBase_ = base;
                moduleSize_ = size;
                resolved    = true;
            }
        }
        if (!resolved && handle_) {
            resolved = resolveModule(anchor, moduleBase_, moduleSize_);
        }
        if (!resolved) {
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
        drv_.close();
        useDriver_  = false;
        pid_        = 0;
        moduleBase_ = 0;
        moduleSize_ = 0;
    }

    bool isAttached() const { return handle_ != nullptr || useDriver_; }
    bool usesDriver() const { return useDriver_; }
    DWORD pid()        const { return pid_; }
    HANDLE handle()    const { return handle_; }
    uintptr_t base()   const { return moduleBase_; }
    size_t    size()   const { return moduleSize_; }

    struct Region {
        uintptr_t base = 0;
        size_t    size = 0;
    };

    // True when [address, address + bytes) is committed, readable and not
    // guarded. Driver-only sessions cannot query mappings, so a non-zero
    // address is trusted and the read result decides.
    bool validate(uintptr_t address, size_t bytes) const {
        if (!address || !bytes) return false;
        if (!handle_) return useDriver_;
        uintptr_t cur = address;
        while (cur < address + bytes) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQueryEx(handle_, (LPCVOID)cur, &mbi, sizeof(mbi))) return false;
            if (mbi.State != MEM_COMMIT || !readable(mbi.Protect)) return false;
            const uintptr_t next =
                reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (next <= cur) return false;
            cur = next;
        }
        return true;
    }

    // Enumerates readable committed regions. Needs the RPM handle; driver-only
    // sessions return nothing. `budget` caps the reported bytes (0 = all).
    std::vector<Region> regions(size_t budget = 0) const {
        std::vector<Region> out;
        if (!handle_) return out;

        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        uintptr_t cur = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
        const uintptr_t last =
            reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

        size_t total = 0;
        while (cur < last) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQueryEx(handle_, (LPCVOID)cur, &mbi, sizeof(mbi))) break;

            const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            if (mbi.State == MEM_COMMIT && readable(mbi.Protect)) {
                size_t size = mbi.RegionSize;
                if (budget && total + size > budget) size = budget - total;
                if (size) {
                    out.push_back({ base, size });
                    total += size;
                }
                if (budget && total >= budget) break;
            }

            const uintptr_t next = base + mbi.RegionSize;
            if (next <= cur) break;
            cur = next;
        }
        return out;
    }

    // Confirms the process is still running. Call periodically from the main
    // loop so the overlay can re-attach when the game exits / restarts.
    bool stillAlive() const {
        if (handle_) {
            DWORD code = 0;
            if (!GetExitCodeProcess(handle_, &code)) return false;
            return code == STILL_ACTIVE;
        }
        if (!useDriver_) return false;
        // Driver-only: a read of the anchor base is the cheapest liveness probe.
        uint32_t probe = 0;
        return drv_.read(moduleBase_, &probe, sizeof(probe));
    }

    // --- Typed memory reads ---

    template <typename T>
    bool read(uintptr_t address, T& out) const {
        static_assert(std::is_trivially_copyable_v<T>, "T must be POD-like");
        if (!address) return false;
        if (useDriver_) return drv_.read(address, &out, sizeof(T));
        if (!handle_) return false;
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
        if (!address || !buffer) return false;
        if (useDriver_) return drv_.read(address, buffer, bytes);
        if (!handle_) return false;
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
        if (!address) return {};
        std::vector<char> buf(maxLen + 1, 0);
        if (!readRaw(address, buf.data(), maxLen)) return {};
        buf[maxLen] = '\0';
        return std::string(buf.data());
    }

private:
    static bool readable(DWORD protect) {
        if (protect & PAGE_GUARD) return false;
        switch (protect & 0xFF) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

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
    DriverClient drv_;
    bool      useDriver_  = false;
};
