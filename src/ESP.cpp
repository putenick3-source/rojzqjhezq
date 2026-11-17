// ESP.cpp - Overlay draw logic.

#include "ESP.h"

#include "imgui.h"

#include <cstdio>
#include <algorithm>

namespace {

    ImU32 packColor(const float c[4]) {
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(c[0], c[1], c[2], c[3]));
    }

    void outlineText(ImDrawList* dl, const ImVec2& at,
                     ImU32 fg, ImU32 shadow,
                     const char* text) {
        dl->AddText({ at.x - 1, at.y     }, shadow, text);
        dl->AddText({ at.x + 1, at.y     }, shadow, text);
        dl->AddText({ at.x,     at.y - 1 }, shadow, text);
        dl->AddText({ at.x,     at.y + 1 }, shadow, text);
        dl->AddText(at, fg, text);
    }

    void drawBox(ImDrawList* dl, const Vec2& tl, const Vec2& br,
                 ImU32 color, int thickness) {
        dl->AddRect({ tl.x, tl.y }, { br.x, br.y }, color,
                    0.0f, 0, (float)thickness);
        // Slight shadow for readability on light backgrounds
        const ImU32 shadow = IM_COL32(0, 0, 0, 160);
        dl->AddRect({ tl.x - 1, tl.y - 1 }, { br.x + 1, br.y + 1 },
                    shadow, 0.0f, 0, 1.0f);
    }

    void drawHealthBar(ImDrawList* dl, const Vec2& tl, const Vec2& br,
                       float ratio) {
        ratio = std::clamp(ratio, 0.0f, 1.0f);

        const float barW = 3.0f;
        const float x1   = tl.x - barW - 2.0f;
        const float y1   = tl.y;
        const float y2   = br.y;
        const float h    = (y2 - y1) * ratio;

        dl->AddRectFilled({ x1 - 1, y1 - 1 }, { x1 + barW + 1, y2 + 1 },
                          IM_COL32(0, 0, 0, 200));

        // HP colour: red -> yellow -> green across 0..1
        const int r = (int)((1.0f - ratio) * 255);
        const int g = (int)(ratio * 255);
        const ImU32 col = IM_COL32(r, g, 0, 255);

        dl->AddRectFilled({ x1, y2 - h }, { x1 + barW, y2 }, col);
    }

}

namespace esp {

    void render(const WorldSnap& world, const Config& cfg,
                float screenW, float screenH) {
        if (!cfg.enabled) return;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        // Crosshair
        if (cfg.drawCrosshair) {
            const ImU32 c = packColor(cfg.colCrosshair);
            const float cx = screenW * 0.5f;
            const float cy = screenH * 0.5f;
            dl->AddLine({ cx - 6, cy }, { cx + 6, cy }, c, 1.0f);
            dl->AddLine({ cx, cy - 6 }, { cx, cy + 6 }, c, 1.0f);
        }
        if (cfg.drawFovCircle) {
            const ImU32 c = packColor(cfg.colCrosshair);
            dl->AddCircle({ screenW * 0.5f, screenH * 0.5f },
                          cfg.fovRadius, c, 64, 1.0f);
        }

        if (!world.valid || !world.inMatch) return;

        const ImU32 colEnemy  = packColor(cfg.colEnemy);
        const ImU32 colTeam   = packColor(cfg.colTeam);
        const ImU32 colText   = packColor(cfg.colText);
        const ImU32 colShadow = packColor(cfg.colShadow);

        for (const auto& e : world.entities) {
            if (!e.alive) continue;
            if (e.isLocal) continue;

            const bool friendly = (e.team == world.localTeam);
            if (cfg.espTeamCheck && friendly) continue;

            const float dist = world.localPos.distanceTo(e.pos);
            if (dist > cfg.maxDistance || dist < cfg.minDistance) continue;

            Vec2 tl, br;
            if (!worldBoxToScreen(e.pos, 1.8f, 0.6f, world.viewMatrix,
                                  screenW, screenH, tl, br)) {
                continue;
            }

            // Cull offscreen
            if (br.x < 0 || tl.x > screenW || br.y < 0 || tl.y > screenH) continue;

            const ImU32 col = friendly ? colTeam : colEnemy;

            if (cfg.espBox) {
                drawBox(dl, tl, br, col, cfg.boxThickness);
            }

            if (cfg.espHealthBar && e.maxHp > 0.0f) {
                drawHealthBar(dl, tl, br, e.hp / e.maxHp);
            }

            if (cfg.espSnapline) {
                const float cx = (tl.x + br.x) * 0.5f;
                dl->AddLine({ screenW * 0.5f, screenH },
                            { cx, br.y }, col, 1.0f);
            }

            if (cfg.espName && !e.name.empty()) {
                outlineText(dl, { tl.x, tl.y - 14 }, colText, colShadow,
                            e.name.c_str());
            }

            if (cfg.espDistance) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "[%dm]", (int)dist);
                outlineText(dl, { tl.x, br.y + 2 }, colText, colShadow, buf);
            }
        }
    }

    void drawMenu(Config& cfg) {
        ImGui::SetNextWindowSize({ 320, 480 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.88f);

        if (!ImGui::Begin("Rob Run Overlay", &cfg.showMenu,
                          ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Enabled", &cfg.enabled);
        ImGui::Separator();

        if (ImGui::CollapsingHeader("ESP", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Box",        &cfg.espBox);
            ImGui::Checkbox("Name",       &cfg.espName);
            ImGui::Checkbox("Health bar", &cfg.espHealthBar);
            ImGui::Checkbox("Distance",   &cfg.espDistance);
            ImGui::Checkbox("Snapline",   &cfg.espSnapline);
            ImGui::Checkbox("Skeleton (requires bone offsets)", &cfg.espSkeleton);
            ImGui::Checkbox("Hide teammates", &cfg.espTeamCheck);
            ImGui::SliderInt("Box thickness", &cfg.boxThickness, 1, 4);
        }

        if (ImGui::CollapsingHeader("Filters")) {
            ImGui::SliderFloat("Max distance", &cfg.maxDistance, 0.0f, 2000.0f);
            ImGui::SliderFloat("Min distance", &cfg.minDistance, 0.0f, 500.0f);
        }

        if (ImGui::CollapsingHeader("Aim aids")) {
            ImGui::Checkbox("Crosshair",  &cfg.drawCrosshair);
            ImGui::Checkbox("FOV circle", &cfg.drawFovCircle);
            ImGui::SliderFloat("FOV radius", &cfg.fovRadius, 20.0f, 500.0f);
        }

        if (ImGui::CollapsingHeader("Colours")) {
            ImGui::ColorEdit4("Enemy",  cfg.colEnemy);
            ImGui::ColorEdit4("Team",   cfg.colTeam);
            ImGui::ColorEdit4("Text",   cfg.colText);
            ImGui::ColorEdit4("Shadow", cfg.colShadow);
            ImGui::ColorEdit4("Cross",  cfg.colCrosshair);
        }

        if (ImGui::CollapsingHeader("Perf")) {
            ImGui::SliderInt("Read interval (ms)", &cfg.readIntervalMs, 0, 50);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        }

        ImGui::Separator();
        ImGui::TextDisabled("INSERT = toggle menu");
        ImGui::TextDisabled("END    = quit");
        ImGui::End();
    }

}
