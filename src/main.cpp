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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Process.h"
#include "World.h"
#include "Overlay.h"
#include "ESP.h"
#include "Config.h"
#include "SDK.h"

#include <chrono>
#include <thread>
#include <atomic>

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
            if (proc.attach(sdk::kProcessName)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(750));
        }
        return false;
    }

}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Hide the console if someone built as a console subsystem binary.
    HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);

    std::atomic<bool> running{ true };

    Process proc;
    if (!waitForProcess(proc, running)) return 0;

    Overlay overlay;
    if (!overlay.create(sdk::kWindowName)) {
        proc.detach();
        return 1;
    }

    World world;
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
            proc.detach();
            if (!waitForProcess(proc, running)) break;
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
            }
        });
        overlay.endFrame();
    }

    overlay.destroy();
    proc.detach();
    return 0;
}
