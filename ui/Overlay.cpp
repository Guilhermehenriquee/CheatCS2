#include "Overlay.h"
#include "Theme.h"

#include "../core/Log.h"

#include "../imgui/backends/imgui_impl_dx11.h"
#include "../imgui/backends/imgui_impl_win32.h"

#include <d3d11.h>
#include <dxgi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace overlay {
namespace {

Instance* g_instance = nullptr;

// Pure magenta: absent from the theme and from anything ImGui draws, so only
// the cleared background is keyed out of the layered window.
constexpr COLORREF kColorKey = RGB(0xFF, 0x00, 0xFF);

constexpr wchar_t kClassName[] = L"muei_overlay";

} // namespace

Instance::~Instance() { shutdown(); }

HWND Instance::findWindowForPid(DWORD pid) {
    if (!pid) return nullptr;

    struct Ctx { DWORD pid; HWND best; };
    Ctx ctx{ pid, nullptr };

    EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
        Ctx* c = (Ctx*)lp;
        DWORD wpid = 0;
        GetWindowThreadProcessId(hw, &wpid);
        if (wpid != c->pid) return TRUE;
        if (!IsWindowVisible(hw)) return TRUE;
        if (GetWindow(hw, GW_OWNER) != nullptr) return TRUE;

        char cls[128]{};
        GetClassNameA(hw, cls, sizeof(cls));
        if (!_stricmp(cls, "ConsoleWindowClass")) return TRUE;
        if (!_stricmp(cls, "IME")) return TRUE;

        RECT r{};
        GetClientRect(hw, &r);
        if ((r.right - r.left) < 320 || (r.bottom - r.top) < 240) return TRUE;

        c->best = hw;
        return FALSE;
    }, (LPARAM)&ctx);

    return ctx.best;
}

RECT Instance::gameClientRect(DWORD pid, bool& found) {
    found = false;
    RECT out{ 0, 0, 0, 0 };

    HWND game = findWindowForPid(pid);
    if (!game) return out;

    RECT client{};
    if (!GetClientRect(game, &client)) return out;

    POINT tl{ client.left, client.top };
    if (!ClientToScreen(game, &tl)) return out;

    out.left   = tl.x;
    out.top    = tl.y;
    out.right  = tl.x + (client.right - client.left);
    out.bottom = tl.y + (client.bottom - client.top);

    found = (out.right - out.left) >= 320 && (out.bottom - out.top) >= 240;
    return out;
}

LRESULT CALLBACK Instance::wndProcStatic(HWND hw, UINT m, WPARAM w, LPARAM l) {
    if (g_instance) return g_instance->wndProc(hw, m, w, l);
    return DefWindowProcW(hw, m, w, l);
}

LRESULT Instance::wndProc(HWND hw, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(hw, m, w, l)) return 1;

    switch (m) {
        case WM_SIZE:
            if (w != SIZE_MINIMIZED) {
                width_  = LOWORD(l);
                height_ = HIWORD(l);
            }
            return 0;
        case WM_DESTROY:
            running_ = false;
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hw, m, w, l);
}

bool Instance::createOverlayWindow(DWORD targetPid) {
    if (!classReg_) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &Instance::wndProcStatic;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.style         = CS_VREDRAW | CS_HREDRAW;
        // No background brush: a brush paints the window opaque before D3D gets
        // a frame, which is one half of the classic black overlay.
        wc.hbrBackground = nullptr;
        if (!RegisterClassExW(&wc)) {
            logx::win32("RegisterClassExW failed", GetLastError());
            return false;
        }
        classReg_ = true;
    }

    // Cover the game's client area exactly when there is one, so the overlay and
    // the frame underneath share a coordinate space. Otherwise fall back to the
    // work area rather than the whole screen: a colour-keyed window over the
    // taskbar would swallow shell clicks.
    bool haveGame = false;
    RECT game = gameClientRect(targetPid, haveGame);

    if (haveGame) {
        posX_  = game.left;
        posY_  = game.top;
        width_ = game.right - game.left;
        height_= game.bottom - game.top;
        logx::line("overlay: tracking game window %dx%d at (%d,%d)",
                   width_, height_, posX_, posY_);
    } else {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        posX_  = work.left;
        posY_  = work.top;
        width_ = work.right - work.left;
        height_= work.bottom - work.top;
        if (width_ < 320 || height_ < 240) {
            posX_ = 0;
            posY_ = 0;
            width_  = GetSystemMetrics(SM_CXSCREEN);
            height_ = GetSystemMetrics(SM_CYSCREEN);
        }
        logx::line("overlay: no game window, covering work area %dx%d at (%d,%d)",
                   width_, height_, posX_, posY_);
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kClassName, L"muei", WS_POPUP,
        posX_, posY_, width_, height_,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!hwnd_) {
        logx::win32("CreateWindowExW failed", GetLastError());
        return false;
    }

    if (!SetLayeredWindowAttributes(hwnd_, kColorKey, 0, LWA_COLORKEY)) {
        logx::win32("SetLayeredWindowAttributes(COLORKEY) failed", GetLastError());
    }

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    logx::line("overlay: window created hwnd=%p", (void*)hwnd_);
    return true;
}

bool Instance::createSwapChain(DXGI_SWAP_EFFECT effect, bool freshDevice) {
    if (freshDevice) shutdownD3D();

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 2;
    sd.BufferDesc.Width   = (UINT)width_;
    sd.BufferDesc.Height  = (UINT)height_;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = hwnd_;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = effect;

    D3D_FEATURE_LEVEL want[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    const UINT wantCount = (UINT)(sizeof(want) / sizeof(want[0]));
    D3D_FEATURE_LEVEL got{};

    HRESULT hr = E_FAIL;
    if (!dev_) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            want, wantCount, D3D11_SDK_VERSION, &sd,
            &swap_, &dev_, &got, &ctx_);
        if (FAILED(hr)) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                want, wantCount, D3D11_SDK_VERSION, &sd,
                &swap_, &dev_, &got, &ctx_);
        }
        if (FAILED(hr)) return false;
    } else {
        IDXGIDevice* dxgiDev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;

        if (FAILED(dev_->QueryInterface(IID_IDXGIDevice, (void**)&dxgiDev)) || !dxgiDev) {
            return false;
        }
        if (FAILED(dxgiDev->GetAdapter(&adapter))) { dxgiDev->Release(); return false; }
        if (FAILED(adapter->GetParent(IID_IDXGIFactory, (void**)&factory))) {
            adapter->Release(); dxgiDev->Release(); return false;
        }

        hr = factory->CreateSwapChain(dev_, &sd, &swap_);
        factory->Release(); adapter->Release(); dxgiDev->Release();
        if (FAILED(hr)) return false;
    }

    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }

    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(swap_->GetBuffer(0, IID_ID3D11Texture2D, (void**)&back)) && back) {
        dev_->CreateRenderTargetView(back, nullptr, &rtv_);
        back->Release();
    }
    return rtv_ != nullptr;
}

bool Instance::initD3D() {
    struct Attempt { DXGI_SWAP_EFFECT effect; bool fresh; const char* name; };

    // A layered window must use bitblt. The flip model presents through DWM,
    // which ignores LWA_COLORKEY and paints the key colour opaquely.
    static const Attempt attempts[] = {
        { DXGI_SWAP_EFFECT_DISCARD, false, "DISCARD" },
        { DXGI_SWAP_EFFECT_DISCARD, true,  "DISCARD+fresh-device" },
    };

    bool ok = false;
    for (const Attempt& a : attempts) {
        if (createSwapChain(a.effect, a.fresh)) {
            logx::line("overlay: swapchain ok effect=%s %dx%d layered=yes",
                       a.name, width_, height_);
            ok = true;
            break;
        }
        logx::line("overlay: swapchain failed effect=%s", a.name);
    }
    if (!ok) return false;

    // Stop DXGI installing alt-enter handling on our window.
    IDXGIDevice* dxgiDev = nullptr;
    if (SUCCEEDED(dev_->QueryInterface(IID_IDXGIDevice, (void**)&dxgiDev)) && dxgiDev) {
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;
        if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && adapter) {
            if (SUCCEEDED(adapter->GetParent(IID_IDXGIFactory, (void**)&factory)) && factory) {
                factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
                factory->Release();
            }
            adapter->Release();
        }
        dxgiDev->Release();
    }
    return true;
}

void Instance::shutdownD3D() {
    if (rtv_)  { rtv_->Release();  rtv_  = nullptr; }
    if (swap_) { swap_->Release(); swap_ = nullptr; }
    if (ctx_)  { ctx_->Release();  ctx_  = nullptr; }
    if (dev_)  { dev_->Release();  dev_  = nullptr; }
}

bool Instance::init(DWORD targetPid) {
    g_instance = this;

    if (!createOverlayWindow(targetPid)) {
        logx::line("overlay: no usable surface");
        return false;
    }

    if (!initD3D()) {
        logx::line("overlay: D3D11 init failed");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const int faces = theme::loadFonts();
    logx::line("overlay: imgui context up, %d font face(s)", faces);

    if (!ImGui_ImplWin32_Init(hwnd_)) {
        logx::line("overlay: ImGui_ImplWin32_Init failed");
        return false;
    }
    imguiWin32_ = true;

    if (!ImGui_ImplDX11_Init(dev_, ctx_)) {
        logx::line("overlay: ImGui_ImplDX11_Init failed");
        return false;
    }
    imguiDx11_ = true;

    return true;
}

void Instance::shutdown() {
    if (imguiDx11_)  { ImGui_ImplDX11_Shutdown();  imguiDx11_  = false; }
    if (imguiWin32_) { ImGui_ImplWin32_Shutdown(); imguiWin32_ = false; }
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();

    shutdownD3D();

    if (hwnd_) DestroyWindow(hwnd_);
    if (classReg_) {
        UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
        classReg_ = false;
    }
    hwnd_ = nullptr;
    g_instance = nullptr;
}

bool Instance::pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) running_ = false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return running_;
}

void Instance::updateStyle() {
    if (!hwnd_) return;

    // Click-through while the menu is closed: the overlay covers the whole game
    // window, so without this every click would land on us instead of the game.
    const LONG ex   = GetWindowLongW(hwnd_, GWL_EXSTYLE);
    const LONG want = menuOpen_ ? (ex & ~WS_EX_TRANSPARENT) : (ex | WS_EX_TRANSPARENT);
    if (want != ex) SetWindowLongW(hwnd_, GWL_EXSTYLE, want);

    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Instance::handleResize() {
    if (!hwnd_) return;
    if (!IsWindow(hwnd_)) { running_ = false; return; }

    RECT r{};
    GetClientRect(hwnd_, &r);
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    if (w < 32 || h < 32) return;
    if (w == width_ && h == height_) return;

    width_  = w;
    height_ = h;

    if (rtv_) { rtv_->Release(); rtv_ = nullptr; }

    if (swap_ && SUCCEEDED(swap_->ResizeBuffers(2, (UINT)w, (UINT)h,
                                                DXGI_FORMAT_R8G8B8A8_UNORM, 0))) {
        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(swap_->GetBuffer(0, IID_ID3D11Texture2D, (void**)&back)) && back) {
            dev_->CreateRenderTargetView(back, nullptr, &rtv_);
            back->Release();
        }
    } else {
        shutdownD3D();
        if (imguiDx11_) ImGui_ImplDX11_InvalidateDeviceObjects();
        initD3D();
    }
}

void Instance::refitToGame(DWORD targetPid) {
    if (!hwnd_ || !targetPid) return;

    bool found = false;
    RECT game = gameClientRect(targetPid, found);
    if (!found) return;

    const int x = game.left;
    const int y = game.top;
    const int w = game.right - game.left;
    const int h = game.bottom - game.top;

    if (x == posX_ && y == posY_ && w == width_ && h == height_) return;

    logx::line("overlay: refit to game %dx%d at (%d,%d)", w, h, x, y);

    posX_  = x;
    posY_  = y;
    width_ = w;
    height_= h;

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    handleResize();
}

void Instance::begin() {
    handleResize();
    updateStyle();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Instance::end() {
    ImGui::Render();

    if (!rtv_ || !swap_ || !ctx_) return;

    // The chain is opaque, so paint the exact key colour and let Windows key it
    // out, leaving everything ImGui drew intact. Anything drawn with alpha will
    // blend against magenta, which is why the shell is fully opaque.
    const float clear[4] = { 1.0f, 0.0f, 1.0f, 1.0f };

    ctx_->OMSetRenderTargets(1, &rtv_, nullptr);
    ctx_->ClearRenderTargetView(rtv_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    const HRESULT hr = swap_->Present(0, 0);
    ++presentCount_;
    if (presentCount_ <= 3 || FAILED(hr)) {
        logx::line("overlay: present #%u hr=0x%08lX %dx%d",
                   presentCount_, (unsigned long)hr, width_, height_);
    }

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        shutdownD3D();
        if (imguiDx11_) ImGui_ImplDX11_InvalidateDeviceObjects();
        initD3D();
    }
}

ImDrawList* Instance::background() const {
    return ImGui::GetBackgroundDrawList();
}

} // namespace overlay
