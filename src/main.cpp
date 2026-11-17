// main.cpp - Rob Run external overlay entry point.
//
// Lifecycle:
//   1. Wait for RobRun.exe to appear, attach to it (Process).
//   2. Create a transparent click-through DX11 overlay over its window.
//   3. Each frame: optionally refresh the world snapshot (RPM), then draw
//      ESP + menu with ImGui.
//   4. INSERT toggles the config menu (makes the overlay interactive);
//      END quits the overlay.
//
// Build: see CMakeLists.txt. Produces RobRunOverlay.exe — run as administrator
// if the game is launched with elevation.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "Process.h"
#include "World.h"
#include "Overlay.h"
#include "ESP.h"
#include "Config.h"
#include "SDK.h"
#include "Offsets.h"
#include "Version.h"
#include "Log.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <chrono>
#include <thread>
#include <atomic>
#include <cstdio>

namespace {

    bool keyPressedEdge(int vk) {
        static bool prev[256] = {};
        const bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = now && !prev[vk & 0xFF];
        prev[vk & 0xFF] = now;
        return edge;
    }

    bool waitForProcess(Process& proc, std::atomic<bool>& run) {
        while (run) {
            if (proc.attach(sdk::kProcessName, sdk::kAnchorModule)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
        }
        return false;
    }

    // Re-evaluates the build guard and gates the world snapshot accordingly.
    void applyBuildGuard(const Process& proc, World& world,
                         buildguard::Verdict& verdict) {
        const buildguard::Status status = buildguard::check(proc);
        verdict = status.verdict;
        world.setBuildVerdict(status.verdict);
        buildguard::record(status);
    }

    // Small status pill inside the config menu (ESP.cpp is left untouched).
    void drawBuildBadge(buildguard::Verdict verdict) {
        ImGuiWindow* menu = ImGui::FindWindowByName("Rob Run Overlay");
        if (!menu || menu->Collapsed) return;

        const char* text = "build non identifiee";
        char matchText[64]{};
        std::snprintf(matchText, sizeof(matchText), "build %ls OK",
                      sdk::kDumpedVersion);
        ImU32 color = IM_COL32(255, 170, 40, 255); // Unknown: orange
        if (verdict == buildguard::Verdict::Match) {
            text = matchText;
            color = IM_COL32(90, 220, 120, 255);
        } else if (verdict == buildguard::Verdict::Mismatch) {
            text = "build divergente";
            color = IM_COL32(255, 90, 90, 255);
        }

        const ImVec2 size = ImGui::CalcTextSize(text);
        float x = menu->Pos.x + menu->Size.x - size.x - 20.0f;
        const float minX = menu->Pos.x + 10.0f;
        if (x < minX) x = minX;
        const float y = menu->Pos.y + 10.0f;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled({ x - 6.0f, y - 3.0f },
                          { x + size.x + 6.0f, y + size.y + 3.0f },
                          IM_COL32(15, 15, 15, 215), 4.0f);
        dl->AddText({ x, y }, color, text);
    }

}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Hide the console if someone built as a console subsystem binary.
    HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);

    std::atomic<bool> running{ true };

    diag::log(L"demarrage - build de reference %s, offsets: %s",
              sdk::kDumpedVersion, offsets::iniPath().c_str());
    if (!offsets::load()) {
        diag::log(L"offsets: echec du chargement, ESP inactif");
    }

    Process proc;
    if (!waitForProcess(proc, running)) return 0;

    World world;
    buildguard::Verdict verdict = buildguard::Verdict::Unknown;
    applyBuildGuard(proc, world, verdict);

    Overlay overlay;
    if (!overlay.create(sdk::kWindowName)) {
        proc.detach();
        return 1;
    }

    auto lastRead = std::chrono::steady_clock::now();
    auto lastSync = std::chrono::steady_clock::now();

    Config& cfg = gConfig();

    while (overlay.beginFrame()) {
        // --- Hotkeys ---
        if (keyPressedEdge(VK_INSERT)) {
            cfg.showMenu = !cfg.showMenu;
            overlay.setInteractive(cfg.showMenu);
        }
        if (keyPressedEdge(VK_END)) {
            running = false;
            break;
        }

        // --- Process liveness ---
        if (!proc.stillAlive()) {
            verdict = buildguard::Verdict::Unknown;
            world.setBuildVerdict(verdict);
            proc.detach();
            if (!waitForProcess(proc, running)) break;
            applyBuildGuard(proc, world, verdict);
        }

        // --- Window geometry sync (lightweight; every ~200 ms) ---
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSync).count() >= 200) {
            overlay.syncGeometry();
            lastSync = now;
        }

        // --- World refresh ---
        const int intervalMs = cfg.readIntervalMs;
        const bool shouldRead =
            intervalMs <= 0 ||
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRead).count() >= intervalMs;
        if (shouldRead) {
            world.refresh(proc);
            lastRead = now;
        }

        // --- Render ---
        overlay.render([&]() {
            const auto snap = world.get();
            esp::render(snap, cfg,
                        (float)overlay.width(), (float)overlay.height());

            if (cfg.showMenu) {
                esp::drawMenu(cfg);
                drawBuildBadge(verdict);
            }
        });
        overlay.endFrame();
    }

    overlay.destroy();
    proc.detach();
    return 0;
}
