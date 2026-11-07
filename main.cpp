// muei 無影 - CS2 external overlay.
//
// This file is orchestration only: it picks a memory backend, builds one frame
// snapshot per rendered frame, hands it to every feature, then draws. All real
// work lives in core/, sdk/, features/ and ui/.
//
// Layout:
//   core/      memory backends, config, logging
//   sdk/       offsets, math, entity model, traversal
//   features/  one file per cheat feature
//   ui/        theme, D3D11 surface, menu
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>

#include "core/Config.h"
#include "core/Log.h"
#include "core/ProcDMA.h"
#include "core/ProcKernel.h"
#include "core/ProcUser.h"

#include "sdk/Frame.h"
#include "sdk/Nav.h"
#include "sdk/Offsets.h"

#include "features/Aimbot.h"
#include "features/Bhop.h"
#include "features/ESP.h"
#include "features/Glow.h"
#include "features/Input.h"
#include "features/Misc.h"
#include "features/RCS.h"
#include "features/SkinChanger.h"
#include "features/Triggerbot.h"

#include "ui/Menu.h"
#include "ui/Overlay.h"
#include "ui/Theme.h"

namespace {

// ---------------------------------------------------------------- globals ---

Config  g_cfg;
ProcUser g_user;
ProcKernel g_kernel;
ProcDMA  g_dma;
IMemory* g_mem = nullptr;

std::unique_ptr<Nav>    g_nav;
Frame                   g_frame;

Aimbot      g_aimbot;
Triggerbot  g_trigger;
Bhop        g_bhop;
Glow        g_glow;
RCS         g_rcs;
SkinChanger g_skins;
Misc        g_misc;

overlay::Instance g_overlay;
menu::State       g_menu;

bool     g_attached      = false;
uint64_t g_lastAttach    = 0;
uint32_t g_attachBackoff = 2000;      // grows on each failure, capped below
int      g_attachFails   = 0;
bool     g_prevMenuKey   = false;
bool     g_prevPanic     = false;

// Bounds the retry cost while the game is not running: without this the loop
// re-runs the whole backend probe every two seconds and fills the log with the
// same four lines forever.
constexpr uint32_t kAttachBackoffMax = 30000;

// Config autosave: armed by a menu edit, flushed after the operator stops.
bool     g_dirty      = false;
uint64_t g_dirtyAt    = 0;

uint32_t g_tick = 0;

// -------------------------------------------------------------- config path --
std::wstring exeDirFile(const wchar_t* name) {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash) *(slash + 1) = L'\0';
    return std::wstring(exe) + name;
}

// ----------------------------------------------------------- attach logic ---
DWORD findGamePidByWindow() {
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

// Try one backend: name search first, then the window owner (a launcher can hide
// the process from a plain snapshot while leaving its window enumerable).
bool tryAttach(IMemory& mem) {
    if (mem.attach(L"cs2.exe")) return true;
    logx::wide("attach by name failed", mem.lastError());

    DWORD wpid = findGamePidByWindow();
    if (!wpid) {
        logx::line("no game window found either (is a map loaded?)");
        return false;
    }

    logx::line("retrying attach by window-owner pid=%lu", (unsigned long)wpid);
    if (mem.attachPid(wpid)) return true;
    logx::wide("attach by pid failed", mem.lastError());
    return false;
}

bool attemptAttach() {
    g_attached = false;
    g_mem = nullptr;

    const Backend wanted = (Backend)g_cfg.backend;

    auto pick = [&](IMemory& mem) -> bool {
        if (!tryAttach(mem)) {
            mem.detach();
            return false;
        }
        g_mem = &mem;
        return true;
    };

    if (wanted == Backend::Kernel)      pick(g_kernel);
    else if (wanted == Backend::DMA)    pick(g_dma);
    else if (wanted == Backend::User)   pick(g_user);
    else {
        // Auto: cheapest and most reliable first. DMA is last because a failed
        // FPGA probe costs seconds, not milliseconds.
        if (!pick(g_kernel)) {
            if (!pick(g_user)) {
                pick(g_dma);
            }
        }
    }

    if (!g_mem) {
        logx::line("attach failed on every backend");
        return false;
    }

    // Only the kernel backend can route input; the others get SendInput.
    input::attachDriver(g_mem == &g_kernel ? &g_kernel : nullptr);

    if (!g_nav) {
        g_nav = std::make_unique<Nav>(*g_mem);
    } else {
        g_nav.reset(new Nav(*g_mem));
    }
    g_nav->refresh();

    g_attached = true;
    logx::line("attached backend=%ls pid=%lu client=%p",
               g_mem->backendName(), (unsigned long)g_mem->pid(),
               (void*)g_nav->client());
    return true;
}

// ------------------------------------------------------------- frame build --
void buildFrame() {
    g_frame.players.clear();
    g_frame.skeletons.clear();
    g_frame.haveLocal = false;

    if (!g_attached || !g_nav) return;
    if (!g_nav->client()) {
        g_nav->refresh();
        if (!g_nav->client()) return;
    }

    // Names only matter while they are on screen, plus a slow refresh so a name
    // is ready the moment the option is toggled.
    const bool wantNames = g_cfg.esp && g_cfg.espName && (g_tick % 45 == 0);

    uintptr_t localPawn = g_nav->localPawn();
    if (localPawn) {
        Player local;
        if (g_nav->readPlayer(localPawn, local, false)) {
            g_frame.local = local;
            g_frame.haveLocal = true;
        }
    }

    g_frame.viewAngles = g_nav->viewAngles();

    const int w = g_overlay.width();
    const int h = g_overlay.height();
    g_frame.screen.w = w;
    g_frame.screen.h = h;
    g_frame.screen.valid = g_nav->viewMatrix(g_frame.screen.m);
    if (g_frame.screen.valid) fixViewMatrix(g_frame.screen.m);
    g_frame.screenCenter = { w * 0.5f, h * 0.5f };

    g_nav->scanPlayers(g_frame.players, wantNames);
}

void tickFeatures() {
    if (!g_attached || !g_mem || !g_nav) return;
    if (!g_mem->alive()) {
        logx::line("target process went away, dropping attachment");
        g_glow.restoreAll();
        g_mem->detach();
        g_attached = false;
        g_mem = nullptr;
        return;
    }

    // Open the write handle only when a feature actually needs it, and only once.
    const bool wantWrite = g_cfg.glow || g_cfg.noFlash || g_cfg.skinChanger ||
                           g_cfg.aimMode == AimMode::Write ||
                           (g_cfg.aimbot && g_cfg.aimMode == AimMode::Write);
    if (wantWrite) g_mem->canWrite();

    g_bhop.tick(*g_nav, g_cfg, g_frame);
    g_rcs.tick(*g_nav, g_cfg, g_frame);
    g_trigger.tick(*g_nav, g_cfg, g_frame);
    g_aimbot.tick(*g_nav, g_cfg, g_frame);
    g_glow.tick(*g_nav, g_cfg, g_frame);
    g_skins.tick(*g_nav, g_cfg, g_frame);
    g_misc.tick(*g_nav, g_cfg, g_frame);
}

void pollKeys() {
    const bool menuKey = input::keyDown(g_cfg.menuKey);
    if (menuKey && !g_prevMenuKey) {
        g_menu.open = !g_menu.open;
        g_overlay.setMenuOpen(g_menu.open);
        logx::line("menu %s", g_menu.open ? "opened" : "closed");
    }
    g_prevMenuKey = menuKey;

    const bool panic = input::keyDown(g_cfg.panicKey);
    if (panic && !g_prevPanic) {
        logx::line("panic key, shutting down");
        PostQuitMessage(0);
    }
    g_prevPanic = panic;
}

void flushDirtyConfig() {
    if (!g_dirty) return;
    if (GetTickCount64() - g_dirtyAt < 1200) return;

    g_dirty = false;
    if (g_cfg.save()) logx::line("config saved");
}

menu::Status buildStatus() {
    menu::Status st;
    st.attached  = g_attached;
    st.backend   = g_mem ? (g_mem == &g_kernel ? "kernel"
                            : g_mem == &g_dma ? "dma" : "user")
                         : "none";
    st.pid       = g_mem ? (unsigned long)g_mem->pid() : 0;
    st.players   = (int)g_frame.players.size();
    st.aimTarget = g_aimbot.targetIndex();
    st.rcsReady  = RCS::available();
    st.skinReady = SkinChanger::available();
    st.skinMissing = SkinChanger::available() ? std::string() : SkinChanger::missing();
    st.build     = g_off.build;
    return st;
}

void drawMenuIfOpen(bool& cfgChanged) {
    if (!g_menu.open) return;
    cfgChanged = menu::draw(g_menu, g_cfg, g_frame, buildStatus());
}

} // namespace

// ------------------------------------------------------------------- entry ---

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();
    logx::init("muei");

    // ------------------------------------------------------------- data ----
    g_cfg.load();
    loadOffsetOverrides(exeDirFile(L"offsets.cfg"));

    if (!RCS::available()) {
        logx::line("startup: recoil control unavailable (punch offsets unknown)");
    }
    if (!SkinChanger::available()) {
        logx::line("startup: skin changer unavailable, missing: %s",
                   SkinChanger::missing().c_str());
    }

    // ---------------------------------------------------------- attach -----
    attemptAttach();

    // --------------------------------------------------------- surface -----
    DWORD surfacePid = 0;
    if (g_mem) {
        surfacePid = g_mem->pid();
    } else {
        surfacePid = findGamePidByWindow();
    }

    if (!g_overlay.init(surfacePid)) {
        logx::line("overlay init failed");
        MessageBoxA(nullptr,
                    "muei: no render surface.\n\n"
                    "Start CS2 in Fullscreen Windowed and run muei as administrator.",
                    "muei", MB_ICONERROR);
        return 1;
    }

    theme::apply(g_cfg.accentCol);
    g_overlay.setMenuOpen(g_menu.open);

    // ------------------------------------------------------------ loop -----
    for (;;) {
        if (!g_overlay.pump()) break;

        ++g_tick;
        pollKeys();

        const uint64_t now = GetTickCount64();
        if (!g_attached && now - g_lastAttach >= g_attachBackoff) {
            g_lastAttach = now;

            if (attemptAttach()) {
                g_attachBackoff = 2000;
                g_attachFails   = 0;
                // A game window that appeared after we built our own surface is
                // worth retargeting onto.
                if (g_mem) g_overlay.refitToGame(g_mem->pid());
            } else {
                ++g_attachFails;
                uint32_t next = g_attachBackoff * 2;
                if (next > kAttachBackoffMax) next = kAttachBackoffMax;
                g_attachBackoff = next;
                logx::line("attach retry %d failed, next attempt in %lu ms",
                           g_attachFails, (unsigned long)g_attachBackoff);
            }
        }

        buildFrame();
        tickFeatures();

        g_overlay.begin();

        ImDrawList* bg = g_overlay.background();
        if (g_nav) esp::draw(bg, *g_nav, g_cfg, g_frame);

        bool cfgChanged = false;
        drawMenuIfOpen(cfgChanged);
        if (cfgChanged) {
            theme::apply(g_cfg.accentCol);
            g_dirty   = true;
            g_dirtyAt = GetTickCount64();
        }

        g_overlay.end();

        flushDirtyConfig();
        Sleep(1);
    }

    // ---------------------------------------------------------- shutdown ---
    g_glow.restoreAll();
    if (g_mem) g_mem->detach();
    g_cfg.save();
    g_overlay.shutdown();

    logx::line("=== muei shutdown ===");
    return 0;
}
