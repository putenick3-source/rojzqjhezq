// ESP.h - ImGui-based ESP renderer.
//
// Reads the current WorldSnap + Config and emits box/name/health/snapline/
// crosshair draws onto the foreground ImGui draw list. All geometry is screen
// space; projection happens here via Math::worldToScreen.

#pragma once
#include "World.h"
#include "Config.h"
#include "VecMath.h"

namespace esp {

    void render(const WorldSnap& world, const Config& cfg,
                float screenW, float screenH);

    void drawMenu(Config& cfg);

}
