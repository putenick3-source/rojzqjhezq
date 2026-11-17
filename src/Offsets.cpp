// Offsets.cpp - RobRunOverlay.ini load / save implementation.

#include "Offsets.h"
#include "Log.h"

#include <cerrno>
#include <cstdlib>
#include <cwchar>

namespace {

    constexpr wchar_t kSectionOffsets[] = L"Offsets";
    constexpr wchar_t kSectionEntity[]  = L"Entity";
    constexpr wchar_t kSectionBuild[]   = L"Build";

    // Anything larger than this cannot be a module-relative offset.
    constexpr unsigned long long kMaxSaneOffset = 0x7FFFFFFFULL;

    struct Entry {
        const wchar_t* section;
        const wchar_t* key;
        uintptr_t*     target;
    };

    const Entry kEntries[] = {
        { kSectionOffsets, L"LocalPlayer", &sdk::off::kLocalPlayer },
        { kSectionOffsets, L"EntityList",  &sdk::off::kEntityList  },
        { kSectionOffsets, L"EntityCount", &sdk::off::kEntityCount },
        { kSectionOffsets, L"ViewMatrix",  &sdk::off::kViewMatrix  },
        { kSectionOffsets, L"GameState",   &sdk::off::kGameState   },
        { kSectionEntity,  L"Position",    &sdk::ent::kPosition    },
        { kSectionEntity,  L"Rotation",    &sdk::ent::kRotation    },
        { kSectionEntity,  L"Health",      &sdk::ent::kHealth      },
        { kSectionEntity,  L"MaxHealth",   &sdk::ent::kMaxHealth   },
        { kSectionEntity,  L"Team",        &sdk::ent::kTeam        },
        { kSectionEntity,  L"Name",        &sdk::ent::kName        },
        { kSectionEntity,  L"IsAlive",     &sdk::ent::kIsAlive     },
        { kSectionEntity,  L"IsLocal",     &sdk::ent::kIsLocal     },
        { kSectionEntity,  L"BonePosHead", &sdk::ent::kBonePosHead },
        { kSectionEntity,  L"Visible",     &sdk::ent::kVisible     },
    };

    const char kSkeleton[] =
        "; RobRunOverlay.ini - offsets runtime (hexadecimaux)\r\n"
        "; Tous les offsets sont relatifs a la base de GameAssembly.dll.\r\n"
        "; Les valeurs 0x0 sont des emplacements a remplir depuis un dump.\r\n"
        "; Ne pas editer pendant que l'overlay tourne : il reecrit ce fichier\r\n"
        "; au demarrage pour completer les cles manquantes.\r\n"
        "\r\n";

    bool fileExists(const std::wstring& path) {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES &&
               (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool ensureFile() {
        const std::wstring path = offsets::iniPath();
        if (fileExists(path)) return true;

        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
            diag::log(L"offsets: impossible de creer %s (erreur %lu)",
                      path.c_str(), GetLastError());
            return false;
        }

        DWORD written = 0;
        WriteFile(h, kSkeleton, (DWORD)(sizeof(kSkeleton) - 1), &written, nullptr);
        CloseHandle(h);
        return true;
    }

    bool readValue(const wchar_t* section, const wchar_t* key,
                   std::wstring& out) {
        wchar_t buf[64]{};
        const DWORD n = GetPrivateProfileStringW(
            section, key, L"", buf, _countof(buf), offsets::iniPath().c_str());
        if (n == 0) return false;
        out.assign(buf, n);
        return true;
    }

    bool parseOffset(const std::wstring& text, uintptr_t& out) {
        if (text.empty()) return false;

        errno = 0;
        wchar_t* end = nullptr;
        const unsigned long long value = std::wcstoull(text.c_str(), &end, 16);
        if (end == text.c_str() || *end != L'\0') return false;
        if (errno == ERANGE || value > kMaxSaneOffset) return false;

        out = static_cast<uintptr_t>(value);
        return true;
    }

    bool writeValue(const wchar_t* section, const wchar_t* key,
                    uintptr_t value) {
        wchar_t buf[32]{};
        swprintf_s(buf, L"0x%llX", static_cast<unsigned long long>(value));
        return WritePrivateProfileStringW(section, key, buf,
                                          offsets::iniPath().c_str()) != FALSE;
    }

}

namespace offsets {

    std::wstring iniPath() { return diag::exeDir() + L"RobRunOverlay.ini"; }

    bool load() {
        if (!ensureFile()) return false;

        for (const Entry& e : kEntries) {
            const uintptr_t fallback = *e.target;
            std::wstring text;
            if (readValue(e.section, e.key, text)) {
                uintptr_t parsed = 0;
                if (parseOffset(text, parsed)) {
                    *e.target = parsed;
                    continue;
                }
                diag::log(L"offsets: valeur invalide [%s] %s=%s, defaut 0x%llX",
                          e.section, e.key, text.c_str(),
                          static_cast<unsigned long long>(fallback));
            }
            // Missing or invalid: keep the compiled default and persist it.
            *e.target = fallback;
            writeValue(e.section, e.key, fallback);
        }

        WritePrivateProfileStringW(kSectionBuild, L"DumpedVersion",
                                   sdk::kDumpedVersion, iniPath().c_str());

        diag::log(L"offsets: charges depuis %s", iniPath().c_str());
        return true;
    }

    bool save() {
        if (!ensureFile()) return false;

        bool ok = true;
        for (const Entry& e : kEntries) {
            ok = writeValue(e.section, e.key, *e.target) && ok;
        }
        ok = WritePrivateProfileStringW(kSectionBuild, L"DumpedVersion",
                                        sdk::kDumpedVersion,
                                        iniPath().c_str()) != FALSE && ok;
        return ok;
    }

}
