// Overlay.h - Transparent, click-through DX11 overlay window with ImGui.

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>

#include <string>
#include <functional>

class Overlay {
public:
    using RenderFn = std::function<void()>;

    // Attaches the overlay over the game window (looked up by title). If the
    // game window moves or resizes, syncGeometry() keeps the overlay aligned.
    bool create(const wchar_t* targetWindowTitle);
    void destroy();

    // Call every frame. Returns false when the user closed the overlay
    // (e.g. END key) or the message loop asked to exit.
    bool beginFrame();
    void endFrame();

    // Called from inside begin/end. The callback issues ImGui draw calls.
    void render(const RenderFn& fn);

    // Re-align the overlay window to the current game-window rect.
    void syncGeometry();

    // ImGui has keyboard focus when the menu is open — pass this to the
    // input layer so the game doesn't consume your typing.
    void setInteractive(bool on);

    HWND hwnd()      const { return hwnd_; }
    HWND target()    const { return target_; }
    int  width()     const { return width_; }
    int  height()    const { return height_; }

    // The DirectX device, exposed so ESP code can hand textures to ImGui if
    // needed. Normal ESP rendering goes through the ImGui draw list.
    ID3D11Device*        device()  const { return device_; }
    ID3D11DeviceContext* context() const { return ctx_; }

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);

    bool initD3D();
    void teardownD3D();
    void createRenderTarget();
    void releaseRenderTarget();

    HWND                 hwnd_    = nullptr;
    HWND                 target_  = nullptr;
    int                  width_   = 0;
    int                  height_  = 0;
    bool                 running_ = false;
    bool                 interactive_ = false;

    ID3D11Device*        device_  = nullptr;
    ID3D11DeviceContext* ctx_     = nullptr;
    IDXGISwapChain*      swap_    = nullptr;
    ID3D11RenderTargetView* rtv_  = nullptr;
};
