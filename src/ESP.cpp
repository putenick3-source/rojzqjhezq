// ESP.cpp - Overlay draw logic.

#include "ESP.h"
#include "Dumper.h"
#include "Scan.h"
#include "SDK.h"
#include "Log.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
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

    constexpr size_t kScanBudget = 512u * 1024u * 1024u;

    struct OffsetChoice {
        const char* label;
        uintptr_t*  slot;
    };

    const OffsetChoice kOffsetChoices[] = {
        { "Position",    &sdk::ent::kPosition },
        { "Transform",   &sdk::ent::kTransform },
        { "TransformPos",&sdk::ent::kTransformPos },
        { "Head",        &sdk::ent::kHead },
        { "Health",      &sdk::ent::kHealth },
        { "MaxHealth",   &sdk::ent::kMaxHealth },
        { "Team",        &sdk::ent::kTeam },
        { "IsAlive",     &sdk::ent::kIsAlive },
        { "Visible",     &sdk::ent::kVisible },
        { "Name",        &sdk::ent::kName },
        { "Rotation",    &sdk::ent::kRotation },
        { "BonePosHead", &sdk::ent::kBonePosHead },
    };

    const char* offsetChoicesGetter(void* data, int idx) {
        return static_cast<const OffsetChoice*>(data)[idx].label;
    }

    // Dumper tab: value scan + pointer-array / view-projection heuristics, then
    // assignment of a hit to an INI-backed offset (addr - baseEntity).
    void dumperTab(const WorldSnap& world, const Process& proc) {
        DumperState& d = gDumper();
        d.baseEntity = world.localAddress;

        if (!proc.isAttached()) {
            ImGui::TextDisabled("process non attache");
            return;
        }

        ImGui::Text("Valeur");
        ImGui::SetNextItemWidth(150.0f);
        ImGui::InputText("##value", d.valueBuf, sizeof(d.valueBuf));

        static const char* kKinds[] = { "Float", "Int32" };
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("Type", &d.kind, kKinds, IM_ARRAYSIZE(kKinds));

        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputFloat("Tolerance", &d.tol, 0.001f, 0.01f, "%.4f");
        if (d.tol < 0.0f) d.tol = 0.0f;

        const scan::Kind kind = (d.kind == 1) ? scan::Kind::Int32 : scan::Kind::Float;
        const double value = std::strtod(d.valueBuf, nullptr);

        if (ImGui::Button("Premier scan")) {
            const bool hit = d.scanner.first(proc, kind, value, d.tol, kScanBudget);
            std::snprintf(d.status, sizeof(d.status), "%zu resultat(s)%s",
                          d.scanner.count(), hit ? "" : " - aucun");
        }
        ImGui::SameLine();
        if (ImGui::Button("Suivant")) {
            d.scanner.next(proc);
            std::snprintf(d.status, sizeof(d.status), "%zu resultat(s)%s",
                          d.scanner.count(),
                          d.scanner.finished() ? " - fin" : "");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            d.scanner.reset();
            d.arrays.clear();
            d.foundVp = Projection{};
            std::snprintf(d.status, sizeof(d.status), "pret");
        }

        ImGui::TextWrapped("%s", d.status);

        static int selected = -1;
        if (selected >= (int)d.scanner.count()) selected = -1;

        ImGui::BeginChild("##hits", { 0.0f, 160.0f }, true);
        ImGuiListClipper clipper;
        clipper.Begin((int)d.scanner.count());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const scan::Hit& h = d.scanner.hits()[(size_t)i];
                char label[96];
                if (d.kind == 1) {
                    std::snprintf(label, sizeof(label), "[%d] 0x%016llX  %d", i,
                                  (unsigned long long)h.addr, (int)h.i);
                } else {
                    std::snprintf(label, sizeof(label), "[%d] 0x%016llX  %.4f", i,
                                  (unsigned long long)h.addr, h.f);
                }
                if (ImGui::Selectable(label, selected == i)) selected = i;
            }
        }
        clipper.End();
        ImGui::EndChild();

        ImGui::Separator();
        if (!d.baseEntity) {
            ImGui::TextDisabled("baseEntity inconnue (LocalPlayer non lu)");
        }
        ImGui::Text("baseEntity: 0x%016llX", (unsigned long long)d.baseEntity);

        static int target = 0;
        ImGui::SetNextItemWidth(150.0f);
        ImGui::Combo("Offset", &target, offsetChoicesGetter,
                     (void*)kOffsetChoices, IM_ARRAYSIZE(kOffsetChoices));

        const bool canAssign = d.baseEntity != 0 && selected >= 0 &&
                               (size_t)selected < d.scanner.count();
        ImGui::BeginDisabled(!canAssign);
        if (ImGui::Button("Assigner l'adresse selectionnee")) {
            const uintptr_t addr = d.scanner.hits()[(size_t)selected].addr;
            if (addr >= d.baseEntity) {
                *kOffsetChoices[target].slot = addr - d.baseEntity;
                std::snprintf(d.status, sizeof(d.status), "%s = 0x%llX",
                              kOffsetChoices[target].label,
                              (unsigned long long)(addr - d.baseEntity));
                diag::log(L"dumper: %S = 0x%llX (entite 0x%llX, adresse 0x%llX)",
                          kOffsetChoices[target].label,
                          (unsigned long long)(addr - d.baseEntity),
                          (unsigned long long)d.baseEntity,
                          (unsigned long long)addr);
            } else {
                std::snprintf(d.status, sizeof(d.status),
                              "adresse sous la base entite");
            }
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::Button("Tableaux de pointeurs")) {
            d.arrays = scan::findPointerArrays(proc, 8, kScanBudget);
            std::snprintf(d.status, sizeof(d.status), "%zu tableau(x) candidat(s)",
                          d.arrays.size());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(d.arrays.empty());
        if (ImGui::Button("Tableaux -> EntityList") && !d.arrays.empty() &&
            d.arrays.front().addr > proc.base()) {
            sdk::off::kEntityList = d.arrays.front().addr - proc.base();
            std::snprintf(d.status, sizeof(d.status), "EntityList assignee");
        }
        ImGui::EndDisabled();

        if (ImGui::Button("Chercher ViewProjection")) {
            size_t checked = 0;
            const bool ok = scan::findViewProjection(proc, d.foundVp, kScanBudget, checked);
            d.vpCandidates = (int)checked;
            std::snprintf(d.status, sizeof(d.status),
                          "%s - %d candidat(s) teste(s)",
                          ok ? "matrice trouvee" : "aucune matrice", d.vpCandidates);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!d.foundVp.valid());
        if (ImGui::Button("ViewProjection -> ViewMatrix") && d.foundVp.addr > proc.base()) {
            sdk::off::kViewMatrix = d.foundVp.addr - proc.base();
            std::snprintf(d.status, sizeof(d.status), "ViewMatrix assignee");
            diag::log(L"dumper: ViewMatrix = 0x%llX",
                      (unsigned long long)sdk::off::kViewMatrix);
        }
        ImGui::EndDisabled();
    }

}

namespace esp {

    void render(const WorldSnap& world, const Config& cfg, Process& /*proc*/,
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
            if (!world.cam.boxToScreen(e.pos, e.head, screenW, screenH, tl, br)) {
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

    void drawMenu(Config& cfg, const WorldSnap& world, const Process& proc) {
        ImGui::SetNextWindowSize({ 360, 520 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.88f);

        if (!ImGui::Begin("Rob Run Overlay", &cfg.showMenu,
                          ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Enabled", &cfg.enabled);
        ImGui::Separator();

        if (ImGui::BeginTabBar("##onglets")) {
            if (ImGui::BeginTabItem("Reglages")) {
                if (ImGui::CollapsingHeader("ESP", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Box",        &cfg.espBox);
                    ImGui::Checkbox("Name",       &cfg.espName);
                    ImGui::Checkbox("Health bar", &cfg.espHealthBar);
                    ImGui::Checkbox("Distance",   &cfg.espDistance);
                    ImGui::Checkbox("Snapline",   &cfg.espSnapline);
                    ImGui::Checkbox("Skeleton (requires bone offsets)", &cfg.espSkeleton);
                    ImGui::Checkbox("Hide teammates", &cfg.espTeamCheck);
                    ImGui::SliderInt("Box thickness", &cfg.boxThickness, 1, 4);
                    ImGui::SliderFloat("Box height (m)", &cfg.boxHeight, 0.5f, 3.0f);
                    ImGui::Checkbox("Cam manuelle", &cfg.manualCam);
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
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Dumper")) {
                dumperTab(world, proc);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::TextDisabled("INSERT = toggle menu");
        ImGui::TextDisabled("END    = quit");
        ImGui::End();
    }

}
