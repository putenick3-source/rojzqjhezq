// SDK.h - Rob Run game-specific offsets, structures, and constants.
//
// These values are tied to a specific build of RobRun.exe. When the game
// updates they will usually need to be re-dumped. Dump them with Cheat Engine,
// IDA, ReClass.NET, or x64dbg against the live process.
//
// Replace each `// FILL:` value with the offset / size found in your dump.
// Everything else in the project is build-agnostic and will not need to change.

#pragma once
#include <cstdint>

namespace sdk {

    // ---- Process targeting ---------------------------------------------------
    inline constexpr const wchar_t* kProcessName = L"RobRun.exe";
    inline constexpr const wchar_t* kWindowName  = L"Rob Run";          // visible window title; adjust if different
    inline constexpr const wchar_t* kMainModule  = L"RobRun.exe";       // module whose base we anchor offsets to

    // ---- Module-relative offsets --------------------------------------------
    // Resolved as: moduleBase + offset -> (optional pointer chain) -> value
    namespace off {
        // Pointer to the local player structure.
        // Typical pattern: static ptr -> LocalPlayer*
        inline constexpr uintptr_t kLocalPlayer   = 0x0;   // FILL: pattern "48 8B 05 ? ? ? ? 48 85 C0 74 ? 48 8B 80"

        // Pointer to the entity list (array of Entity*).
        inline constexpr uintptr_t kEntityList    = 0x0;   // FILL: pattern "48 8B 0D ? ? ? ? E8 ? ? ? ? 48 8B C8 E8"

        // Maximum entity count (number of slots in the list).
        inline constexpr uintptr_t kEntityCount   = 0x0;   // FILL: often a sibling of EntityList

        // View matrix used for world-to-screen (row-major 4x4 float).
        inline constexpr uintptr_t kViewMatrix    = 0x0;   // FILL: pattern "48 8D 0D ? ? ? ? 48 C1 E0 06"

        // Game state / in-match flag — used to hide ESP on menu screens.
        inline constexpr uintptr_t kGameState     = 0x0;   // FILL
    }

    // ---- Per-entity offsets (relative to an Entity* struct) -----------------
    namespace ent {
        inline constexpr uintptr_t kPosition      = 0x0;   // FILL: Vec3 world position
        inline constexpr uintptr_t kRotation      = 0x0;   // FILL: Vec3 or quaternion
        inline constexpr uintptr_t kHealth        = 0x0;   // FILL: int / float HP
        inline constexpr uintptr_t kMaxHealth     = 0x0;   // FILL
        inline constexpr uintptr_t kTeam          = 0x0;   // FILL: int team id
        inline constexpr uintptr_t kName          = 0x0;   // FILL: inline char[32] or pointer to string
        inline constexpr uintptr_t kIsAlive       = 0x0;   // FILL: bool / int
        inline constexpr uintptr_t kIsLocal       = 0x0;   // FILL: bool flag, OR compare address to LocalPlayer
        inline constexpr uintptr_t kBonePosHead   = 0x0;   // FILL: optional, for head-aim / head box
        inline constexpr uintptr_t kVisible       = 0x0;   // FILL: optional visibility flag
    }

    // Entity list stride — size of each pointer slot. Usually sizeof(void*) = 8.
    inline constexpr size_t kEntityStride = sizeof(void*);

    // Expected / sane maximum, fallback if kEntityCount is 0.
    inline constexpr int kMaxEntities = 128;

    // Team id of the local player's team — used to tell friends from enemies.
    // Read dynamically from the local player; this is just a fallback.
    inline constexpr int kFallbackLocalTeam = -1;
}
