// Offsets.h - Runtime offset persistence (RobRunOverlay.ini).
//
// All offsets live in an INI next to the executable, so a re-dump never needs
// a rebuild. Values are hex, module-relative to GameAssembly.dll:
//
//   [Offsets]  LocalPlayer / EntityList / EntityCount / ViewMatrix / GameState
//   [Entity]   Position / Rotation / Health / MaxHealth / Team / Name /
//              IsAlive / IsLocal / BonePosHead / Visible
//
// Missing or invalid keys fall back to the compiled defaults and are written
// back so the file stays complete and editable.

#pragma once
#include "SDK.h"

#include <string>

namespace offsets {

    // Absolute path of RobRunOverlay.ini (next to the executable).
    std::wstring iniPath();

    // Creates the INI with a documented skeleton if absent, then loads values
    // into sdk::off / sdk::ent and persists any missing key. Returns false only
    // when the file cannot be created/read.
    bool load();

    // Persists the current in-memory values.
    bool save();

}
