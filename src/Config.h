// Config.h - Runtime-toggleable cheat settings. Rendered as the ImGui menu.

#pragma once
#include <cstdint>

struct Config {
    // Master
    bool enabled          = true;
    bool showMenu         = true;    // toggled with INSERT

    // ESP toggles
    bool espBox           = true;
    bool espName          = true;
    bool espHealthBar     = true;
    bool espDistance      = true;
    bool espSnapline      = false;
    bool espSkeleton      = false;   // requires bone offsets
    bool espTeamCheck     = true;    // hide teammates

    // Filters
    float maxDistance     = 300.0f;  // world units
    float minDistance     = 0.0f;

    // Crosshair / FOV
    bool  drawCrosshair   = false;
    bool  drawFovCircle   = false;
    float fovRadius       = 150.0f;  // pixels

    // Colors (RGBA, 0..1)
    float colEnemy[4]     = { 1.00f, 0.20f, 0.20f, 1.00f };
    float colTeam[4]      = { 0.20f, 0.60f, 1.00f, 1.00f };
    float colText[4]      = { 1.00f, 1.00f, 1.00f, 1.00f };
    float colShadow[4]    = { 0.00f, 0.00f, 0.00f, 0.80f };
    float colCrosshair[4] = { 0.00f, 1.00f, 0.60f, 1.00f };

    // Rendering
    int   boxThickness    = 1;
    float textSize        = 13.0f;

    // Update rate for memory reads (ms between full world refreshes).
    // 0 = every frame. Raise this to reduce RPM pressure.
    int   readIntervalMs  = 0;
};

inline Config& gConfig() {
    static Config c;
    return c;
}
