// DriverClient.cpp - SYMLINK open + batched RR_IOCTL_BATCH / RR_IOCTL_MODULE.

#include "DriverClient.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// The driver picks a random suffix per load; find the symlink by prefix.
static std::wstring rr_find_symlink() {
    std::vector<wchar_t> buf(1u << 16);
    const DWORD n = QueryDosDeviceW(nullptr, buf.data(), (DWORD)buf.size());
    if (!n) return {};

    const wchar_t* p = buf.data();
    while (*p) {
        const size_t len = std::wcslen(p);
        if (len > 3 && _wcsnicmp(p, RR_NAME_PREFIX, 3) == 0) return std::wstring(p, len);
        p += len + 1;
    }
    return {};
}

bool DriverClient::open(DWORD pid) {
    close();
    if (!pid) return false;

    const std::wstring link = rr_find_symlink();
    if (link.empty()) return false;
    const std::wstring path = L"\\\\.\\" + link;

    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    handle_ = handle;
    pid_    = pid;
    return true;
}

void DriverClient::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    pid_ = 0;
}

bool DriverClient::read(uintptr_t address, void* out, size_t size) const {
    if (!ready() || !out || !address || !size) return false;

    uint8_t* dst = static_cast<uint8_t*>(out);
    size_t   done = 0;

    while (done < size) {
        RrBatchRequest req{};
        req.pid = pid_;

        std::vector<uint8_t> reply(sizeof(RrBatchRequest));
        size_t   payload = sizeof(RrBatchRequest);
        uint32_t count   = 0;

        while (count < RR_MAX_BATCH && done < size) {
            const size_t chunk = std::min<size_t>(size - done, RR_MAX_CHUNK);

            RrBatchEntry& entry = req.entries[count];
            entry.address = address + done;
            entry.size    = static_cast<uint32_t>(chunk);
            entry.offset  = static_cast<uint32_t>(payload);

            payload += chunk;
            ++count;
            done += chunk;
        }
        req.count = count;
        reply.resize(payload);

        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(
            handle_, RR_IOCTL_BATCH, &req, static_cast<DWORD>(sizeof(req)),
            reply.data(), static_cast<DWORD>(payload), &returned, nullptr);

        const size_t usable = std::min<size_t>(returned, payload);
        size_t       offset = 0;

        for (uint32_t i = 0; i < count; ++i) {
            const RrBatchEntry& entry = req.entries[i];
            const size_t chunk = entry.size;
            if (entry.offset + chunk <= usable) {
                std::memcpy(dst + offset, reply.data() + entry.offset, chunk);
            } else {
                std::memset(dst + offset, 0, chunk);
            }
            offset += chunk;
        }

        if (!ok && usable < payload) return false;
    }
    return true;
}

bool DriverClient::moduleBase(const wchar_t* name, uintptr_t& base,
                              size_t& size) const {
    if (!ready() || !name) return false;

    RrModuleRequest req{};
    req.pid = pid_;
    constexpr size_t kNameChars = sizeof(req.name) / sizeof(req.name[0]);
    for (size_t i = 0; i + 1 < kNameChars && name[i]; ++i) {
        req.name[i] = name[i];
    }

    RrModuleReply reply{};
    DWORD returned = 0;
    if (!DeviceIoControl(handle_, RR_IOCTL_MODULE, &req,
                         static_cast<DWORD>(sizeof(req)), &reply,
                         static_cast<DWORD>(sizeof(reply)), &returned, nullptr)) {
        return false;
    }
    if (returned < sizeof(reply) || !reply.base) return false;

    base = static_cast<uintptr_t>(reply.base);
    size = static_cast<size_t>(reply.size);
    return true;
}
