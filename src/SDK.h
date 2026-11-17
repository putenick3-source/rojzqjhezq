// SDK.h - Rob Run game-specific offsets, structures, and constants.
//
// Offsets are anchored on the GameAssembly.dll module base (IL2CPP). They are
// no longer compile-time constants: Overlay startup loads them from
// RobRunOverlay.ini (next to the executable) and persists them back, so a new
// dump only requires editing the INI, not rebuilding.
//
// Replace each default `0x0` with the offset found in your dump. All offsets
// are hexadecimal and relative to the GameAssembly.dll base.

#pragma once
#include <cstdint>

namespace sdk {

    // ---- Process targeting ---------------------------------------------------
    inline constexpr const wchar_t* kProcessName = L"ROB RUN.exe";
    inline constexpr const wchar_t* kWindowName  = L"ROB RUN";          // visible window title; adjust if different
    inline constexpr const wchar_t* kAnchorModule = L"GameAssembly.dll"; // module whose base offsets are relative to

    // Build the offsets below were dumped against. Only the "major.minor.build"
    // prefix is compared, so revisions do not trip the guard. On mismatch the
    // ESP is cut until the INI has been re-dumped.
    inline constexpr const wchar_t* kDumpedVersion = L"0.7.0";

    // ---- Module-relative offsets --------------------------------------------
    // Resolved as: GameAssembly.dll base + offset -> (optional pointer chain)
    // Loaded from RobRunOverlay.ini, section [Offsets].
    namespace off {
        // Pointer to the local player structure.
        // Typical pattern: static ptr -> LocalPlayer*
        inline uintptr_t kLocalPlayer   = 0x0;   // INI: LocalPlayer

        // Pointer to the entity list (array of Entity*).
        inline uintptr_t kEntityList    = 0x0;   // INI: EntityList

        // Maximum entity count (number of slots in the list).
        inline uintptr_t kEntityCount   = 0x0;   // INI: EntityCount

        // View matrix used for world-to-screen (row-major 4x4 float).
        inline uintptr_t kViewMatrix    = 0x0;   // INI: ViewMatrix

        // Game state / in-match flag — used to hide ESP on menu screens.
        inline uintptr_t kGameState     = 0x0;   // INI: GameState
    }

    // ---- Per-entity offsets (relative to an Entity* struct) -----------------
    // Loaded from RobRunOverlay.ini, section [Entity].
    namespace ent {
        inline uintptr_t kPosition      = 0x0;   // INI: Position      Vec3 world position
        inline uintptr_t kRotation      = 0x0;   // INI: Rotation      Vec3 or quaternion
        inline uintptr_t kHealth        = 0x0;   // INI: Health        int / float HP
        inline uintptr_t kMaxHealth     = 0x0;   // INI: MaxHealth
        inline uintptr_t kTeam          = 0x0;   // INI: Team          int team id
        inline uintptr_t kName          = 0x0;   // INI: Name          inline char[32] or pointer to string
        inline uintptr_t kIsAlive       = 0x0;   // INI: IsAlive       bool / int
        inline uintptr_t kIsLocal       = 0x0;   // INI: IsLocal       bool flag, OR compare address to LocalPlayer
        inline uintptr_t kBonePosHead   = 0x0;   // INI: BonePosHead   optional, for head-aim / head box
        inline uintptr_t kVisible       = 0x0;   // INI: Visible       optional visibility flag

        // Optional transform indirection: `Transform*` at this offset, world
        // position at kTransformPos inside it. Preferred over kPosition when set.
        inline uintptr_t kTransform     = 0x0;   // INI: Transform     Entity* + this -> Transform*
        inline uintptr_t kTransformPos  = 0x0;   // INI: TransformPos  Transform* + this -> Vec3

        // Optional head position (bone or head collider), relative to the Entity*.
        inline uintptr_t kHead          = 0x0;   // INI: Head          Vec3 head position
    }

    // Entity list stride — size of each pointer slot. Usually sizeof(void*) = 8.
    inline constexpr size_t kEntityStride = sizeof(void*);

    // Expected / sane maximum, fallback if kEntityCount is 0.
    inline constexpr int kMaxEntities = 128;

    // Team id of the local player's team — used to tell friends from enemies.
    // Read dynamically from the local player; this is just a fallback.
    inline constexpr int kFallbackLocalTeam = -1;
}


