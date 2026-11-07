// muei 無影 - configuration and persistence.
//
// One struct, one JSON file, saved next to the executable. Loading is tolerant:
// a missing field keeps its default, an unknown field is ignored, and a corrupt
// file is renamed rather than overwritten so the operator can inspect what broke.
//
// Every option here is wired to something real in features/ or ui/. A toggle
// that does nothing is worse than no toggle: it makes the menu lie.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "../sdk/Offsets.h"

enum class AimMode : int { Mouse = 0, Write = 1 };
enum class AimKey  : int { RMB = 0, LMB = 1, Shift = 2, Alt = 3, XButton1 = 4 };

// Which memory backend to try. Auto walks kernel -> dma -> user and keeps the
// first that attaches; the explicit values force one for testing.
enum class Backend : int { Auto = 0, User = 1, Kernel = 2, DMA = 3 };

// Aim-point resolution strategy.
enum class ResolveMode : int {
    None = 0,        // straight bone position
    Travel = 1,      // lead the target by bullet travel time
    TravelJitter = 2 // travel lead plus a small deterministic offset
};

struct Config {
    int version = 2;

    // ------------------------------------------------------------------- ESP --
    bool  esp           = true;
    bool  espBox        = true;
    bool  espHealth     = true;
    bool  espName       = false;
    bool  espSkeleton   = false;
    bool  espDistance   = false;
    bool  espFilled     = false;
    bool  espSnaplines  = false;
    float espMaxDist    = 4000.f;
    int   espTeamFilter = 1;      // 0 = all, 1 = enemy only
    float espThickness  = 1.5f;
    float espEnemyCol[4] = { 1.00f, 0.25f, 0.25f, 0.90f };
    float espTeamCol[4]  = { 0.30f, 0.65f, 1.00f, 0.85f };
    float espSkelCol[4]  = { 1.00f, 1.00f, 1.00f, 0.70f };

    // ----------------------------------------------------------------- Bhop --
    bool bhop           = false;
    bool bhopAutoStrafe = false;

    // ------------------------------------------------------------ Triggerbot --
    bool triggerbot       = false;
    int  triggerDelayMs   = 30;
    int  triggerHoldMs    = 35;
    bool triggerTeamFlag  = true;
    bool triggerHeadOnly  = false;
    bool triggerUseDriver = false;

    // ---------------------------------------------------------------- Aimbot --
    bool    aimbot         = false;
    bool    aimVisibleOnly = true;
    AimMode aimMode        = AimMode::Mouse;
    AimKey  aimKey         = AimKey::RMB;
    float   aimFov         = 15.f;
    float   aimSmooth      = 2.0f;
    int     aimBone        = Bone::HEAD;
    bool    aimAutoFire    = false;
    float   aimMaxDist     = 3000.f;
    float   aimSens        = 2.5f;
    bool    aimUseDriver   = false;

    // -------------------------------------------------------------- Resolver --
    bool        resolverOn          = true;
    ResolveMode resolverMode        = ResolveMode::Travel;
    float       resolverBulletSpeed = 780.f;    // units/s, AK-47 baseline
    float       resolverLatency     = 0.012f;   // seconds of assumed round trip

    // ------------------------------------------------------------------ RCS --
    bool  rcs        = false;
    float rcsScaleX  = 1.0f;
    float rcsScaleY  = 1.0f;
    int   rcsBullets = 1;

    // ----------------------------------------------------------------- Glow --
    bool  glow         = false;
    int   glowType     = 3;
    float glowEnemy[4] = { 1.00f, 0.15f, 0.15f, 1.f };
    float glowTeam[4]  = { 0.20f, 0.55f, 1.00f, 1.f };

    // ----------------------------------------------------------- SkinChanger --
    bool  skinChanger  = false;
    int   skinPaintKit = 180;
    int   skinSeed     = 0;
    float skinWear     = 0.00001f;
    int   skinStatTrak = 100;
    int   skinQuality  = 2;
    char  skinName[64] = "muei";
    bool  skinSetMask  = true;

    // ----------------------------------------------------------------- Misc --
    bool  noFlash   = false;
    int   menuKey   = VK_INSERT;
    int   panicKey  = VK_END;
    int   backend   = (int)Backend::Auto;
    float accentCol[4] = { 0.00f, 0.85f, 1.00f, 1.00f };

    bool load();
    bool save() const;
    void reset();

    static std::wstring defaultPath();
};
