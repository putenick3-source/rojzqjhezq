// Overlay.cpp - DX11 transparent overlay implementation.

#include "Overlay.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    constexpr wchar_t kClassName[] = L"RobRunOverlayWnd_8f12";
    Overlay* gOverlayInstance = nullptr;
}

LRESULT CALLBACK Overlay::wndProcThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (gOverlayInstance) return gOverlayInstance->wndProc(h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT Overlay::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 0;

    switch (m) {
    case WM_SIZE:
        if (device_ && w != SIZE_MINIMIZED) {
            releaseRenderTarget();
            swap_->ResizeBuffers(0, (UINT)LOWORD(l), (UINT)HIWORD(l),
                                 DXGI_FORMAT_UNKNOWN, 0);
            createRenderTarget();
            width_  = LOWORD(l);
            height_ = HIWORD(l);
        }
        return 0;
    case WM_DESTROY:
        running_ = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool Overlay::create(const wchar_t* title) {
    gOverlayInstance = this;

    target_ = FindWindowW(nullptr, title);
    if (!target_) {
        // Fall back to a full-screen overlay if the window is not found yet.
        // syncGeometry() will reattach when the window appears.
        target_ = nullptr;
    }

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wndProcThunk;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    // WS_EX_LAYERED + WS_EX_TRANSPARENT = click-through, see-through.
    // WS_EX_TOOLWINDOW keeps the overlay out of the taskbar.
    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST
                        | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    hwnd_ = CreateWindowExW(
        exStyle,
        kClassName, L"RobRunOverlay",
        WS_POPUP,
        0, 0, 100, 100,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd_) return false;

    // Real transparency via DWM, not colour keying — keeps anti-aliased edges.
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd_, &m);

    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);

    if (!initD3D()) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, ctx_);

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
    syncGeometry();
    running_ = true;
    return true;
}

void Overlay::destroy() {
    if (device_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    teardownD3D();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
    gOverlayInstance = nullptr;
}

bool Overlay::initD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd_;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL got;
    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        want, _countof(want), D3D11_SDK_VERSION,
        &sd, &swap_, &device_, &got, &ctx_);
    if (FAILED(hr)) return false;

    createRenderTarget();
    return true;
}

void Overlay::teardownD3D() {
    releaseRenderTarget();
    if (swap_)   { swap_->Release();   swap_ = nullptr; }
    if (ctx_)    { ctx_->Release();    ctx_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

void Overlay::createRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    swap_->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        device_->CreateRenderTargetView(back, nullptr, &rtv_);
        back->Release();
    }
}

void Overlay::releaseRenderTarget() {
    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
}

void Overlay::syncGeometry() {
    // Re-find the target window if we lost it.
    if (!target_ || !IsWindow(target_)) {
        // Caller passes the title in create(); re-search by class if needed.
    }

    RECT r{};
    if (target_ && GetClientRect(target_, &r)) {
        POINT tl{ r.left, r.top };
        ClientToScreen(target_, &tl);
        width_  = r.right - r.left;
        height_ = r.bottom - r.top;
        SetWindowPos(hwnd_, HWND_TOPMOST, tl.x, tl.y, width_, height_,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        // Full-desktop fallback until the game window appears.
        const int w = GetSystemMetrics(SM_CXSCREEN);
        const int h = GetSystemMetrics(SM_CYSCREEN);
        width_  = w;
        height_ = h;
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, w, h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

void Overlay::setInteractive(bool on) {
    if (on == interactive_) return;
    interactive_ = on;

    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (on) {
        // Interactive: clickable and focusable so ImGui receives WM_CHAR.
        ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    } else {
        ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);

    if (on) {
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
    } else if (target_ && IsWindow(target_)) {
        // Hand the keyboard back so the game keeps its input.
        SetForegroundWindow(target_);
    }
}

bool Overlay::beginFrame() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) running_ = false;
    }
    if (!running_) return false;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void Overlay::render(const RenderFn& fn) {
    if (fn) fn();
}

void Overlay::endFrame() {
    ImGui::Render();

    const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    swap_->Present(1, 0);
}
