# Rob Run — External Overlay

External, read-only overlay. Finds `RobRun.exe`, opens a transparent click-through DX11 window over it, draws ImGui ESP (boxes, names, HP bars, distance, snaplines, crosshair, FOV circle).

Pure `ReadProcessMemory` — no injection, no hooks, no writes. All cheat logic lives outside the game process.

## Build

```bat
git clone --depth 1 https://github.com/ocornut/imgui vendor/imgui
cmake -B build -S . -A x64
cmake --build build --config Release
```

Output: `build/Release/RobRunOverlay.exe`.

Run **as administrator** if the game is launched with elevation (common on anti-cheat protected titles).

## Runtime

| Key    | Action                        |
|--------|-------------------------------|
| INSERT | Toggle config menu            |
| END    | Quit overlay                  |

The overlay attaches automatically and re-attaches if the game is closed and reopened.

## Filling in the offsets

Everything in the framework is build-agnostic **except** `src/SDK.h`. Each `// FILL:` line is one value to dump against the current `RobRun.exe` build. You need:

**Module-relative (static) offsets:**
- `kLocalPlayer`  — pointer-to-pointer to the local player struct
- `kEntityList`   — pointer to the array of entity pointers
- `kEntityCount`  — int: number of active entities (optional; falls back to `kMaxEntities`)
- `kViewMatrix`   — 4x4 float, row-major view-projection matrix
- `kGameState`    — int or bool, non-zero in a match (optional)

**Per-entity offsets** (relative to each `Entity*`):
- `kPosition`   — `Vec3` world position (float x/y/z)
- `kHealth`, `kMaxHealth` — floats
- `kTeam`       — int team id
- `kIsAlive`    — bool / int
- `kName`       — inline `char[32]` or `char*`
- `kBonePosHead` — optional, only if you want head boxes / snaplines to head
- `kVisible`    — optional visibility flag

### How to find them

1. **Cheat Engine** — attach to `RobRun.exe`, scan for your known HP value, narrow by taking damage, find a pointer path. Repeat for each field. "Find what accesses this address" gives you the instruction, then "Browse memory region" / Mono / dissect data structure maps out the entity layout.
2. **ReClass.NET** — paste the entity pointer, walk the struct interactively. Great for discovering `Position`, `Health`, `Team` fields visually.
3. **IDA / Ghidra / x64dbg** — for patch-stable signatures. Find the instruction that reads `g_LocalPlayer`, extract the surrounding bytes (10–12 bytes with wildcards on relative operands), feed it to `mem::findPattern` + `mem::resolveRip` at runtime instead of hard-coding the offset. That version survives small patches.

The scanner in `src/Memory.h` accepts IDA-style signatures (`"48 8B 05 ? ? ? ?"`). Swap any raw `kFoo = 0xABC` for a scan at startup once you have a signature.

## Layout

```
robrun-overlay/
  CMakeLists.txt
  README.md
  src/
    main.cpp         entry point, hotkeys, lifecycle
    Overlay.h/.cpp   DX11 transparent window + ImGui
    ESP.h/.cpp       ESP draws + settings menu
    World.h          one-tick world snapshot (RPM)
    Process.h        attach, module base, templated RPM
    Memory.h         IDA-style pattern scan + RIP resolver
    SDK.h            game offsets (the one file to edit per patch)
    Math.h           Vec2/Vec3/Matrix4 + world-to-screen
    Config.h         runtime settings
```

## Notes

- **External only.** The overlay never writes to the game, never allocates inside it, never injects. Reads are rate-limitable via `Config::readIntervalMs` to keep RPM pressure low.
- **View matrix layout.** `Math::worldToScreen` assumes row-major. If positions project to the wrong quadrant, your view matrix is column-major — transpose on read or flip the indexing in `worldToScreen`.
- **Entity stride.** Default is `sizeof(void*)` — change in `SDK.h` if the game uses an array of inline structs instead of pointers.
- **Click-through.** Window is `WS_EX_TRANSPARENT` by default; `setInteractive(true)` flips it off while the menu is open so ImGui can receive input.
