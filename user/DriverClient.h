// DriverClient.h - user-mode side of RrSvc.sys.
//
// Wraps the device handle and the two IOCTLs. Reads are split into
// RR_MAX_CHUNK pieces and packed up to RR_MAX_BATCH per call; every call is
// still read-only, nothing is written to the target process.

#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstddef>

#include "Shared.h"

class DriverClient {
public:
    DriverClient() = default;

    ~DriverClient() { close(); }

    DriverClient(const DriverClient&) = delete;
    DriverClient& operator=(const DriverClient&) = delete;

    // Opens the control device and binds it to `pid`. False when the driver is
    // not loaded (or the symlink cannot be reached).
    bool open(DWORD pid);

    void close();

    bool  ready() const { return handle_ != INVALID_HANDLE_VALUE && pid_ != 0; }
    DWORD pid()   const { return pid_; }

    // Batched read into `out`. False on any unusable reply.
    bool read(uintptr_t address, void* out, size_t size) const;

    // PEB walk through RR_IOCTL_MODULE. `name` is the module base name.
    bool moduleBase(const wchar_t* name, uintptr_t& base, size_t& size) const;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD  pid_    = 0;
};
