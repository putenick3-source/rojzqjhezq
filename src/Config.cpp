// Config.cpp - Single INI persistence: offsets, display settings, build state.
//
// One file (RobRunOverlay.ini), one format. Offsets are hex and module-relative
// to GameAssembly.dll; colours are "r,g,b,a" in 0..1. Missing or invalid keys
// are rewritten from the in-memory defaults on load.

#include "Config.h"
#include "SDK.h"
#include "Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace {

    constexpr wchar_t kSectionOffsets[] = L"Offsets";
    constexpr wchar_t kSectionEntity[]  = L"Entity";
    constexpr wchar_t kSectionDisplay[] = L"Display";
    constexpr wchar_t kSectionBuild[]   = L"Build";

    // Anything larger than this cannot be a module-relative offset.
    constexpr double kMaxSaneOffset = 2147483647.0;

    enum class Kind {
        Offset,
        Bool,
        Int,
        Float,
        Color,
    };

    struct Entry {
        const wchar_t* section;
        const wchar_t* key;
        void*          target;
        Kind           kind;
        double         lo = 0.0;
        double         hi = 0.0;
    };

    Config& g = gConfig();

    const Entry kEntries[] = {
        // Offsets (hex, relative to GameAssembly.dll).
        { kSectionOffsets, L"LocalPlayer", &sdk::off::kLocalPlayer, Kind::Offset },
        { kSectionOffsets, L"EntityList",  &sdk::off::kEntityList,  Kind::Offset },
        { kSectionOffsets, L"EntityCount", &sdk::off::kEntityCount, Kind::Offset },
        { kSectionOffsets, L"ViewMatrix",  &sdk::off::kViewMatrix,  Kind::Offset },
        { kSectionOffsets, L"GameState",   &sdk::off::kGameState,   Kind::Offset },
        { kSectionEntity,  L"Position",    &sdk::ent::kPosition,    Kind::Offset },
        { kSectionEntity,  L"Rotation",    &sdk::ent::kRotation,    Kind::Offset },
        { kSectionEntity,  L"Health",      &sdk::ent::kHealth,      Kind::Offset },
        { kSectionEntity,  L"MaxHealth",   &sdk::ent::kMaxHealth,   Kind::Offset },
        { kSectionEntity,  L"Team",        &sdk::ent::kTeam,        Kind::Offset },
        { kSectionEntity,  L"Name",        &sdk::ent::kName,        Kind::Offset },
        { kSectionEntity,  L"IsAlive",     &sdk::ent::kIsAlive,     Kind::Offset },
        { kSectionEntity,  L"IsLocal",     &sdk::ent::kIsLocal,     Kind::Offset },
        { kSectionEntity,  L"BonePosHead", &sdk::ent::kBonePosHead, Kind::Offset },
        { kSectionEntity,  L"Visible",     &sdk::ent::kVisible,     Kind::Offset },
        { kSectionEntity,  L"Transform",   &sdk::ent::kTransform,   Kind::Offset },
        { kSectionEntity,  L"TransformPos", &sdk::ent::kTransformPos, Kind::Offset },
        { kSectionEntity,  L"Head",        &sdk::ent::kHead,        Kind::Offset },

        // Display settings.
        { kSectionDisplay, L"Enabled",        &g.enabled,        Kind::Bool },
        { kSectionDisplay, L"ShowMenu",       &g.showMenu,       Kind::Bool },
        { kSectionDisplay, L"EspBox",         &g.espBox,         Kind::Bool },
        { kSectionDisplay, L"EspName",        &g.espName,        Kind::Bool },
        { kSectionDisplay, L"EspHealthBar",   &g.espHealthBar,   Kind::Bool },
        { kSectionDisplay, L"EspDistance",    &g.espDistance,    Kind::Bool },
        { kSectionDisplay, L"EspSnapline",    &g.espSnapline,    Kind::Bool },
        { kSectionDisplay, L"EspSkeleton",    &g.espSkeleton,    Kind::Bool },
        { kSectionDisplay, L"EspTeamCheck",   &g.espTeamCheck,   Kind::Bool },
        { kSectionDisplay, L"MaxDistance",    &g.maxDistance,    Kind::Float, 0.0, 10000.0 },
        { kSectionDisplay, L"MinDistance",    &g.minDistance,    Kind::Float, 0.0, 10000.0 },
        { kSectionDisplay, L"Crosshair",      &g.drawCrosshair,  Kind::Bool },
        { kSectionDisplay, L"FovCircle",      &g.drawFovCircle,  Kind::Bool },
        { kSectionDisplay, L"FovRadius",      &g.fovRadius,      Kind::Float, 1.0, 2000.0 },
        { kSectionDisplay, L"ColEnemy",       g.colEnemy,        Kind::Color },
        { kSectionDisplay, L"ColTeam",        g.colTeam,         Kind::Color },
        { kSectionDisplay, L"ColText",        g.colText,         Kind::Color },
        { kSectionDisplay, L"ColShadow",      g.colShadow,       Kind::Color },
        { kSectionDisplay, L"ColCrosshair",   g.colCrosshair,    Kind::Color },
        { kSectionDisplay, L"BoxThickness",   &g.boxThickness,   Kind::Int, 1.0, 10.0 },
        { kSectionDisplay, L"BoxHeight",      &g.boxHeight,      Kind::Float, 0.1, 10.0 },
        { kSectionDisplay, L"ManualCam",      &g.manualCam,      Kind::Bool },
        { kSectionDisplay, L"TextSize",       &g.textSize,       Kind::Float, 6.0, 48.0 },
        { kSectionDisplay, L"ReadIntervalMs", &g.readIntervalMs, Kind::Int, 0.0, 1000.0 },
    };
    const char kSkeleton[] =
        "; RobRunOverlay.ini - configuration unique (offsets + affichage)\r\n"
        "; Offsets: hexadecimaux, relatifs a la base de GameAssembly.dll.\r\n"
        "; Couleurs: r,g,b,a dans 0..1. Les 0x0 sont a remplir depuis un dump.\r\n"
        "; Le fichier est complete automatiquement; ne pas editer en cours d'execution.\r\n"
        "\r\n[Offsets]\r\n\r\n[Entity]\r\n\r\n[Display]\r\n\r\n[Build]\r\n";

    bool fileExists(const std::wstring& path) {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES &&
               (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool ensureFile() {
        const std::wstring path = config::iniPath();
        if (fileExists(path)) return true;

        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
            diag::log(L"config: impossible de creer %s (erreur %lu)",
                      path.c_str(), GetLastError());
            return false;
        }

        DWORD written = 0;
        WriteFile(h, kSkeleton, (DWORD)(sizeof(kSkeleton) - 1), &written, nullptr);
        CloseHandle(h);
        return true;
    }

    bool readValue(const Entry& e, std::wstring& out) {
        wchar_t buf[256]{};
        const DWORD n = GetPrivateProfileStringW(
            e.section, e.key, L"", buf, _countof(buf), config::iniPath().c_str());
        if (n == 0) return false;
        out.assign(buf, n);
        return true;
    }

    bool writeValue(const Entry& e) {
        wchar_t buf[256]{};
        switch (e.kind) {
        case Kind::Offset:
            swprintf_s(buf, L"0x%llX",
                       (unsigned long long)*static_cast<uintptr_t*>(e.target));
            break;
        case Kind::Bool:
            swprintf_s(buf, L"%d", *static_cast<bool*>(e.target) ? 1 : 0);
            break;
        case Kind::Int:
            swprintf_s(buf, L"%d", *static_cast<int*>(e.target));
            break;
        case Kind::Float:
            swprintf_s(buf, L"%.6g", (double)*static_cast<float*>(e.target));
            break;
        case Kind::Color: {
            const float* c = static_cast<float*>(e.target);
            swprintf_s(buf, L"%.6g,%.6g,%.6g,%.6g",
                       (double)c[0], (double)c[1], (double)c[2], (double)c[3]);
            break;
        }
        }
        return WritePrivateProfileStringW(e.section, e.key, buf,
                                          config::iniPath().c_str()) != FALSE;
    }
    bool parseNumber(const std::wstring& text, double& out) {
        if (text.empty()) return false;

        errno = 0;
        wchar_t* end = nullptr;
        const double value = std::wcstod(text.c_str(), &end);
        if (end == text.c_str() || *end != L'\0') return false;
        if (errno == ERANGE || !std::isfinite(value)) return false;

        out = value;
        return true;
    }

    bool parseOffset(const std::wstring& text, double& out) {
        if (text.empty()) return false;

        errno = 0;
        wchar_t* end = nullptr;
        const unsigned long long value = std::wcstoull(text.c_str(), &end, 16);
        if (end == text.c_str() || *end != L'\0') return false;
        if (errno == ERANGE || (double)value > kMaxSaneOffset) return false;

        out = (double)value;
        return true;
    }

    bool parseColor(const std::wstring& text, float out[4]) {
        const wchar_t* cursor = text.c_str();
        float values[4]{};

        for (int i = 0; i < 4; ++i) {
            errno = 0;
            wchar_t* end = nullptr;
            const double v = std::wcstod(cursor, &end);
            if (end == cursor || errno == ERANGE || !std::isfinite(v)) return false;

            values[i] = (float)std::clamp(v, 0.0, 1.0);
            cursor = end;
            if (i < 3) {
                if (*cursor != L',') return false;
                ++cursor;
            }
        }
        if (*cursor != L'\0') return false;

        std::memcpy(out, values, sizeof(values));
        return true;
    }

    // Applies a key's text to its target. Returns false when malformed.
    bool assign(const Entry& e, const std::wstring& text) {
        double number = 0.0;
        switch (e.kind) {
        case Kind::Offset:
            if (!parseOffset(text, number)) return false;
            *static_cast<uintptr_t*>(e.target) = (uintptr_t)number;
            return true;
        case Kind::Bool:
            if (text == L"1" || text == L"true" || text == L"True") {
                *static_cast<bool*>(e.target) = true;
                return true;
            }
            if (text == L"0" || text == L"false" || text == L"False") {
                *static_cast<bool*>(e.target) = false;
                return true;
            }
            return false;
        case Kind::Int:
            if (!parseNumber(text, number)) return false;
            *static_cast<int*>(e.target) =
                (int)std::clamp(number, e.lo, e.hi);
            return true;
        case Kind::Float:
            if (!parseNumber(text, number)) return false;
            *static_cast<float*>(e.target) =
                (float)std::clamp(number, e.lo, e.hi);
            return true;
        case Kind::Color:
            return parseColor(text, static_cast<float*>(e.target));
        }
        return false;
    }

}

namespace config {

    std::wstring iniPath() {
        std::wstring dir = diag::exeDir();
        if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
        return dir + L"RobRunOverlay.ini";
    }

    bool load() {
        if (!ensureFile()) return false;

        for (const Entry& e : kEntries) {
            std::wstring text;
            if (readValue(e, text) && assign(e, text)) {
                continue;
            }
            if (!text.empty()) {
                diag::log(L"config: valeur invalide [%s] %s=%s, defaut restaure",
                          e.section, e.key, text.c_str());
            }
            writeValue(e);
        }

        WritePrivateProfileStringW(kSectionBuild, L"DumpedVersion",
                                   sdk::kDumpedVersion, iniPath().c_str());

        diag::log(L"config: charge depuis %s", iniPath().c_str());
        return true;
    }

    bool save() {
        if (!ensureFile()) return false;

        bool ok = true;
        for (const Entry& e : kEntries) {
            ok = writeValue(e) && ok;
        }
        ok = WritePrivateProfileStringW(kSectionBuild, L"DumpedVersion",
                                        sdk::kDumpedVersion,
                                        iniPath().c_str()) != FALSE && ok;
        return ok;
    }

}
