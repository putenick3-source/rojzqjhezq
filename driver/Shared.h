// Shared.h - IOCTL contract between RrSvc.sys and DriverClient.
//
// Layout is fixed: the client builds one RrBatchRequest per IOCTL, with output
// chunks concatenated at `offset` inside the buffered system buffer. Sizes are
// compile-time so both sides agree without negotiation.

#pragma once

#ifdef _KERNEL_MODE
#include <wdm.h>

typedef unsigned __int64 uint64_t;
typedef unsigned int     uint32_t;
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#endif

#define RR_DEVICE_NAME  L"\\Device\\RrSvc94"
#define RR_SYMLINK_NAME L"\\DosDevices\\RrSvc94"

#define RR_IOCTL_BATCH  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define RR_IOCTL_MODULE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Largest single chunk the driver will copy.
#define RR_MAX_CHUNK 0x1000

// Chunks per batch request (16 * 0x1000 = 64 KB of payload maximum).
#define RR_MAX_BATCH 16

// Staging buffer size; every payload offset is validated against it.
#define RR_STAGING 0x10000

struct RrBatchEntry {
    uint64_t address;
    uint32_t size;
    uint32_t offset;
};

struct RrBatchRequest {
    uint32_t pid;
    uint32_t count;
    RrBatchEntry entries[RR_MAX_BATCH];
};

struct RrModuleRequest {
    uint32_t pid;
    uint32_t pad;
    wchar_t  name[64];
};

struct RrModuleReply {
    uint64_t base;
    uint64_t size;
};
