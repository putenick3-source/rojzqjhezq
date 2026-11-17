// RrSvc.sys - read-only kernel memory reader, manual-mapped.
//
// Goals, in order: never touch anything the game can see (no hooks, no
// callbacks, no threads), keep the kernel stack inside ntoskrnl while EAC is
// sampling NMI stacks, and leave as few traces as possible when unloaded.
//
// Only ntoskrnl is imported. No CRT: every copy/compare/zero below is a hand
// rolled loop, structs are initialised field by field, /GS- and /GR- are used.

#include <ntifs.h>
#include <ntimage.h>

#include "Shared.h"

// ExAllocatePoolWithTag is flagged deprecated in favour of ExAllocatePool2,
// which only exists from Windows 10 2004; the older API keeps the fallback
// usable on every kernel the overlay targets.
#pragma warning(disable : 4996)

// Diagnostic build switch (CMake target RrSvc_min / build.bat min). When
// defined the entry point stops as soon as the driver object is in place: no
// device, no symlink, no staging, no IOCTL wiring.
#ifndef RR_MINIMAL
#define RR_MINIMAL 0
#endif

// RUNTIME_FUNCTION lives in winnt.h, which the km headers do not pull in.
typedef IMAGE_RUNTIME_FUNCTION_ENTRY RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

// Exported by ntoskrnl but missing from the current km headers.
extern "C" NTSTATUS NTAPI MmCopyVirtualMemory(
    PEPROCESS fromProcess, PVOID fromAddress,
    PEPROCESS toProcess, PVOID toAddress,
    SIZE_T bufferSize, KPROCESSOR_MODE previousMode, PSIZE_T returnSize);
extern "C" PVOID NTAPI RtlPcToFileHeader(PVOID pcValue, PVOID* baseOfImage);
extern "C" PVOID NTAPI PsGetProcessPeb(PEPROCESS process);

// Exporte par ntoskrnl, absent des en-tetes km de ce kit.
extern "C" NTSTATUS NTAPI IoCreateDriver(PUNICODE_STRING name, PDRIVER_INITIALIZE entry);

// MmAllocateIndependentPages is exported on most kernels but not all; resolved
// at init so an import stub cannot fail the mapping on the others.
typedef PVOID(NTAPI* RR_ALLOC_INDEP)(SIZE_T);
typedef VOID(NTAPI* RR_FREE_INDEP)(PVOID, SIZE_T);

// Toggle to disable the unload-time trace scrub without rebuilding the rest.
bool RR_CLEAN_TRACES = false;

static PVOID          g_Staging = nullptr;
static BOOLEAN        g_StagingIsPool = false;
static PDEVICE_OBJECT g_Device = nullptr;
static ULONG          g_TimeDateStamp = 0;
static RR_ALLOC_INDEP g_AllocIndep = nullptr;
static RR_FREE_INDEP  g_FreeIndep = nullptr;

static WCHAR g_DeviceName[64];
static WCHAR g_SymlinkName[64];

// Device and symlink names are built per load: the prefix stays stable so the
// client can locate the symlink, the suffix never repeats across boots.
static void rr_build_names(void) {
    static const WCHAR kAlphabet[] = L"abcdefghijklmnopqrstuvwxyz0123456789";
    static const WCHAR kDevPrefix[] = L"\\Device\\" RR_NAME_PREFIX;
    static const WCHAR kLinkPrefix[] = L"\\DosDevices\\" RR_NAME_PREFIX;

    WCHAR suffix[9];
    ULONG64 v = (ULONG64)(ULONG_PTR)&rr_build_names ^ ((ULONG64)g_TimeDateStamp << 16);
    for (ULONG i = 0; i < 8; ++i) {
        suffix[i] = kAlphabet[(ULONG)(v % 36)];
        v /= 36;
    }
    suffix[8] = 0;

    ULONG i = 0;
    ULONG j = 0;
    while (kDevPrefix[i]) { g_DeviceName[i] = kDevPrefix[i]; ++i; }
    while (suffix[j]) { g_DeviceName[i++] = suffix[j++]; }
    g_DeviceName[i] = 0;

    i = 0;
    j = 0;
    while (kLinkPrefix[i]) { g_SymlinkName[i] = kLinkPrefix[i]; ++i; }
    while (suffix[j]) { g_SymlinkName[i++] = suffix[j++]; }
    g_SymlinkName[i] = 0;
}
static PVOID             g_ImageBase = nullptr;
static PRUNTIME_FUNCTION g_FnTable = nullptr;
static ULONG             g_FnCount = 0;

// PEB->Ldr offset of the 64-bit PEB.
static const ULONG kPebLdr = 0x18;

// Minimal loader structures; the km headers only forward-declare PEB.
typedef struct _RR_PEB_LDR_DATA {
    ULONG      Length;
    ULONG      Initialized;
    PVOID      SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} RR_PEB_LDR_DATA;

typedef struct _RR_LDR_ENTRY {
    LIST_ENTRY     InLoadOrderLinks;
    LIST_ENTRY     InMemoryOrderLinks;
    LIST_ENTRY     InInitializationOrderLinks;
    PVOID          DllBase;
    PVOID          EntryPoint;
    ULONG          SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} RR_LDR_ENTRY;

static_assert(FIELD_OFFSET(RR_LDR_ENTRY, DllBase) == 0x30, "loader entry layout");
static_assert(FIELD_OFFSET(RR_LDR_ENTRY, SizeOfImage) == 0x40, "loader entry layout");
static_assert(FIELD_OFFSET(RR_LDR_ENTRY, BaseDllName) == 0x58, "loader entry layout");

// Entry found in g_KernelHashBucketList.
typedef struct _RR_HASH_ENTRY {
    LIST_ENTRY     HashListEntry;
    UNICODE_STRING DriverName;
    ULONG          TimeDateStamp;
} RR_HASH_ENTRY;

// PiDDBCacheTable element.
typedef struct _RR_PIDDB_ENTRY {
    LIST_ENTRY     List;
    UNICODE_STRING DriverName;
    ULONG          TimeDateStamp;
    NTSTATUS       LoadStatus;
} RR_PIDDB_ENTRY;

// MmUnloadedDrivers element.
typedef struct _RR_UNLOADED_DRIVER {
    UNICODE_STRING Name;
    PVOID          StartAddress;
    PVOID          EndAddress;
    LARGE_INTEGER  CurrentTime;
} RR_UNLOADED_DRIVER;

// ---- primitives (no CRT) ----------------------------------------------------

static void rr_zero(PVOID dst, SIZE_T len) {
    PUCHAR p = (PUCHAR)dst;
    SIZE_T i = 0;
    while (i < len) {
        p[i] = 0;
        ++i;
    }
}

static void rr_copy(PVOID dst, const VOID* src, SIZE_T len) {
    PUCHAR d = (PUCHAR)dst;
    const UCHAR* s = (const UCHAR*)src;
    SIZE_T i = 0;
    while (i < len) {
        d[i] = s[i];
        ++i;
    }
}

static ULONG rr_len_w(const wchar_t* s, ULONG max) {
    ULONG i = 0;
    while (i < max && s[i]) ++i;
    return i;
}

static wchar_t rr_lower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return (wchar_t)(c + 32);
    return c;
}

static BOOLEAN rr_equal_ci_w(const wchar_t* a, const wchar_t* b, ULONG len) {
    ULONG i = 0;
    while (i < len) {
        if (rr_lower(a[i]) != rr_lower(b[i])) return FALSE;
        ++i;
    }
    return TRUE;
}

static BOOLEAN rr_contains_ci(const wchar_t* hay, ULONG hayLen,
                              const wchar_t* needle) {
    const ULONG n = rr_len_w(needle, 64);
    if (!n || n > hayLen) return FALSE;
    ULONG i = 0;
    while (i + n <= hayLen) {
        if (rr_equal_ci_w(hay + i, needle, n)) return TRUE;
        ++i;
    }
    return FALSE;
}

// ---- signature scan ---------------------------------------------------------

typedef struct _RR_SIG_BYTE {
    UCHAR value;
    UCHAR wild;
} RR_SIG_BYTE;

static ULONG rr_hex(UCHAR c) {
    if (c >= '0' && c <= '9') return (ULONG)(c - '0');
    if (c >= 'a' && c <= 'f') return (ULONG)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (ULONG)(c - 'A') + 10;
    return 0xFFFFFFFFu;
}

// "48 8B 0D ? ? ? ?" -> bytes + wildcard map. Fails on malformed input.
static BOOLEAN rr_parse_sig(const char* sig, RR_SIG_BYTE* out, ULONG cap,
                            ULONG* outLen) {
    ULONG n = 0;
    ULONG i = 0;

    while (sig[i]) {
        if (sig[i] == ' ') {
            ++i;
            continue;
        }
        if (n >= cap) return FALSE;

        if (sig[i] == '?') {
            out[n].value = 0;
            out[n].wild = 1;
            ++n;
            ++i;
            if (sig[i] == '?') ++i;
            continue;
        }

        const ULONG hi = rr_hex((UCHAR)sig[i]);
        if (hi == 0xFFFFFFFFu || !sig[i + 1]) return FALSE;
        const ULONG lo = rr_hex((UCHAR)sig[i + 1]);
        if (lo == 0xFFFFFFFFu) return FALSE;

        out[n].value = (UCHAR)((hi << 4) | lo);
        out[n].wild = 0;
        ++n;
        i += 2;
    }

    if (!n) return FALSE;
    *outLen = n;
    return TRUE;
}

static PUCHAR rr_find_pattern(PUCHAR base, SIZE_T size, const char* sig) {
    RR_SIG_BYTE pat[32];
    ULONG len = 0;
    if (!rr_parse_sig(sig, pat, 32, &len) || size < len) return nullptr;

    SIZE_T i = 0;
    while (i + len <= size) {
        ULONG j = 0;
        while (j < len && (pat[j].wild || base[i + j] == pat[j].value)) ++j;
        if (j == len) return base + i;
        ++i;
    }
    return nullptr;
}

// RIP-relative operand at `dispOff` in an instruction of `insnLen` bytes.
static ULONG64 rr_rip(PUCHAR at, ULONG dispOff, ULONG insnLen) {
    LONG disp = 0;
    rr_copy(&disp, at + dispOff, sizeof(disp));
    return (ULONG64)(at + insnLen) + (ULONG64)(LONG64)disp;
}

static PUCHAR rr_kernel_base(SIZE_T* size) {
    PVOID base = nullptr;
    *size = 0;
    if (!RtlPcToFileHeader((PVOID)&IoCreateDevice, &base) || !base) return nullptr;

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((PUCHAR)base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    *size = nt->OptionalHeader.SizeOfImage;
    return (PUCHAR)base;
}

static BOOLEAN rr_sane_ptr(PUCHAR base, SIZE_T size, ULONG64 p) {
    return p >= (ULONG64)base && p < (ULONG64)base + size;
}

// ---- trace cleanup ----------------------------------------------------------

// DriverObject->DriverName.Buffer lives in pool; wipe the text, keep the rest.
static void rr_scrub_name(PDRIVER_OBJECT drv) {
    if (!drv->DriverName.Buffer || !drv->DriverName.Length) return;
    rr_zero(drv->DriverName.Buffer, drv->DriverName.Length);
}

// The hash bucket list keeps one entry per loaded image.
static void rr_clean_hash(void) {
    SIZE_T size = 0;
    PUCHAR base = rr_kernel_base(&size);
    if (!base) return;

    PUCHAR hit = rr_find_pattern(base, size, "48 8B 0D ? ? ? ? 48 85 C9 74 24");
    if (!hit) return;

    LIST_ENTRY* head = (LIST_ENTRY*)rr_rip(hit, 3, 7);
    if (!rr_sane_ptr(base, size, (ULONG64)head)) return;

    LIST_ENTRY* cur = head->Flink;
    ULONG guard = 0x100;
    while (cur && cur != head && guard--) {
        RR_HASH_ENTRY* entry = (RR_HASH_ENTRY*)cur;
        LIST_ENTRY* next = cur->Flink;

        if (entry->TimeDateStamp == g_TimeDateStamp && entry->DriverName.Buffer &&
            rr_contains_ci(entry->DriverName.Buffer,
                           entry->DriverName.Length / sizeof(wchar_t), L"RrSvc")) {
            RemoveEntryList(&entry->HashListEntry);
            ExFreePool(entry);
        }
        cur = next;
    }
}

// Several ntoskrnl builds carry PiDDBCacheTable behind a different instruction;
// try the known shapes and keep the first that holds a plausible AVL table.
typedef struct _RR_PIDDB_SIG {
    const char* sig;
    ULONG       dispOff;
    BOOLEAN     deref;
} RR_PIDDB_SIG;

static const RR_PIDDB_SIG kPiddbSigs[] = {
    { "48 8D 0D ? ? ? ? E8 ? ? ? ? 4C 8B 35", 3, FALSE }, // lea rcx, PiDDBCacheTable
    { "48 8B 0D ? ? ? ? 8B 1D ? ? ? ? 4C 8B 35", 3, TRUE }, // mov rcx, [PiDDBCacheTable]
    { "48 8D 15 ? ? ? ? 48 8B 0D ? ? ? ? 48 85 C9", 10, TRUE },
};

static RTL_AVL_TABLE* rr_find_piddb(void) {
    SIZE_T size = 0;
    PUCHAR base = rr_kernel_base(&size);
    if (!base) return nullptr;

    for (ULONG i = 0; i < RTL_NUMBER_OF(kPiddbSigs); ++i) {
        PUCHAR hit = rr_find_pattern(base, size, kPiddbSigs[i].sig);
        if (!hit) continue;

        ULONG64 at = rr_rip(hit, kPiddbSigs[i].dispOff, 7);
        if (kPiddbSigs[i].deref) {
            if (!rr_sane_ptr(base, size, at)) continue;
            at = *(ULONG64*)at;
        }
        if (!rr_sane_ptr(base, size, at)) continue;

        RTL_AVL_TABLE* table = (RTL_AVL_TABLE*)at;
        if (!table->CompareRoutine ||
            !rr_sane_ptr(base, size, (ULONG64)table->CompareRoutine) ||
            !table->AllocateRoutine ||
            !rr_sane_ptr(base, size, (ULONG64)table->AllocateRoutine)) {
            continue;
        }
        return table;
    }
    return nullptr;
}

static void rr_clean_piddb(void) {
    RTL_AVL_TABLE* table = rr_find_piddb();
    if (!table) return;

    PVOID element = RtlEnumerateGenericTableAvl(table, TRUE);
    ULONG guard = 0x100;
    while (element && guard--) {
        RR_PIDDB_ENTRY* entry = (RR_PIDDB_ENTRY*)element;
        if (entry->TimeDateStamp == g_TimeDateStamp) {
            RemoveEntryList(&entry->List);
            RtlDeleteElementGenericTableAvl(table, element); // frees via FreeRoutine
            return;
        }
        element = RtlEnumerateGenericTableAvl(table, FALSE);
    }
}

// MmUnloadedDrivers keeps the last 0x32 unloaded images; scrub our name.
static void rr_clean_unloaded(void) {
    SIZE_T size = 0;
    PUCHAR base = rr_kernel_base(&size);
    if (!base) return;

    PUCHAR hit = rr_find_pattern(base, size,
                                 "4C 8B 25 ? ? ? ? 4C 8B 3D ? ? ? ?");
    if (!hit) return;

    RR_UNLOADED_DRIVER* drivers = (RR_UNLOADED_DRIVER*)rr_rip(hit, 3, 7);
    if (!rr_sane_ptr(base, size, (ULONG64)drivers)) return;

    for (ULONG i = 0; i < 0x32; ++i) {
        UNICODE_STRING* name = &drivers[i].Name;
        if (!name->Buffer || name->Length < 2 * sizeof(wchar_t)) continue;
        if (!rr_contains_ci(name->Buffer, name->Length / sizeof(wchar_t),
                            L"RrSvc")) {
            continue;
        }
        rr_zero(name->Buffer, name->Length);
        name->Length = 0;
        name->MaximumLength = 0;
    }
}

// Every step is isolated and silent: a failed scrub must never fail the unload.
static void rr_clean_traces(PDRIVER_OBJECT drv) {
    if (!RR_CLEAN_TRACES) return;

    __try { rr_scrub_name(drv); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try { rr_clean_hash(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try { rr_clean_piddb(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    __try { rr_clean_unloaded(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---- IOCTL workers ----------------------------------------------------------

static NTSTATUS rr_do_batch(PVOID input, ULONG inLen,
                            PVOID output, ULONG outLen, ULONG* info) {
    const ULONG header = sizeof(RrBatchRequest);
    if (!input || inLen < header) return STATUS_BUFFER_TOO_SMALL;

    const RrBatchRequest* req = (const RrBatchRequest*)input;
    if (!req->pid || !req->count || req->count > RR_MAX_BATCH) {
        return STATUS_INVALID_PARAMETER;
    }

    // Snapshot the entries: the output copy overwrites the system buffer.
    RrBatchEntry entries[RR_MAX_BATCH];
    rr_zero(entries, sizeof(entries));
    rr_copy(entries, req->entries, (SIZE_T)req->count * sizeof(RrBatchEntry));

    ULONG end = 0;
    for (ULONG i = 0; i < req->count; ++i) {
        const RrBatchEntry* e = &entries[i];
        if (!e->address || !e->size || e->size > RR_MAX_CHUNK) {
            return STATUS_INVALID_PARAMETER;
        }
        if (e->offset < header) return STATUS_INVALID_PARAMETER;
        if (e->offset + e->size < e->offset) return STATUS_INVALID_PARAMETER;
        if (e->offset + e->size > outLen) return STATUS_BUFFER_TOO_SMALL;
        if (e->offset - header + e->size > RR_STAGING) return STATUS_BUFFER_OVERFLOW;
        if (e->offset + e->size > end) end = e->offset + e->size;
    }

    PEPROCESS target = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId(
        (HANDLE)(ULONG_PTR)req->pid, &target);
    if (!NT_SUCCESS(status)) return status;

    PUCHAR out = (PUCHAR)output;
    NTSTATUS result = STATUS_SUCCESS;

    for (ULONG i = 0; i < req->count; ++i) {
        const RrBatchEntry* e = &entries[i];
        PUCHAR dst = (PUCHAR)g_Staging + (e->offset - header);
        SIZE_T copied = 0;

        status = MmCopyVirtualMemory(target, (PVOID)(ULONG_PTR)e->address,
                                     PsGetCurrentProcess(), dst, e->size,
                                     KernelMode, &copied);
        if (!NT_SUCCESS(status) || copied != e->size) {
            rr_zero(dst, e->size);
            if (NT_SUCCESS(result)) result = STATUS_PARTIAL_COPY;
        }
        rr_copy(out + e->offset, dst, e->size);
    }

    ObDereferenceObject(target);
    *info = end;
    return result;
}

// The target's user pages are not mapped in the caller's context, so every read
// of its structures goes through MmCopyVirtualMemory into a local copy.
static NTSTATUS rr_copy_proc(PEPROCESS target, PVOID address, PVOID local, SIZE_T size) {
    SIZE_T copied = 0;
    const NTSTATUS status = MmCopyVirtualMemory(target, address, PsGetCurrentProcess(),
                                                local, size, KernelMode, &copied);
    if (!NT_SUCCESS(status)) return status;
    return copied == size ? STATUS_SUCCESS : STATUS_PARTIAL_COPY;
}

static UNICODE_STRING rr_module_name(const RrModuleRequest* req) {
    UNICODE_STRING name;
    ULONG len = rr_len_w(req->name, RTL_NUMBER_OF(req->name) - 1);
    name.Buffer = (PWCH)req->name;
    name.Length = (USHORT)(len * sizeof(wchar_t));
    name.MaximumLength = (USHORT)(RTL_NUMBER_OF(req->name) * sizeof(wchar_t));
    return name;
}

static NTSTATUS rr_do_module(PVOID input, ULONG inLen,
                             PVOID output, ULONG outLen, ULONG* info) {
    if (!input || inLen < sizeof(RrModuleRequest) ||
        outLen < sizeof(RrModuleReply) || !output) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RrModuleRequest req;
    rr_zero(&req, sizeof(req));
    rr_copy(&req, input, sizeof(req));
    req.name[RTL_NUMBER_OF(req.name) - 1] = 0;

    PEPROCESS target = nullptr;
    NTSTATUS status = PsLookupProcessByProcessId(
        (HANDLE)(ULONG_PTR)req.pid, &target);
    if (!NT_SUCCESS(status)) return status;

    RrModuleReply reply;
    rr_zero(&reply, sizeof(reply));

    __try {
        PVOID peb = PsGetProcessPeb(target);
        if (!peb) {
            status = STATUS_NOT_FOUND;
        } else {
            PVOID ldr = nullptr;
            status = rr_copy_proc(target, (PUCHAR)peb + kPebLdr, &ldr, sizeof(ldr));
            if (NT_SUCCESS(status) && ldr) {
                const UNICODE_STRING wanted = rr_module_name(&req);
                ULONG64 head = (ULONG64)ldr +
                               FIELD_OFFSET(RR_PEB_LDR_DATA, InLoadOrderModuleList);

                LIST_ENTRY link;
                status = rr_copy_proc(target, (PVOID)head, &link, sizeof(link));
                ULONG64 cur = NT_SUCCESS(status) ? (ULONG64)link.Flink : 0;
                ULONG guard = 0x200;
                status = STATUS_NOT_FOUND;

                while (cur && cur != head && guard--) {
                    RR_LDR_ENTRY entry;
                    rr_zero(&entry, sizeof(entry));
                    if (!NT_SUCCESS(rr_copy_proc(target, (PVOID)cur, &entry, sizeof(entry)))) {
                        break;
                    }

                    if (entry.DllBase && entry.BaseDllName.Buffer &&
                        entry.BaseDllName.Length >= sizeof(wchar_t)) {
                        wchar_t nameBuf[64] = { 0 };
                        ULONG chars = entry.BaseDllName.Length / sizeof(wchar_t);
                        if (chars > RTL_NUMBER_OF(nameBuf) - 1) {
                            chars = RTL_NUMBER_OF(nameBuf) - 1;
                        }

                        if (NT_SUCCESS(rr_copy_proc(target, entry.BaseDllName.Buffer,
                                                    nameBuf, chars * sizeof(wchar_t)))) {
                            UNICODE_STRING found;
                            found.Buffer = nameBuf;
                            found.Length = (USHORT)(chars * sizeof(wchar_t));
                            found.MaximumLength = (USHORT)sizeof(nameBuf);

                            if (RtlEqualUnicodeString(&wanted, &found, TRUE)) {
                                reply.base = (ULONG64)entry.DllBase;
                                reply.size = entry.SizeOfImage;
                                status = STATUS_SUCCESS;
                                break;
                            }
                        }
                    }

                    cur = (ULONG64)entry.InLoadOrderLinks.Flink;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status = GetExceptionCode();
        if (NT_SUCCESS(status)) status = STATUS_UNSUCCESSFUL;
    }

    ObDereferenceObject(target);

    if (NT_SUCCESS(status)) {
        rr_copy(output, &reply, sizeof(reply));
        *info = sizeof(reply);
    }
    return status;
}

static NTSTATUS rr_create(PDEVICE_OBJECT device, PIRP irp) {
    UNREFERENCED_PARAMETER(device);
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS rr_control(PDEVICE_OBJECT device, PIRP irp) {
    UNREFERENCED_PARAMETER(device);

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PVOID input = irp->AssociatedIrp.SystemBuffer;
    PVOID output = irp->AssociatedIrp.SystemBuffer;
    const ULONG inLen = stack->Parameters.DeviceIoControl.InputBufferLength;
    const ULONG outLen = stack->Parameters.DeviceIoControl.OutputBufferLength;

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG info = 0;

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
    case RR_IOCTL_BATCH:
        status = rr_do_batch(input, inLen, output, outLen, &info);
        break;
    case RR_IOCTL_MODULE:
        status = rr_do_module(input, inLen, output, outLen, &info);
        break;
    default:
        break;
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = info;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static VOID rr_unload(PDRIVER_OBJECT drv) {
    rr_clean_traces(drv);

    if (g_Device) {
        UNICODE_STRING link;
        RtlInitUnicodeString(&link, g_SymlinkName);
        IoDeleteSymbolicLink(&link);
        IoDeleteDevice(g_Device);
        g_Device = nullptr;
    }

    if (g_Staging) {
        if (g_StagingIsPool || !g_FreeIndep) {
            ExFreePoolWithTag(g_Staging, '94sR');
        } else {
            g_FreeIndep(g_Staging, RR_STAGING);
        }
        g_Staging = nullptr;
    }

    g_AllocIndep = nullptr;
    g_FreeIndep = nullptr;
}

// ---- entry point ------------------------------------------------------------

static PVOID rr_alloc_staging(void) {
    UNICODE_STRING allocName;
    UNICODE_STRING freeName;
    RtlInitUnicodeString(&allocName, L"MmAllocateIndependentPages");
    RtlInitUnicodeString(&freeName, L"MmFreeIndependentPages");

    g_AllocIndep = (RR_ALLOC_INDEP)MmGetSystemRoutineAddress(&allocName);
    g_FreeIndep = (RR_FREE_INDEP)MmGetSystemRoutineAddress(&freeName);

    if (g_AllocIndep && g_FreeIndep) {
        g_StagingIsPool = false;
        PVOID pages = g_AllocIndep(RR_STAGING);
        if (pages) return pages;
    }

    // Fallback for kernels that do not export the independent-page API.
    g_StagingIsPool = true;
    return ExAllocatePoolWithTag(NonPagedPool, RR_STAGING, '94sR');
}

// kdmapper calls the entry with (param1, param2), never with a DRIVER_OBJECT.
// With --PassAllocationPtr param1 is the image base; an image base is not a
// driver object, so the Type/Size pair discriminates the two cases.
static BOOLEAN rr_is_driver_object(PVOID p) {
    if (!p) return FALSE;
    __try {
        PDRIVER_OBJECT o = (PDRIVER_OBJECT)p;
        return o->Type == IO_TYPE_DRIVER && o->Size >= sizeof(DRIVER_OBJECT);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

// Prepare la table de fonctions (.pdata) du module mappe, sans l'enregistrer
// pour l'instant : tant qu'elle n'est pas enregistree, les __try/__except de ce
// fichier restent inertes.
static BOOLEAN rr_register_exceptions(PVOID imageBase) {
    if (!imageBase) return FALSE;
    __try {
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)imageBase;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
        PIMAGE_NT_HEADERS nt =
            (PIMAGE_NT_HEADERS)((PUCHAR)imageBase + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;

        const ULONG rva =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress;
        const ULONG size =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size;
        if (!rva || size < sizeof(RUNTIME_FUNCTION)) return FALSE;

        g_FnTable = (PRUNTIME_FUNCTION)((PUCHAR)imageBase + rva);
        g_FnCount = size / sizeof(RUNTIME_FUNCTION);
        // Kernel equivalent is not exported: the dynamic function table path
        // (RtlInsertDynamicFunctionTable) has to be pattern-scanned, and the
        // KLDR entry route is deferred. Until then __try/__except stays inert.
        return FALSE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

static ULONG rr_capture_stamp(PVOID imageBase) {
    ULONG stamp = 0;
    __try {
        if (imageBase) {
            PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)imageBase;
            PIMAGE_NT_HEADERS nt =
                (PIMAGE_NT_HEADERS)((PUCHAR)imageBase + dos->e_lfanew);
            stamp = nt->FileHeader.TimeDateStamp;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        stamp = 0;
    }
    return stamp;
}

// Header pages of a manually mapped image can be read-only (kdmapper --indPages
// maps through MmAllocateIndependentPages); a write fault here must not kill
// DriverEntry, so the scrub is trapped and stays best effort.
static void rr_drop_headers(PVOID imageBase) {
    if (!imageBase) return;
    __try {
        rr_zero(imageBase, 0x400);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING path) {
    UNREFERENCED_PARAMETER(path);

    if (!rr_is_driver_object(drv)) {
        g_ImageBase = (PVOID)drv;
        return IoCreateDriver(nullptr, DriverEntry);
    }

#if RR_MINIMAL
    // Minimal variant: the object is in place, nothing else happens. No device,
    // no symlink, no staging, no IOCTL handlers, no header scrub.
    UNREFERENCED_PARAMETER(drv);
    return STATUS_SUCCESS;
#else
    if (!g_ImageBase) g_ImageBase = drv->DriverStart;

    rr_register_exceptions(g_ImageBase);
    g_TimeDateStamp = rr_capture_stamp(g_ImageBase);

    rr_build_names();
    UNICODE_STRING deviceName;
    RtlInitUnicodeString(&deviceName, g_DeviceName);

    PDEVICE_OBJECT device = nullptr;
    NTSTATUS status = IoCreateDevice(drv, 0, &deviceName, FILE_DEVICE_UNKNOWN,
                                     FILE_DEVICE_SECURE_OPEN, FALSE, &device);
    if (!NT_SUCCESS(status)) return status;

    device->Flags |= DO_BUFFERED_IO;
    g_Device = device;

    UNICODE_STRING link;
    RtlInitUnicodeString(&link, g_SymlinkName);
    status = IoCreateSymbolicLink(&link, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(device);
        g_Device = nullptr;
        return status;
    }

    g_Staging = rr_alloc_staging();
    if (!g_Staging) {
        IoDeleteSymbolicLink(&link);
        IoDeleteDevice(device);
        g_Device = nullptr;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    drv->MajorFunction[IRP_MJ_CREATE] = rr_create;
    drv->MajorFunction[IRP_MJ_CLOSE] = rr_create;
    drv->MajorFunction[IRP_MJ_DEVICE_CONTROL] = rr_control;
    drv->DriverUnload = rr_unload;

    // The I/O manager clears this automatically only for a DriverEntry reached
    // through the normal loader path; here it comes from IoCreateDriver, so the
    // device would stay unpublishable forever.
    device->Flags &= ~DO_DEVICE_INITIALIZING;

    // Best effort: nothing may read the PE headers after this call.
    rr_drop_headers(g_ImageBase);
    return STATUS_SUCCESS;
#endif
}

