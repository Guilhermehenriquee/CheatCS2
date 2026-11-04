#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>
#include <cstdarg>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "proc.h"
#include "offsets.h"
#include "math.h"
#include "nav.h"
#include "features.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "psapi.lib")

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
static Proc       g_proc;
static Nav        g_nav(g_proc);
static Config     g_cfg;
static Aimbot     g_aim;
static Triggerbot g_trigger;
static Bhop       g_bhop;
static Glow       g_glow;
static Misc       g_misc;

static HWND     g_overlay  = nullptr;
static bool     g_running  = true;
static bool     g_menuOpen = true;
static bool     g_attached = false;
static bool     g_ownWindow = false;   // true when no game window was available

// Colour key for the own-window fallback. Pure magenta, unused by the theme and
// by anything drawn on top, so only the cleared background dissolves. The clear
// colour below MUST match this - see the present loop.
constexpr COLORREF kColorKey = RGB(0xFF, 0x00, 0xFF);

static void LogLine(const char* fmt, ...);

static int g_screenW = 0;
static int g_screenH = 0;

static Frame g_frame;

namespace UI {
    constexpr float kWindowW = 440.f;
    constexpr float kWindowH = 640.f;
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;
}

// -----------------------------------------------------------------------------
// Window
// -----------------------------------------------------------------------------
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK WndProc(HWND hw, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(hw, m, w, l)) return true;

    switch (m) {
        case WM_KEYDOWN:
            if (w == VK_INSERT) { g_menuOpen = !g_menuOpen; return 0; }
            if (w == VK_END)    { g_running = false; PostQuitMessage(0); return 0; }
            break;
        case WM_SIZE:
            if (w != SIZE_MINIMIZED) {
                g_screenW = LOWORD(l);
                g_screenH = HIWORD(l);
            }
            return 0;
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hw, m, w, l);
}

// Find the top-level window the game renders into.
//
// Window discovery is deliberately independent of the memory attach. Finding the
// HWND only needs EnumWindows + GetWindowThreadProcessId, which work without any
// process handle. If attach failed (game elevated and this process is not), the
// overlay can still inherit the right window and draw - it just has no data to
// draw from until attach succeeds.
static HWND FindTargetWindow() {
    if (!g_proc.pid) return nullptr;

    struct Ctx { DWORD pid; HWND best; };
    Ctx ctx{ g_proc.pid, nullptr };

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

// Build the standalone topmost overlay window. Used when there is no game window
// to inherit, and again as the fallback when the inherited window refuses every
// swap-chain effect.
static bool CreateFallbackWindow() {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"cs2ext_overlay";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.style         = CS_VREDRAW | CS_HREDRAW;
        // No background brush: a brush paints the window opaque before D3D ever
        // gets a frame, which is one half of the original black screen.
        wc.hbrBackground = nullptr;
        if (!RegisterClassExW(&wc)) return false;
        classRegistered = true;
    }

    // Work area, not the full screen: a colour-keyed window covering the taskbar
    // would swallow clicks that belong to the shell.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    g_screenW = work.right - work.left;
    g_screenH = work.bottom - work.top;
    if (g_screenW < 320 || g_screenH < 240) {
        g_screenW = GetSystemMetrics(SM_CXSCREEN);
        g_screenH = GetSystemMetrics(SM_CYSCREEN);
        work.left = 0;
        work.top  = 0;
    }

    HWND hw = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        L"cs2ext_overlay", L"cs2ext", WS_POPUP,
        work.left, work.top, g_screenW, g_screenH,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hw) {
        LogLine("CreateWindowExW fallback FAILED gle=%lu", GetLastError());
        return false;
    }

    // Colour key, not alpha. LWA_ALPHA needs per-pixel alpha the DXGI swap chain
    // cannot supply; a colour key works with the plain bitblt presentation path.
    if (!SetLayeredWindowAttributes(hw, kColorKey, 0, LWA_COLORKEY)) {
        LogLine("SetLayeredWindowAttributes(COLORKEY) FAILED gle=%lu", GetLastError());
    }

    g_overlay   = hw;
    g_ownWindow = true;
    ShowWindow(hw, SW_SHOWNOACTIVATE);
    LogLine("fallback window created hwnd=%p %dx%d at (%d,%d)",
            (void*)hw, g_screenW, g_screenH, work.left, work.top);
    return true;
}
// Resolve the render surface.
//
// Two paths, and the difference is not cosmetic:
//
//   Inherited  - no window of our own. The swap chain is created against the
//                game's HWND, so the overlay draws on top of the frame the game
//                already presents. No z-order war, no layered compositing, and
//                nothing to flash. This is the path that is used whenever the
//                game is running.
//
//   Own window - only when no game window exists (attached but still loading,
//                or attached to a hidden window). A plain WS_POPUP without
//                WS_EX_LAYERED, because DXGI cannot present into a layered
//                window: the swap chain reports success and every frame comes
//                out black. That was the black screen.
static bool ResolveSurface() {
    HWND game = FindTargetWindow();
    if (game) {
        g_overlay   = game;
        g_ownWindow = false;

        RECT r{};
        GetClientRect(game, &r);
        g_screenW = r.right - r.left;
        g_screenH = r.bottom - r.top;
        if (g_screenW < 320 || g_screenH < 240) {
            g_screenW = GetSystemMetrics(SM_CXSCREEN);
            g_screenH = GetSystemMetrics(SM_CYSCREEN);
        }
        return true;
    }

    return CreateFallbackWindow();
}

// -----------------------------------------------------------------------------
// D3D11
// -----------------------------------------------------------------------------
static ID3D11Device*           g_dev  = nullptr;
static ID3D11DeviceContext*    g_ctx  = nullptr;
static IDXGISwapChain*         g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv  = nullptr;

static void ShutdownD3D();

// Swap chain creation. A window the game already owns will refuse a flip-model
// chain, so the order is: flip -> bitblt -> bitblt with a fresh device. A
// discarded swap effect against a foreign HWND can come back HRESULT-ok and
// still present nothing, which looks exactly like a black overlay.
static bool CreateSwapChain(HWND hw, DXGI_SWAP_EFFECT effect, bool needNewDevice) {
    if (needNewDevice) ShutdownD3D();

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount        = 2;
    sd.BufferDesc.Width   = g_screenW;
    sd.BufferDesc.Height  = g_screenH;
    sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 0;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow       = hw;
    sd.SampleDesc.Count   = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed           = TRUE;
    sd.SwapEffect         = effect;

    D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                 D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got{};

    HRESULT hr = E_FAIL;
    if (!g_dev) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            want, _countof(want), D3D11_SDK_VERSION, &sd,
            &g_swap, &g_dev, &got, &g_ctx);
        if (FAILED(hr)) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                want, _countof(want), D3D11_SDK_VERSION, &sd,
                &g_swap, &g_dev, &got, &g_ctx);
        }
        if (FAILED(hr)) return false;
    } else {
        // Reuse the existing device, rebuild only the chain.
        IDXGIDevice* dxgiDev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;
        if (FAILED(g_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev))) return false;
        if (FAILED(dxgiDev->GetAdapter(&adapter))) { dxgiDev->Release(); return false; }
        if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory))) {
            adapter->Release(); dxgiDev->Release(); return false;
        }
        hr = factory->CreateSwapChain(g_dev, &sd, &g_swap);
        factory->Release(); adapter->Release(); dxgiDev->Release();
        if (FAILED(hr)) return false;
    }

    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && back) {
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
    return g_rtv != nullptr;
}

static bool InitD3D(HWND hw) {
    // The effect that actually succeeded is logged - on an inherited game window
    // the flip model is usually refused, and knowing which one took is the
    // difference between debugging and guessing.
    //
    // A colour-keyed (layered) window MUST use the bitblt model. The flip model
    // presents through DWM composition, which ignores LWA_COLORKEY entirely: the
    // key colour is drawn as a plain opaque rectangle. That was the second black
    // screen - this time magenta.
    struct Attempt { DXGI_SWAP_EFFECT effect; bool fresh; const char* name; };
    static const Attempt flipAttempts[] = {
        { DXGI_SWAP_EFFECT_FLIP_DISCARD, false, "FLIP_DISCARD" },
    };
    static const Attempt bltAttempts[] = {
        { DXGI_SWAP_EFFECT_DISCARD,      false, "DISCARD" },
        { DXGI_SWAP_EFFECT_DISCARD,      true,  "DISCARD+fresh-device" },
    };

    LONG ex = GetWindowLongW(hw, GWL_EXSTYLE);
    bool layered = (ex & WS_EX_LAYERED) != 0;

    bool ok = false;
    auto tryAll = [&](const Attempt* list, size_t count) -> bool {
        for (size_t i = 0; i < count; ++i) {
            const Attempt& a = list[i];
            if (CreateSwapChain(hw, a.effect, a.fresh)) {
                LogLine("swapchain ok  effect=%s  hwnd=%p  %dx%d  layered=%s",
                        a.name, (void*)hw, g_screenW, g_screenH, layered ? "yes" : "no");
                return true;
            }
            LogLine("swapchain FAILED  effect=%s  hwnd=%p", a.name, (void*)hw);
        }
        return false;
    };

    if (!layered) {
        ok = tryAll(flipAttempts, _countof(flipAttempts));
    }
    if (!ok) {
        ok = tryAll(bltAttempts, _countof(bltAttempts));
    }
    if (!ok) return false;

    // Stop DXGI from installing its own alt-enter handling on a window we may
    // not own.
    IDXGIDevice* dxgiDev = nullptr;
    if (SUCCEEDED(g_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev))) {
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory* factory = nullptr;
        if (SUCCEEDED(dxgiDev->GetAdapter(&adapter))) {
            if (SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory))) {
                factory->MakeWindowAssociation(hw, DXGI_MWA_NO_ALT_ENTER);
                factory->Release();
            }
            adapter->Release();
        }
        dxgiDev->Release();
    }
    return true;
}

static void ShutdownD3D() {
    if (g_rtv)  { g_rtv->Release();  g_rtv  = nullptr; }
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx)  { g_ctx->Release();  g_ctx  = nullptr; }
    if (g_dev)  { g_dev->Release();  g_dev  = nullptr; }
}

// -----------------------------------------------------------------------------
// Frame build - one entity scan, shared by every feature.
// -----------------------------------------------------------------------------
static void BuildFrame(bool wantNames) {
    g_frame.players.clear();
    g_frame.skeletons.clear();
    g_frame.haveLocal = false;

    g_nav.refresh();
    if (!g_nav.client()) return;

    uintptr_t lp = g_nav.localPawn();
    if (lp) {
        Player local;
        if (g_nav.readPlayer(lp, local, false)) {
            g_frame.local = local;
            g_frame.haveLocal = true;
        }
    }

    g_frame.viewAngles = g_nav.viewAngles();
    g_frame.screen.w = g_screenW;
    g_frame.screen.h = g_screenH;
    g_frame.screen.valid = g_nav.viewMatrix(g_frame.screen.m);
    if (g_frame.screen.valid) fixViewMatrix(g_frame.screen.m);
    g_frame.screenCenter = { g_screenW * 0.5f, g_screenH * 0.5f };

    int myTeam = g_frame.haveLocal ? g_frame.local.team : 0;
    g_nav.scanPlayers(g_frame.players, wantNames, myTeam);

    // Remove the local pawn - it is a valid record and would box the camera.
    if (g_frame.haveLocal) {
        for (size_t i = 0; i < g_frame.players.size();) {
            if (g_frame.players[i].pawn == g_frame.local.pawn) {
                g_frame.players.erase(g_frame.players.begin() + (ptrdiff_t)i);
            } else {
                ++i;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// ESP
// -----------------------------------------------------------------------------
static ImU32 toU32(const float c[4], float alphaScale = 1.f) {
    float a = clampf(c[3] * alphaScale, 0.f, 1.f);
    return IM_COL32((int)(c[0] * 255.f), (int)(c[1] * 255.f),
                    (int)(c[2] * 255.f), (int)(a * 255.f));
}

struct Box2 { Vec2 topLeft, bottomRight; bool valid = false; };

// Screen box from projected head and foot bones. Falls back to an estimated
// head/foot pair so ESP still renders when the bone array cannot be read.
static Box2 computeBox(const Nav& nav, const Player& pl, const Frame& f) {
    Box2 box;

    Vec3 head, foot;
    bool haveHead = nav.bone(pl.pawn, Bone::HEAD, pl.origin, head);
    bool haveFoot = nav.bone(pl.pawn, Bone::R_FOOT, pl.origin, foot)
                 || nav.bone(pl.pawn, Bone::L_FOOT, pl.origin, foot);

    Vec3 headPt = haveHead ? head : Vec3{ pl.origin.x, pl.origin.y, pl.origin.z + 72.f };
    Vec3 footPt = haveFoot ? foot : Vec3{ pl.origin.x, pl.origin.y, pl.origin.z - 2.f };

    Vec2 hs, fs;
    if (!f.screen.project(headPt, hs)) return box;
    if (!f.screen.project(footPt, fs)) return box;
    if (!std::isfinite(hs.x) || !std::isfinite(fs.x)) return box;

    float top = hs.y;
    float bot = fs.y;
    if (bot <= top) return box;

    float h = bot - top;
    if (h < 4.f || h > 4000.f) return box;

    float w  = h * 0.42f;
    float cx = (hs.x + fs.x) * 0.5f;

    box.topLeft     = { cx - w * 0.5f, top };
    box.bottomRight = { cx + w * 0.5f, bot };
    box.valid = true;
    return box;
}

static void DrawBox(ImDrawList* dl, const Box2& b, ImU32 col, float th, bool filled) {
    ImVec2 tl(b.topLeft.x, b.topLeft.y);
    ImVec2 br(b.bottomRight.x, b.bottomRight.y);

    if (filled) {
        dl->AddRectFilled(tl, br,
            IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 30), 0.f);
    }
    dl->AddRect(tl, br, IM_COL32(0, 0, 0, 160), 0.f, 0, th + 2.f);
    dl->AddRect(tl, br, col, 0.f, 0, th);
}

static void DrawHealthBar(ImDrawList* dl, const Box2& b, int hp, int maxHp) {
    if (maxHp <= 0) maxHp = 100;
    float pct = clampf((float)hp / (float)maxHp, 0.f, 1.f);

    float h = b.bottomRight.y - b.topLeft.y;
    float x = b.topLeft.x - 6.f;
    ImVec2 barMin(x, b.topLeft.y);
    ImVec2 barMax(x + 3.f, b.bottomRight.y);

    dl->AddRectFilled(ImVec2(barMin.x - 1.f, barMin.y - 1.f),
                      ImVec2(barMax.x + 1.f, barMax.y + 1.f),
                      IM_COL32(0, 0, 0, 190));

    ImU32 fill = IM_COL32((int)(255.f * (1.f - pct)), (int)(255.f * pct), 0, 230);
    ImVec2 fMin(barMin.x, barMax.y - h * pct);
    dl->AddRectFilled(fMin, barMax, fill);
}

static void DrawText(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* txt) {
    dl->AddText(ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 210), txt);
    dl->AddText(pos, col, txt);
}

static void RenderESP(ImDrawList* dl) {
    if (!g_cfg.esp || !g_frame.haveLocal) return;
    if (!g_frame.screen.valid) return;

    const int myTeam = g_frame.local.team;

    for (const Player& pl : g_frame.players) {
        if (g_cfg.espTeamFilter == 1 && pl.team == myTeam) continue;
        if (!pl.alive()) continue;

        float dist = vlen(pl.origin - g_frame.local.origin);
        if (dist > g_cfg.espMaxDist) continue;

        bool enemy = (pl.team != myTeam);
        ImU32 col  = enemy ? toU32(g_cfg.espEnemyCol) : toU32(g_cfg.espTeamCol);

        Box2 box = computeBox(g_nav, pl, g_frame);
        if (!box.valid) continue;

        if (g_cfg.espFilled) DrawBox(dl, box, col, g_cfg.espThickness, true);
        if (g_cfg.espBox)    DrawBox(dl, box, col, g_cfg.espThickness, false);

        if (g_cfg.espHealth) DrawHealthBar(dl, box, pl.health, pl.maxHealth);

        float textY = box.topLeft.y - 15.f;
        if (g_cfg.espName && pl.name[0]) {
            DrawText(dl, ImVec2(box.topLeft.x, textY), col, pl.name);
            textY -= 15.f;
        }

        if (g_cfg.espDistance) {
            char buf[64];
            sprintf_s(buf, "%.0f m", dist * 0.0254f);
            DrawText(dl, ImVec2(box.topLeft.x, box.bottomRight.y + 2.f), col, buf);
        }

        if (g_cfg.espSkeleton) {
            Frame::BoneSet bs;
            loadSkeleton(g_nav, pl, bs);

            for (const auto& pair : kSkeletonPairs) {
                int a = pair[0];
                int b = pair[1];
                if (!bs.ok[a] || !bs.ok[b]) continue;

                Vec2 sa, sb;
                if (!g_frame.screen.project(bs.pts[a], sa)) continue;
                if (!g_frame.screen.project(bs.pts[b], sb)) continue;

                dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), col, 1.2f);
            }
        }

        if (g_cfg.espSnaplines) {
            dl->AddLine(ImVec2((float)g_screenW * 0.5f, (float)g_screenH),
                        ImVec2(box.bottomRight.x, box.bottomRight.y),
                        col, 1.f);
        }
    }

    if (g_cfg.aimbot) {
        float radius = std::tanf(g_cfg.aimFov * DEG2RAD) * ((float)g_screenH * 0.5f);
        radius = clampf(radius, 4.f, 4000.f);
        dl->AddCircle(ImVec2(g_frame.screenCenter.x, g_frame.screenCenter.y),
                      radius, IM_COL32(255, 255, 255, 60), 0, 1.0f);
    }
}

// -----------------------------------------------------------------------------
// Menu
// -----------------------------------------------------------------------------
static void DrawMenu() {
    if (!g_menuOpen) return;

    ImGui::SetNextWindowSize(ImVec2(UI::kWindowW, UI::kWindowH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);

    ImGui::Begin("cs2ext", &g_menuOpen, UI::kFlags);

    ImGui::TextDisabled("build %s   |   INSERT menu   |   END quit", Off::BUILD_NUMBER);
    ImGui::TextColored(g_attached ? ImVec4(0.3f, 1.f, 0.3f, 1.f) : ImVec4(1.f, 0.3f, 0.3f, 1.f),
                       g_attached ? "cs2.exe: attached" : "cs2.exe: not attached");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("ESP", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable ESP", &g_cfg.esp);
        ImGui::SameLine();
        ImGui::Checkbox("Box", &g_cfg.espBox);
        ImGui::SameLine();
        ImGui::Checkbox("Fill", &g_cfg.espFilled);

        ImGui::Checkbox("Health bar", &g_cfg.espHealth);
        ImGui::SameLine();
        ImGui::Checkbox("Name", &g_cfg.espName);
        ImGui::SameLine();
        ImGui::Checkbox("Distance", &g_cfg.espDistance);

        ImGui::Checkbox("Skeleton", &g_cfg.espSkeleton);
        ImGui::SameLine();
        ImGui::Checkbox("Snaplines", &g_cfg.espSnaplines);

        ImGui::SliderFloat("Max dist", &g_cfg.espMaxDist, 200.f, 8000.f, "%.0f u");
        ImGui::SliderFloat("Thickness", &g_cfg.espThickness, 1.f, 5.f, "%.1f");

        const char* filters[] = { "All", "Enemy only" };
        ImGui::Combo("Team filter", &g_cfg.espTeamFilter, filters, 2);

        ImGui::ColorEdit4("Enemy colour", g_cfg.espEnemyCol, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Team colour", g_cfg.espTeamCol, ImGuiColorEditFlags_NoInputs);
    }

    if (ImGui::CollapsingHeader("Aimbot")) {
        ImGui::Checkbox("Enable aimbot", &g_cfg.aimbot);
        ImGui::SameLine();
        ImGui::Checkbox("Visible only", &g_cfg.aimVisibleOnly);

        ImGui::SliderFloat("FOV", &g_cfg.aimFov, 1.f, 90.f, "%.1f deg");
        ImGui::SliderFloat("Smooth", &g_cfg.aimSmooth, 1.f, 20.f, "%.1f");
        ImGui::SliderFloat("Max dist##aim", &g_cfg.aimMaxDist, 200.f, 8000.f, "%.0f u");

        const char* bones[]  = { "Head", "Neck", "Spine", "Chest", "Pelvis" };
        const int   boneVals[] = { Bone::HEAD, Bone::NECK, Bone::SPINE, Bone::CHEST, Bone::PELVIS };
        int boneSel = 0;
        for (int i = 0; i < 5; ++i) if (boneVals[i] == g_cfg.aimBone) boneSel = i;
        if (ImGui::Combo("Bone", &boneSel, bones, 5)) g_cfg.aimBone = boneVals[boneSel];

        const char* modes[] = { "Mouse (relative)", "Angle write" };
        int mode = (int)g_cfg.aimMode;
        if (ImGui::Combo("Method", &mode, modes, 2)) g_cfg.aimMode = (AimMode)mode;

        const char* keys[] = { "Right mouse", "Left mouse", "Shift", "Alt", "Mouse 4" };
        int key = (int)g_cfg.aimKey;
        if (ImGui::Combo("Hotkey", &key, keys, 5)) g_cfg.aimKey = (AimKey)key;

        ImGui::Checkbox("Auto fire", &g_cfg.aimAutoFire);
    }

    if (ImGui::CollapsingHeader("Triggerbot")) {
        ImGui::Checkbox("Enable trigger", &g_cfg.triggerbot);
        ImGui::SliderInt("Delay ms", &g_cfg.triggerDelayMs, 0, 250);
        ImGui::Checkbox("Enemy only", &g_cfg.triggerTeamFlag);
        ImGui::SameLine();
        ImGui::Checkbox("Head checkpoint", &g_cfg.triggerHeadOnly);
    }

    if (ImGui::CollapsingHeader("Bhop")) {
        ImGui::Checkbox("Enable bhop", &g_cfg.bhop);
        ImGui::Checkbox("Auto strafe", &g_cfg.bhopAutoStrafe);
    }

    if (ImGui::CollapsingHeader("Glow")) {
        ImGui::Checkbox("Enable glow", &g_cfg.glow);
        ImGui::SliderInt("Glow type", &g_cfg.glowType, 1, 3);
        ImGui::ColorEdit4("Enemy glow", g_cfg.glowEnemy, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Team glow", g_cfg.glowTeam, ImGuiColorEditFlags_NoInputs);
        if (g_cfg.glow && g_proc.hw == nullptr) {
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0.2f, 1.f), "write handle unavailable");
        }
    }

    if (ImGui::CollapsingHeader("Misc")) {
        ImGui::Checkbox("No flash", &g_cfg.noFlash);
        ImGui::Separator();
        ImGui::TextDisabled("entities: %d   local: %s",
                            (int)g_frame.players.size(),
                            g_frame.haveLocal ? "ok" : "none");
    }

    ImGui::End();
}

// -----------------------------------------------------------------------------
// Frame tick
// -----------------------------------------------------------------------------
// Diagnostics - written next to the exe. A black screen leaves a readable trail
// instead of a guess: which surface was picked, swap-chain HRESULT, present
// result, and the first few frames of state.
// -----------------------------------------------------------------------------
static void LogLine(const char* fmt, ...) {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* dot = strrchr(path, '.');
    if (dot) *dot = '\0';
    strcat_s(path, "_log.txt");

    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void LogSurface(const char* tag) {
    if (!g_overlay) { LogLine("%s surface=none", tag); return; }

    LONG ex = GetWindowLongW(g_overlay, GWL_EXSTYLE);
    LogLine("%s mode=%s hwnd=%p size=%dx%d layered=%s transparent=%s",
            tag,
            g_ownWindow ? "own-window" : "inherited",
            (void*)g_overlay, g_screenW, g_screenH,
            (ex & WS_EX_LAYERED) ? "YES" : "no",
            (ex & WS_EX_TRANSPARENT) ? "yes" : "no");
}

// -----------------------------------------------------------------------------
static void UpdateOverlayStyle() {
    // Only our own window is styled. The game's HWND is never touched - setting
    // WS_EX_TRANSPARENT on it would break its own input and pump.
    if (!g_ownWindow || !g_overlay) return;

    LONG ex   = GetWindowLongW(g_overlay, GWL_EXSTYLE);
    LONG want = g_menuOpen ? (ex & ~WS_EX_TRANSPARENT) : (ex | WS_EX_TRANSPARENT);
    if (want != ex) SetWindowLongW(g_overlay, GWL_EXSTYLE, want);

    SetWindowPos(g_overlay, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// Handle a resize (alt-tab, fullscreen toggle, the game recreating its window).
static void HandleSurfaceChange() {
    if (!g_ownWindow || !g_overlay) return;
    if (!IsWindow(g_overlay)) { g_running = false; return; }

    RECT r{};
    GetClientRect(g_overlay, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w < 32 || h < 32) return;

    if (w != g_screenW || h != g_screenH) {
        g_screenW = w;
        g_screenH = h;
        if (g_swap) { g_rtv->Release(); g_rtv = nullptr; }
        if (g_swap && FAILED(g_swap->ResizeBuffers(2, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0))) {
            ShutdownD3D();
            InitD3D(g_overlay);
            if (g_rtv) ImGui_ImplDX11_InvalidateDeviceObjects();
        } else if (g_swap) {
            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && back) {
                g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
                back->Release();
            }
        }
    }
}

static void TickFeatures() {
    static uint32_t tick = 0;
    ++tick;

    // Names are only needed while visible in the menu, plus a slow background
    // refresh so a name is ready the moment the option is switched on.
    bool wantNames = (g_cfg.esp && g_cfg.espName && tick % 60 == 0);

    BuildFrame(wantNames);

    if (g_attached && !g_proc.alive()) {
        g_glow.dropAll();
        g_attached = false;
    }

    if (!g_attached) return;

    g_bhop.tick(g_nav, g_cfg, g_frame);
    g_trigger.tick(g_nav, g_cfg, g_frame);
    g_aim.tick(g_nav, g_cfg, g_frame);
    g_glow.tick(g_nav, g_cfg, g_frame);
    g_misc.tick(g_nav, g_cfg, g_frame);
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Attach to the game, trying two routes.
//
//   1. Name search - Toolhelp snapshot for cs2.exe. Fast, works when the game is
//      visible to this process.
//
//   2. Window owner - enumerate top-level windows, take the PID of the one whose
//      class is SDL_app (what CS2 creates) or whose title mentions Counter-Strike.
//      This survives a launcher that hides the process from a plain snapshot:
//      windows stay enumerable even when the process list does not show the game.
//
// Both routes end in the same access check. A failure records why, so the log
// carries the actual Win32 error instead of "attached=0" and nothing else.
// -----------------------------------------------------------------------------
static DWORD FindGamePidByWindow() {
    struct Ctx { DWORD pid; };
    Ctx ctx{ 0 };

    EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
        Ctx* c = (Ctx*)lp;
        if (!IsWindowVisible(hw)) return TRUE;

        RECT r{};
        GetWindowRect(hw, &r);
        if ((r.right - r.left) < 640 || (r.bottom - r.top) < 480) return TRUE;

        char cls[128]{};
        GetClassNameA(hw, cls, sizeof(cls));

        bool looksLikeGame = (_stricmp(cls, "SDL_app") == 0);
        if (!looksLikeGame) {
            char title[256]{};
            GetWindowTextA(hw, title, sizeof(title));
            if (strstr(title, "Counter-Strike") || strstr(title, "CS2")) looksLikeGame = true;
        }
        if (!looksLikeGame) return TRUE;

        DWORD wpid = 0;
        GetWindowThreadProcessId(hw, &wpid);
        if (!wpid || wpid == GetCurrentProcessId()) return TRUE;

        c->pid = wpid;
        return FALSE;
    }, (LPARAM)&ctx);

    return ctx.pid;
}

static void LogWide(const char* fmt, const std::wstring& wide) {
    char narrow[512]{};
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    LogLine(fmt, narrow);
}

static bool AttachGame() {
    if (g_proc.attach(L"cs2.exe")) return true;
    LogWide("attach via name failed: %s", g_proc.lastError());

    DWORD wpid = FindGamePidByWindow();
    if (!wpid) {
        LogLine("no game window found either (is CS2 running a map?)");
        return false;
    }
    LogLine("found game window owner pid=%lu - retrying attach by pid", (unsigned long)wpid);

    if (g_proc.attachPid(wpid)) return true;
    LogWide("attach via window pid failed: %s", g_proc.lastError());
    return false;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();

    // Attach first: the surface depends on whether the game has a window.
    g_attached = AttachGame();
    g_nav.refresh();

    LogLine("=== cs2ext start === build %s  attached=%d pid=%lu",
            Off::BUILD_NUMBER, (int)g_attached, (unsigned long)g_proc.pid);

    if (!ResolveSurface()) {
        LogLine("ResolveSurface FAILED");
        MessageBoxA(nullptr, "no render surface - is cs2.exe running?", "cs2ext",
                    MB_ICONERROR);
        return 1;
    }
    LogSurface("surface resolved");

    // Try the resolved surface. If the inherited game window refuses every swap
    // effect, fall back to our own topmost window instead of aborting - a
    // standalone overlay still draws ESP over the game, it just cannot use the
    // game's own presentation.
    if (!InitD3D(g_overlay)) {
        if (!g_ownWindow) {
            LogLine("inherited surface rejected by DXGI - falling back to own window");
            CreateFallbackWindow();
            if (!InitD3D(g_overlay)) {
                LogLine("InitD3D FAILED on fallback too");
                MessageBoxA(nullptr, "D3D11 init failed on every surface.\n"
                            "Set CS2 to 'Fullscreen Windowed' and retry.",
                            "cs2ext", MB_ICONERROR);
                DestroyWindow(g_overlay);
                return 1;
            }
        } else {
            LogLine("InitD3D FAILED for hwnd=%p %dx%d", (void*)g_overlay, g_screenW, g_screenH);
            MessageBoxA(nullptr, "D3D11 init failed.", "cs2ext", MB_ICONERROR);
            DestroyWindow(g_overlay);
            return 1;
        }
    }
    LogLine("InitD3D ok  swap=%p rtv=%p mode=%s", (void*)g_swap, (void*)g_rtv,
            g_ownWindow ? "own-window" : "inherited");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 0.f;
    style.FrameRounding    = 3.f;
    style.WindowBorderSize = 1.f;
    style.WindowPadding    = ImVec2(10, 10);

    ImGui_ImplWin32_Init(g_overlay);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    uint64_t lastAttachTry = GetTickCount64();
    unsigned presentCount = 0;

    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        if (!g_attached && GetTickCount64() - lastAttachTry > 2000) {
            lastAttachTry = GetTickCount64();
            if (AttachGame()) {
                g_attached = true;
                g_nav.refresh();
                LogLine("attached pid=%lu image=%p", (unsigned long)g_proc.pid, (void*)g_proc.exeBase);
            } else {
                // Log the reason once per distinct failure, not every 2 seconds.
                std::wstring err = g_proc.lastError();
                static std::wstring lastErr;
                if (err != lastErr) {
                    lastErr = err;
                    char narrow[512]{};
                    WideCharToMultiByte(CP_UTF8, 0, err.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
                    LogLine("attach FAILED: %s", narrow);
                }
                // Memory is unreachable, but the window may still be inherited so
                // the overlay draws once access is granted.
                if (!g_ownWindow) {
                    HWND w = FindTargetWindow();
                    if (w) { g_overlay = w; g_ownWindow = false;
                             RECT r{}; GetClientRect(w, &r);
                             g_screenW = r.right - r.left; g_screenH = r.bottom - r.top; }
                }
            }
        }

        UpdateOverlayStyle();
        HandleSurfaceChange();
        if (!g_rtv) { Sleep(10); continue; }

        TickFeatures();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        RenderESP(dl);
        DrawMenu();

        ImGui::Render();

        // Clear colour depends on which surface is in use.
        //
        // Inherited: alpha 0. The game's frame is already the background, so the
        // overlay contributes only what ImGui draws.
        //
        // Own window: the swap chain is opaque, so alpha means nothing. Paint the
        // exact colour-key magenta; Windows keys that one colour out and leaves
        // ImGui's widgets intact.
        float clear[4];
        if (g_ownWindow) {
            clear[0] = 1.0f; clear[1] = 0.0f; clear[2] = 1.0f; clear[3] = 1.0f;
        } else {
            clear[0] = 0.0f; clear[1] = 0.0f; clear[2] = 0.0f; clear[3] = 0.0f;
        }
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_swap->Present(0, 0);
        if (++presentCount <= 3 || FAILED(hr)) {
            LogLine("present #%u hr=0x%08lX windowed=%dx%d rtv=%p",
                    presentCount, (unsigned long)hr, g_screenW, g_screenH, (void*)g_rtv);
        }
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            // GPU reset or driver swap - rebuild from scratch against the same HWND.
            ShutdownD3D();
            ImGui_ImplDX11_InvalidateDeviceObjects();
            if (!InitD3D(g_overlay)) { Sleep(500); continue; }
        }

        Sleep(1);
    }

    g_glow.dropAll();
    g_proc.detach();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    ShutdownD3D();
    if (g_ownWindow && g_overlay) DestroyWindow(g_overlay);
    UnregisterClassW(L"cs2ext_overlay", GetModuleHandleW(nullptr));
    return 0;
}
