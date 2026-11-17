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
#include "Version.h"
#include "Projection.h"
#include "Scan.h"
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
            if (proc.attach(sdk::kProcessName, sdk::kAnchorModule)) {
                diag::log(L"attach: reussie");
                return true;
            }
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

    // Resolves the view-projection: the INI offset first (either layout), then a
    // heuristic scan. A scan hit is written back as a module-relative offset.
    void detectViewProjection(const Process& proc, Projection& vp) {
        vp = Projection{};
        if (!proc.isAttached() || !proc.base()) return;

        if (sdk::off::kViewMatrix) {
            const uintptr_t at = proc.base() + sdk::off::kViewMatrix;
            for (Layout layout : { Layout::RowMajor, Layout::ColumnMajor }) {
                Projection candidate;
                if (readProjection(proc, at, layout, candidate) && candidate.valid()) {
                    vp = candidate;
                    return;
                }
            }
        }

        Projection found;
        size_t checked = 0;

        const std::vector<Process::Region> regions = proc.regions(512u * 1024u * 1024u);
        size_t bytes = 0;
        for (const auto& r : regions) bytes += r.size;
        diag::log(L"projection: %zu regions, %zu octets enumeres", regions.size(), bytes);

        if (scan::findViewProjection(proc, regions, found, checked) &&
            found.valid() && found.addr != 0) {
            vp = found;
            sdk::off::kViewMatrix = found.addr - proc.base();
            diag::log(L"projection: ViewMatrix detectee = 0x%llX (%zu candidats)",
                      (unsigned long long)sdk::off::kViewMatrix, checked);
            return;
        }

        // Scan infructueux: on journalise le nombre de candidats testes.
        diag::log(L"projection: echec (%zu candidats testes)", checked);
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
              sdk::kDumpedVersion, config::iniPath().c_str());
    if (!config::load()) {
        diag::log(L"config: echec du chargement, ESP inactif");
    }

    Process proc;
    if (!waitForProcess(proc, running)) return 0;
    diag::log(L"boucle: entree apres attache");

    World world;
    buildguard::Verdict verdict = buildguard::Verdict::Unknown;
    applyBuildGuard(proc, world, verdict);

    // Cached view-projection, refreshed on a 2 s cadence when lost or stale.
    Projection vp;
    detectViewProjection(proc, vp);

    Overlay overlay;
    if (!overlay.create(sdk::kWindowName)) {
        proc.detach();
        return 1;
    }

    auto lastRead = std::chrono::steady_clock::now();
    auto lastSync = std::chrono::steady_clock::now();
    auto lastVp   = std::chrono::steady_clock::now();

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
            detectViewProjection(proc, vp);
            lastVp = std::chrono::steady_clock::now();
        }

        // --- Window geometry sync (lightweight; every ~200 ms) ---
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSync).count() >= 200) {
            overlay.syncGeometry();
            lastSync = now;
        }

        // --- View-projection re-detection (2 s, only when unusable) ---
        if (!vp.valid() || !vp.addr || !proc.validate(vp.addr, 64)) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastVp).count() >= 2000) {
                detectViewProjection(proc, vp);
                lastVp = now;
            }
        }

        // --- World refresh ---
        const int intervalMs = cfg.readIntervalMs;
        const bool shouldRead =
            intervalMs <= 0 ||
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRead).count() >= intervalMs;
        if (shouldRead) {
            world.refresh(proc, vp);
            lastRead = now;
        }

        // --- Render ---
        static bool trameLogged = false;
        if (!trameLogged) {
            diag::log(L"boucle: trame");
            trameLogged = true;
        }
        overlay.render([&]() {
            const auto snap = world.get();
            esp::render(snap, cfg, proc,
                        (float)overlay.width(), (float)overlay.height());

            if (cfg.showMenu) {
                esp::drawMenu(cfg, snap, proc);
                drawBuildBadge(verdict);
            }
        });
        overlay.endFrame();
    }

    config::save();
    overlay.destroy();
    proc.detach();
    return 0;
}
