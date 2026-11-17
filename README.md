# Rob Run — External Overlay

External, read-only overlay. Finds `RobRun.exe`, opens a transparent click-through DX11 window over it, draws ImGui ESP (boxes, names, HP bars, distance, snaplines, crosshair, FOV circle).

Pure `ReadProcessMemory` — no injection, no hooks, no writes to the game. All cheat logic lives outside the game process. Offsets are runtime values loaded from `RobRunOverlay.ini` and anchored on the `GameAssembly.dll` base.

An optional manual-mapped kernel driver (`driver/`, see `docs/driver.md`) replaces the reads; when it is not loaded the overlay falls back to RPM unchanged.

A build guard cuts the ESP when the live `GameAssembly.dll` version differs from `sdk::kDumpedVersion`.

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

## Offsets and build guard

Everything is anchored on the `GameAssembly.dll` module base. Offsets and display settings share one persisted file, `RobRunOverlay.ini`, owned by `Config.h/.cpp`; missing or invalid keys are rewritten with the defaults (`0x0` for offsets). A re-dump only requires editing the INI — no rebuild.

Reference prefix: `0.7.0` (`sdk::kDumpedVersion` in `src/SDK.h`). At every attach the overlay reads the process EXE's `FileVersion` (fallback: `GameAssembly.dll`) and compares only `major.minor.build`, ignoring the revision:

- matching prefix → ESP enabled, green badge;
- different prefix → world snapshot invalidated, ESP cut, red badge;
- unreadable version → verdict `Unknown`, ESP allowed, orange badge;
- the observed version and verdict are written to `[Build]` and `RobRunOverlay.log`.

To support a new build, re-dump the INI, bump `kDumpedVersion`, and rebuild.

## Filling in the offsets

Every offset key in `[Offsets]` / `[Entity]` is hexadecimal and module-relative to `GameAssembly.dll`:

**`[Offsets]` (static):**
- `LocalPlayer` — pointer-to-pointer to the local player struct
- `EntityList` — pointer to the array of entity pointers
- `EntityCount` — int: number of active entities (optional; falls back to `kMaxEntities`)
- `ViewMatrix` — 4x4 float, row-major view-projection matrix
- `GameState` — int or bool, non-zero in a match (optional)

**`[Entity]` (relative to each `Entity*`):**
- `Position` — `Vec3` world position (float x/y/z)
- `Health`, `MaxHealth` — floats
- `Team` — int team id
- `IsAlive` — bool / int
- `Name` — inline `char[32]` or `char*`
- `BonePosHead` — optional, only if you want head boxes / snaplines to head
- `Head` — optional `Vec3` head position, preferred over `BonePosHead`
- `Transform` / `TransformPos` — optional indirection: `Transform*` at `Transform`, `Vec3` at `TransformPos` inside it; preferred over `Position`
- `Visible` — optional visibility flag
- `Rotation` — unused by the current renderer, kept for future work

### Dumper tab

The menu has a **Dumper** tab (RPM only) to help fill those values: scan a float (with tolerance) or int, page through the hits, run the aligned-pointer and view-projection heuristics, then assign a hit to an offset. Assignment stores `hit - baseEntity` (base = local player address) and `ViewMatrix`/`EntityList` assignments store `address - GameAssembly.dll base`. Values live in memory until the INI is written on exit.

### How to find them

1. **Cheat Engine** — attach to `RobRun.exe`, scan for your known HP value, narrow by taking damage, find a pointer path. Repeat for each field. "Find what accesses this address" gives you the instruction, then "Browse memory region" / Mono / dissect data structure maps out the entity layout.
2. **ReClass.NET** — paste the entity pointer, walk the struct interactively. Great for discovering `Position`, `Health`, `Team` fields visually.
3. **IDA / Ghidra / x64dbg** — for patch-stable signatures. Find the instruction that reads `g_LocalPlayer`, extract the surrounding bytes (10–12 bytes with wildcards on relative operands), feed it to `mem::findPattern` + `mem::resolveRip` at runtime instead of hard-coding the offset. That version survives small patches.

The scanner in `src/Memory.h` accepts IDA-style signatures (`"48 8B 05 ? ? ? ?"`). Once a signature is stable it can feed a startup resolver; the resulting address minus the `GameAssembly.dll` base is the INI value to persist.

## Layout

```
robrun-overlay/
  CMakeLists.txt
  README.md
  src/
    main.cpp         entry point, hotkeys, lifecycle, build guard
    Overlay.h/.cpp   DX11 transparent window + ImGui
    ESP.h/.cpp       ESP draws + settings menu
    World.h          one-tick world snapshot (RPM)
    Process.h        attach, anchor module base, templated RPM
    Memory.h         IDA-style pattern scan + RIP resolver
    SDK.h            anchor module, reference build, offset defaults
    Version.h/.cpp   build guard (EXE version, three verdicts)
    Log.h            file + debugger diagnostics
    VecMath.h        Vec2/Vec3 + sane();
    Projection.h     Mat4 view-projection read + validity checks
    Camera.h         view-projection or manual projection source
    Scan.h           resumable value scan, pointer-array / VP heuristics
    Dumper.h         state backing the Dumper tab
    Config.h/.cpp    single INI persistence (settings + offsets)
  driver/
    Shared.h         IOCTL contract shared with the client
    Source.cpp       RrSvc.sys (ntoskrnl only, no CRT, manual mapped)
    build.bat        WDK kernel build
  user/
    DriverClient.h/.cpp  device client, chunked/batched reads
  docs/driver.md     mapping, test order, EAC audit notes
```

## Notes

- **External only.** The overlay never writes to the game, never allocates inside it, never injects. The only file it writes is `RobRunOverlay.ini` (and the diagnostic `RobRunOverlay.log`). Reads are rate-limitable via `Config::readIntervalMs` to keep RPM pressure low.
- **View matrix layout.** `Projection` stores the matrix row-major with the camera basis in rows 0..2; column-major sources are transposed on read and both layouts are tried by the scanner. `Camera` also offers a manual mode (`ManualCam`) that builds its own basis instead.
- **Entity stride.** Default is `sizeof(void*)` — change in `SDK.h` if the game uses an array of inline structs instead of pointers.
- **Version guard.** The EXE resource is authoritative; `GameAssembly.dll` is only a fallback. An unreadable version is `Unknown` (ESP allowed), never a false cut.
- **Click-through.** Window is `WS_EX_TRANSPARENT` by default; `setInteractive(true)` flips it off while the menu is open so ImGui can receive input.
