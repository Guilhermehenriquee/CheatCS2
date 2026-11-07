// muei 無影 - render surface and D3D11 presentation.
//
// One surface: our own layered, topmost, colour-keyed window.
//
// The obvious-looking alternative — creating the swap chain against the game's
// HWND — does not work for an external overlay, and this build proved it on a
// live game: the chain creates, every Present returns S_OK, and the screen shows
// nothing, because the game presents its own frame over the same window surface
// immediately afterwards. Inheriting the HWND is only viable when the overlay is
// injected and hooks the game's Present.
//
// So we own the window. It covers exactly the game's client area (or the work
// area when no game window exists), sits topmost, and is colour-keyed so only
// what we draw survives. The bitblt swap model is mandatory: the flip model
// presents through DWM, which ignores LWA_COLORKEY and would paint the key
// colour as an opaque rectangle.
//
// Window styles: click-through (WS_EX_TRANSPARENT) while the menu is closed so
// the game stays playable; interactive while it is open.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include "../imgui/imgui.h"

namespace overlay {

class Instance {
public:
    Instance() = default;
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    // Create the surface over targetPid's window (0 = whole work area) and bring
    // up D3D11 + ImGui backends.
    bool init(DWORD targetPid);

    void shutdown();

    // Drain the message queue. Returns false once the app should exit.
    bool pump();

    // ImGui frame brackets. background() is valid between begin() and end().
    void begin();
    void end();

    ImDrawList* background() const;

    int  width() const { return width_; }
    int  height() const { return height_; }

    bool menuOpen() const { return menuOpen_; }
    void setMenuOpen(bool open) { menuOpen_ = open; }

    // Re-fit the surface to a game window that moved or resized (alt-tab, mode
    // change, restart).
    void refitToGame(DWORD targetPid);

private:
    static LRESULT CALLBACK wndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);

    bool createOverlayWindow(DWORD targetPid);
    bool initD3D();
    bool createSwapChain(DXGI_SWAP_EFFECT effect, bool freshDevice);
    void shutdownD3D();
    void updateStyle();
    void handleResize();

    static HWND  findWindowForPid(DWORD pid);
    static RECT  gameClientRect(DWORD pid, bool& found);

    HWND  hwnd_       = nullptr;
    bool  classReg_   = false;
    bool  running_    = true;
    bool  menuOpen_   = true;

    int   width_  = 0;
    int   height_ = 0;
    int   posX_   = 0;
    int   posY_   = 0;

    ID3D11Device*           dev_  = nullptr;
    ID3D11DeviceContext*    ctx_  = nullptr;
    IDXGISwapChain*         swap_ = nullptr;
    ID3D11RenderTargetView* rtv_  = nullptr;

    bool imguiWin32_ = false;
    bool imguiDx11_  = false;

    unsigned presentCount_ = 0;
};

} // namespace overlay
