// muei 無影 - CS2 external overlay.
//
// Pure external: one process, one ReadProcessMemory handle, one layered window
// drawn on top of the game. No driver, no second machine, no service, nothing to
// install beyond this exe.
//
// This file is orchestration only: attach, build one frame snapshot per rendered
// frame, hand it to every feature, draw. All real work lives in core/, sdk/,
// features/ and ui/.
//
// Layout:
//   core/      process handle, config, logging
//   sdk/       offsets, math, entity model, traversal
//   features/  one file per cheat feature
//   ui/        theme, D3D11 surface, menu
#include <windows.h>
#include <shellapi.h>

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <string>

#pragma comment(lib, "shell32.lib")

#include "core/Config.h"
#include "core/Log.h"
#include "core/ProcUser.h"
#include "core/SelfTest.h"

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

Config   g_cfg;
ProcUser g_mem;

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
// retries the attach every two seconds and fills the log with the
// same four lines forever.
constexpr uint32_t kAttachBackoffMax = 30000;

// Config autosave: armed by a menu edit, flushed after the operator stops.
bool     g_dirty      = false;
uint64_t g_dirtyAt    = 0;

uint32_t g_tick = 0;
bool     g_elevated = false;

// --------------------------------------------------------------- elevation ---
// CS2 runs at High integrity. A process at Medium cannot open it for read no
// matter what rights it asks for, and the resulting failure ("error 5") looks
// exactly like a broken cheat: the menu renders, the ESP is empty.
//
// The MSVC build embeds a requireAdministrator manifest, so it never reaches
// this path. The MinGW build cannot embed one here, and a double-clicked exe
// gets no elevation - so re-launch ourselves through the shell's runas verb and
// exit. If the operator declines the UAC prompt we keep running, but say so
// loudly in the log and in the menu instead of pretending to be waiting.
bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                                        sizeof(elevation), &size) &&
                    elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return ok;
}

bool relaunchElevated() {
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;

    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"runas";
    sei.lpFile       = exe;
    sei.lpParameters = nullptr;   // the overlay takes no arguments
    sei.nShow        = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        // ERROR_CANCELLED is the usual one: the operator clicked No on UAC.
        logx::win32("elevation request failed", GetLastError());
        return false;
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}

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

// Attach: name search first, then the window owner. A launcher can hide the
// process from a plain snapshot while leaving its window enumerable, and CS2
// itself is sometimes invisible to the snapshot route.
void diagnoseOnce();

bool attemptAttach() {
    g_attached = false;

    if (g_mem.attach(L"cs2.exe")) goto attached;
    logx::wide("attach by name failed", g_mem.lastError());

    {
        const DWORD wpid = findGamePidByWindow();
        if (!wpid) {
            logx::line("no game window either - is CS2 running with a map loaded?");
            g_mem.detach();
            return false;
        }

        logx::line("retrying attach by window-owner pid=%lu", (unsigned long)wpid);
        if (g_mem.attachPid(wpid)) goto attached;
        logx::wide("attach by pid failed", g_mem.lastError());
        // fall through to the shared failure path
    }

    g_mem.detach();
    return false;

attached:
    g_nav = std::make_unique<Nav>(g_mem);
    g_nav->refresh();

    if (!g_nav->client()) {
        logx::line("attached but client.dll did not resolve");
        g_mem.detach();
        return false;
    }

    g_attached = true;
    logx::line("attached pid=%lu client=%p", (unsigned long)g_mem.pid(),
               (void*)g_nav->client());
    diagnoseOnce();
    return true;
}

// ------------------------------------------------------------- diagnostics --
// Run once per attach. Turns "0 players" into a specific reason: a stale RVA, a
// missing spawn, or simply not being in a match. Without this the overlay looks
// identical in all three cases.
void diagnoseOnce() {
    if (!g_nav) return;

    const uintptr_t list = g_nav->entityList();
    if (!IMemory::plausible(list)) {
        logx::line("DIAG: entity list unreadable at client+0x%llX - offsets are stale, "
                   "refresh offsets.cfg (run verify.bat)",
                   (unsigned long long)g_off.dwEntityList);
        return;
    }

    const uintptr_t local = g_nav->localPawn();

    float m[16]{};
    const bool viewOk = g_nav->viewMatrix(m);

    std::vector<Player> players;
    g_nav->scanPlayers(players, false);

    logx::line("DIAG: list=%p local=%p viewmatrix=%s players=%zu",
               (void*)list, (void*)local, viewOk ? "ok" : "FAIL", players.size());

    if (!local) {
        logx::line("DIAG: no local pawn - you are not spawned into a map yet");
    }
    if (!viewOk) {
        logx::line("DIAG: view matrix unreadable - dwViewMatrix looks stale");
    }
    if (players.empty()) {
        logx::line("DIAG: entity list resolved but no player records - either no "
                   "enemies are alive, or the schema fields are stale");
    }
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
    if (!g_attached || !g_nav) return;
    if (!g_mem.alive()) {
        logx::line("target process went away, dropping attachment");
        g_glow.restoreAll();
        g_mem.detach();
        g_attached = false;
        return;
    }

    // Open the write handle only when a feature actually needs it, and only once.
    const bool wantWrite = g_cfg.glow || g_cfg.noFlash || g_cfg.skinChanger ||
                           g_cfg.aimMode == AimMode::Write ||
                           (g_cfg.aimbot && g_cfg.aimMode == AimMode::Write);
    if (wantWrite) g_mem.canWrite();

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
    st.elevated  = g_elevated;
    st.pid       = g_attached ? (unsigned long)g_mem.pid() : 0;
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

    // --------------------------------------------------------- elevation ----
    // --no-elevate exists for two real cases: launching from a shell that is
    // already elevated (a second UAC prompt would be redundant), and debugging
    // the overlay deliberately at Medium integrity.
    const bool noElevate = wcsstr(GetCommandLineW(), L"--no-elevate") != nullptr;

    if (!noElevate && !isElevated()) {
        logx::line("running at Medium integrity; CS2 runs at High - re-launching elevated");

        if (relaunchElevated()) {
            logx::line("elevated instance started, this one exits");
            return 0;
        }
        logx::line("elevation declined or unavailable - memory access will be refused");
    }
    g_elevated = isElevated();
    logx::line("integrity: %s", g_elevated ? "elevated" : "NOT elevated");

    // ------------------------------------------------------------- --verify --
    // Reports to muei_check.txt and opens it. Runs before the overlay exists so
    // it works even when no surface can be created.
    if (wcsstr(GetCommandLineW(), L"--verify") != nullptr) {
        loadOffsetOverrides(exeDirFile(L"offsets.cfg"));

        const std::wstring report = exeDirFile(L"muei_check.txt");
        const int rc = runSelfTest(report);

        ShellExecuteW(nullptr, L"open", L"notepad.exe", report.c_str(), nullptr,
                      SW_SHOWNORMAL);
        logx::line("self test finished, rc=%d, report=%ls", rc, report.c_str());
        return rc;
    }

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
    const DWORD surfacePid = g_attached ? g_mem.pid() : findGamePidByWindow();

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
                if (g_attached) g_overlay.refitToGame(g_mem.pid());
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
    g_mem.detach();
    g_cfg.save();
    g_overlay.shutdown();

    logx::line("=== muei shutdown ===");
    return 0;
}
