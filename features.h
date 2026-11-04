#pragma once
#include <windows.h>
#include <cmath>
#include <vector>
#include <cstdint>
#include <algorithm>

#include "proc.h"
#include "offsets.h"
#include "math.h"
#include "nav.h"

// -----------------------------------------------------------------------------
// Config - every tunable the overlay exposes. Defaults are the spec's defaults:
// ESP on, everything else off, team filter enemy-only.
// -----------------------------------------------------------------------------

enum class AimMode : int { Mouse = 0, Write = 1 };
enum class AimKey  : int { RMB = 0, LMB = 1, Shift = 2, Alt = 3, XButton1 = 4 };

struct Config {
    // ---- ESP ----
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
    float espColor[4]   = { 0.f, 1.f, 0.f, 0.85f };
    float espEnemyCol[4]= { 1.f, 0.25f, 0.25f, 0.90f };
    float espTeamCol[4] = { 0.30f, 0.65f, 1.f, 0.85f };

    // ---- Bhop ----
    bool bhop            = false;
    bool bhopAutoStrafe  = false;

    // ---- Triggerbot ----
    bool  triggerbot      = false;
    int   triggerDelayMs  = 30;
    bool  triggerHoldFire = false;
    bool  triggerTeamFlag = true;   // enemy only
    bool  triggerHeadOnly = false;

    // ---- Aimbot ----
    bool    aimbot         = false;
    bool    aimVisibleOnly = true;
    AimMode aimMode        = AimMode::Mouse;
    AimKey  aimKey         = AimKey::RMB;
    float   aimFov         = 15.f;
    float   aimSmooth      = 2.0f;
    int     aimBone        = Bone::HEAD;
    bool    aimAutoFire    = false;
    float   aimMaxDist     = 3000.f;

    // ---- Glow ----
    bool  glow       = false;
    int   glowType   = 3;
    float glowEnemy[4] = { 1.f, 0.15f, 0.15f, 1.f };
    float glowTeam[4]  = { 0.20f, 0.55f, 1.f, 1.f };

    // ---- Misc ----
    bool  noFlash      = false;
    bool  holdToRun    = false;
    int   menuKey      = VK_INSERT;
};

// -----------------------------------------------------------------------------
// Key state helpers - polled, never hooked.
// -----------------------------------------------------------------------------
inline bool keyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

inline int aimKeyToVk(AimKey k) {
    switch (k) {
        case AimKey::RMB:       return VK_RBUTTON;
        case AimKey::LMB:       return VK_LBUTTON;
        case AimKey::Shift:     return VK_SHIFT;
        case AimKey::Alt:       return VK_MENU;
        case AimKey::XButton1:  return VK_XBUTTON1;
    }
    return VK_RBUTTON;
}

// -----------------------------------------------------------------------------
// Mouse injection - relative move so the physical cursor never teleports and
// the movement is indistinguishable from a hand on the pad.
// -----------------------------------------------------------------------------
inline void mouseMoveRelative(long dx, long dy) {
    if (dx == 0 && dy == 0) return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

inline void mouseButton(bool left, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    if (left) {
        in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else {
        in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    }
    SendInput(1, &in, sizeof(INPUT));
}

// -----------------------------------------------------------------------------
// WorldToScreen helper with a fixed matrix cached once per frame.
// -----------------------------------------------------------------------------
struct ScreenCtx {
    float m[16]{};
    int   w = 0;
    int   h = 0;
    bool  valid = false;

    bool project(const Vec3& p, Vec2& out) const {
        if (!valid) return false;
        return worldToScreen(m, p, out, w, h);
    }
};

// -----------------------------------------------------------------------------
// Per-frame target scan shared by ESP, aimbot and triggerbot.
// One scan per frame keeps the read count flat regardless of feature count.
// -----------------------------------------------------------------------------
struct Frame {
    std::vector<Player> players;
    Player      local;
    bool        haveLocal = false;
    QAngle      viewAngles;
    ScreenCtx   screen;
    Vec2        screenCenter{ 0.f, 0.f };

    // Skeleton bone cache per player index, filled on demand by ESP.
    struct BoneSet {
        Vec3 pts[24]{};
        bool ok[24]{};
    };
    std::vector<BoneSet> skeletons;   // parallel to players
};

// Bones drawn as connected segments. Pairs index into BoneSet::pts via the
// order below; keep the array in sync with the draw loop in main.cpp.
static const int kSkeletonPairs[][2] = {
    { Bone::HEAD,       Bone::NECK },
    { Bone::NECK,       Bone::SPINE },
    { Bone::SPINE,      Bone::CHEST },
    { Bone::CHEST,      Bone::PELVIS },
    { Bone::CHEST,      Bone::L_SHOULDER },
    { Bone::L_SHOULDER, Bone::L_ELBOW },
    { Bone::L_ELBOW,    Bone::L_HAND },
    { Bone::CHEST,      Bone::R_SHOULDER },
    { Bone::R_SHOULDER, Bone::R_ELBOW },
    { Bone::R_ELBOW,    Bone::R_HAND },
    { Bone::PELVIS,     Bone::L_HIP },
    { Bone::L_HIP,      Bone::L_KNEE },
    { Bone::L_KNEE,     Bone::L_FOOT },
    { Bone::PELVIS,     Bone::R_HIP },
    { Bone::R_HIP,      Bone::R_KNEE },
    { Bone::R_KNEE,     Bone::R_FOOT },
};

// Load the bone positions a skeleton needs for one player.
inline void loadSkeleton(const Nav& nav, const Player& pl, Frame::BoneSet& bs) {
    bs = Frame::BoneSet{};
    for (const auto& pair : kSkeletonPairs) {
        for (int k = 0; k < 2; ++k) {
            int idx = pair[k];
            if (bs.ok[idx]) continue;
            Vec3 wp;
            if (nav.bone(pl.pawn, idx, pl.origin, wp)) {
                bs.pts[idx] = wp;
                bs.ok[idx] = true;
            }
        }
    }
}

// Head position for aiming, with a fallback to eye height when the bone read
// fails so the aimbot degrades instead of dropping the target.
inline Vec3 targetPoint(const Nav& nav, const Player& pl, int bone, bool& fromBone) {
    Vec3 wp;
    if (nav.bone(pl.pawn, bone, pl.origin, wp)) {
        fromBone = true;
        return wp;
    }
    fromBone = false;
    return { pl.origin.x, pl.origin.y, pl.origin.z + Off::EYE_OFFSET_Z - 4.f };
}

// -----------------------------------------------------------------------------
// Aimbot
//
// Target: nearest to crosshair inside FOV, optionally limited to spots the
// local player has actually spotted. Writes either the view-angle vector the
// engine samples (fov check + write path) or a relative mouse move (default).
//
// The mouse path is the smoother of the two: it converts the angle delta to
// pixels through the game's sensitivity and FOV, so the correction is applied
// by the same code path that handles a human's mouse input.
// -----------------------------------------------------------------------------
class Aimbot {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& f) {
        cooldown();

        if (!cfg.aimbot || !f.haveLocal || !f.local.alive()) { held_ = false; return; }
        if (!keyDown(aimKeyToVk(cfg.aimKey)))      { held_ = false; return; }

        QAngle cur = f.viewAngles;
        if (!cur.finite()) return;

        int   bestIdx = -1;
        float bestFov = cfg.aimFov;
        float bestDist = 1e9f;

        for (size_t i = 0; i < f.players.size(); ++i) {
            const Player& pl = f.players[i];
            if (pl.team == f.local.team) continue;
            if (!pl.alive()) continue;

            float dist = vlen(pl.origin - f.local.origin);
            if (dist > cfg.aimMaxDist) continue;

            Vec2 sp;
            bool fromBone = false;
            Vec3 aimPt = targetPoint(nav, pl, cfg.aimBone, fromBone);
            if (!f.screen.project(aimPt, sp)) continue;

            // FOV is measured on screen, in the 2D radius around the crosshair,
            // converted to degrees through the projected half-height.
            float dx = sp.x - f.screenCenter.x;
            float dy = sp.y - f.screenCenter.y;
            float px = std::sqrt(dx * dx + dy * dy);
            float radius = (float)f.screen.h * 0.5f;
            float fovDeg = std::atan2f(px, radius) * RAD2DEG;
            if (fovDeg > bestFov) continue;
            if (cfg.aimVisibleOnly && !pl.spotted) continue;

            bestFov  = fovDeg;
            bestIdx  = (int)i;
            bestDist = dist;
        }

        (void)bestDist;
        if (bestIdx < 0) { held_ = false; return; }

        const Player& tgt = f.players[bestIdx];
        bool fromBone = false;
        Vec3 aimPt = targetPoint(nav, tgt, cfg.aimBone, fromBone);

        QAngle want = vecToAngles(aimPt - f.local.eye);
        normalizeAngle(want);

        QAngle delta = angleSub(want, cur);
        if (!std::isfinite(delta.yaw) || !std::isfinite(delta.pitch)) return;

        float smooth = cfg.aimSmooth < 1.f ? 1.f : cfg.aimSmooth;

        if (cfg.aimMode == AimMode::Write) {
            applyWrite(nav, cur, delta, smooth);
        } else {
            applyMouse(delta, smooth, f);
        }

        if (cfg.aimAutoFire && !held_) {
            mouseButton(true, true);
            held_ = true;
            fireUntil_ = GetTickCount64() + 40;
        }
    }

private:
    bool     held_ = false;
    uint64_t fireUntil_ = 0;

    void cooldown() {
        if (held_ && GetTickCount64() > fireUntil_) {
            mouseButton(true, false);
            held_ = false;
        }
    }

    // Relative mouse move: degrees -> pixels.
    // 360 degrees of yaw maps to (sensitivity * m_yaw)-ish pixels; CS2 default
    // m_yaw is 0.022 deg per count with sensitivity 1.0. We read the player's
    // sensitivity indirectly through a fixed 2.5 baseline and scale by smooth.
    static void applyMouse(const QAngle& delta, float smooth, const Frame& f) {
        constexpr float kDegPerCount = 0.022f;
        constexpr double kSens        = 2.5;

        float stepPitch = delta.pitch / smooth;
        float stepYaw   = delta.yaw   / smooth;

        // Scale by the projected FOV so long-distance corrections feel identical
        // to close ones.
        float scale = 1.0f;
        if (f.screen.valid && f.screen.h > 0) {
            constexpr float kBaseHalfHeight = 540.f;
            scale = (float)f.screen.h * 0.5f / kBaseHalfHeight;
            if (scale < 0.5f) scale = 0.5f;
            if (scale > 2.0f) scale = 2.0f;
        }

        long dx = (long)std::lround(stepYaw   / kDegPerCount / (float)kSens * scale);
        long dy = (long)std::lround(stepPitch / kDegPerCount / (float)kSens * scale);

        if (dx >  500) dx =  500;
        if (dx < -500) dx = -500;
        if (dy >  500) dy =  500;
        if (dy < -500) dy = -500;

        if (dx == 0 && dy == 0) return;
        mouseMoveRelative(dx, dy);
    }

    // Write the corrected angle vector the engine samples.
    static void applyWrite(const Nav& nav, const QAngle& cur,
                           const QAngle& delta, float smooth) {
        if (!nav.proc().ensureWrite()) return;

        QAngle next = cur;
        next.pitch += delta.pitch / smooth;
        next.yaw   += delta.yaw   / smooth;
        normalizeAngle(next);

        uintptr_t addr = nav.at(Off::dwViewAngles);
        if (addr) nav.proc().wr<QAngle>(addr, next);
    }
};

// -----------------------------------------------------------------------------
// Triggerbot
//
// Reads the crosshair entity handle straight off the local pawn - that handle is
// written by the engine every tick and already accounts for the actual trace and
// spread. Resolve it, verify team and health, then click after the configured
// delay with the button held for a short burst.
// -----------------------------------------------------------------------------
class Triggerbot {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& f) {
        releaseIfHeld();

        if (!cfg.triggerbot || !f.haveLocal || !f.local.alive()) { armed_ = false; return; }

        uintptr_t localPawn = f.local.pawn;
        uint32_t handle = nav.proc().rd<uint32_t>(localPawn + Off::m_iIDEntIndex);
        if (!handle) { armed_ = false; return; }

        uintptr_t ent = nav.entityFromHandle(handle);
        if (!ent) { armed_ = false; return; }
        if (ent == localPawn) { armed_ = false; return; }

        int health = nav.proc().rd<int32_t>(ent + Off::m_iHealth);
        uint8_t life = nav.proc().rd<uint8_t>(ent + Off::m_lifeState);
        int team = (int)nav.proc().rd<uint8_t>(ent + Off::m_iTeamNum);
        if (health <= 0 || life != 0) { armed_ = false; return; }
        if (cfg.triggerTeamFlag && team == f.local.team) { armed_ = false; return; }

        if (cfg.triggerHeadOnly) {
            // Crosshair on the head: compare the hit point to the head bone.
            Vec3 origin;
            Vec3 head;
            if (nav.readOrigin(ent, origin) && nav.bone(ent, Bone::HEAD, origin, head)) {
                QAngle want = vecToAngles(head - f.local.eye);
                normalizeAngle(want);
                QAngle d = angleSub(want, f.viewAngles);
                if (std::fabs(d.pitch) > 3.5f || std::fabs(d.yaw) > 3.5f) {
                    armed_ = false;
                    return;
                }
            }
        }

        if (!armed_) {
            armed_  = true;
            armedAt_ = GetTickCount64();
            return;
        }

        if (GetTickCount64() - armedAt_ < (uint64_t)cfg.triggerDelayMs) return;

        mouseButton(true, true);
        held_    = true;
        fireAt_  = GetTickCount64();
        armedAt_ = GetTickCount64() - cfg.triggerDelayMs;  // keep re-arming without delay
    }

private:
    bool     armed_  = false;
    uint64_t armedAt_ = 0;
    bool     held_   = false;
    uint64_t fireAt_ = 0;

    void releaseIfHeld() {
        if (!held_) return;
        uint64_t holdFor = 35;
        if (GetTickCount64() - fireAt_ >= holdFor) {
            mouseButton(true, false);
            held_ = false;
        }
    }
};

// -----------------------------------------------------------------------------
// Bhop - jump the instant the player lands. m_fFlags bit 0 is FL_ONGROUND.
// The key is held down across the whole airborne phase so the engine's own
// auto-jump buffer takes over on landing; a press/release pulse on the landing
// tick alone is what the old build did and it drops roughly every third hop.
// -----------------------------------------------------------------------------
class Bhop {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& f) {
        if (!cfg.bhop || !f.haveLocal || !f.local.alive()) {
            release();
            return;
        }

        bool onGround = f.local.onGround();
        if (onGround) {
            if (!pressed_) {
                INPUT in{};
                in.type = INPUT_KEYBOARD;
                in.ki.wVk = VK_SPACE;
                SendInput(1, &in, sizeof(INPUT));
                pressed_ = true;
            }
        } else {
            release();
        }

        if (cfg.bhopAutoStrafe) {
            // Strafe assist: hold the turn key in the direction of the current
            // yaw delta so the velocity vector keeps re-projecting forward.
            QAngle v = nav.viewAngles();
            static float lastYaw = 0.f;
            float d = v.yaw - lastYaw;
            lastYaw = v.yaw;
            if (d > 180.f)  d -= 360.f;
            if (d < -180.f) d += 360.f;

            INPUT mv{};
            mv.type = INPUT_MOUSE;
            mv.mi.dx = d > 0.6f ? 6 : (d < -0.6f ? -6 : 0);
            mv.mi.dwFlags = MOUSEEVENTF_MOVE;
            if (mv.mi.dx) SendInput(1, &mv, sizeof(INPUT));
        }
    }

private:
    bool pressed_ = false;

    void release() {
        if (!pressed_) return;
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = VK_SPACE;
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
        pressed_ = false;
    }
};

// -----------------------------------------------------------------------------
// Glow - writes CGlowProperty on each player pawn.
//
// CGlowProperty is inline inside C_BaseModelEntity (m_Glow at 0xDE8), not a
// pointer, which is why the old build's write path never stuck: it was
// dereferencing a struct as if it were a pointer.
//
// Written per entity: m_iGlowType (configurable, 3 = always), m_glowColorOverride
// = team colour, m_bEligibleForScreenHighlight = true. Values are restored when
// the feature is switched off so the game does not keep glowing after the
// overlay closes.
// -----------------------------------------------------------------------------
class Glow {
public:
    ~Glow() { dropAll(); }

    void tick(const Nav& nav, const Config& cfg, const Frame& f) {
        proc_ = &nav.proc();

        if (!cfg.glow) { dropAll(); return; }

        Proc& p = nav.proc();
        if (!p.ensureWrite()) return;
        if (!f.haveLocal) return;

        for (const Player& pl : f.players) {
            if (pl.team == f.local.team) continue;

            uintptr_t glow = pl.pawn + Off::m_Glow;
            uint32_t color = packColor(pl.team == 3 ? cfg.glowTeam : cfg.glowEnemy);

            remember(pl.pawn, glow, color, cfg.glowType);
        }

        // Restore everything that stopped glowing this tick (died, went dormant,
        // left the slot, or the feature was filtered out).
        for (size_t i = 0; i < tracked_.size();) {
            Tracked& t = tracked_[i];
            if (t.activeThisFrame) { t.activeThisFrame = false; ++i; continue; }
            p.wr<int32_t>(t.glowAddr + Off::m_iGlowType, t.oldType);
            p.wr<uint32_t>(t.glowAddr + Off::m_glowColorOverride, t.oldColor);
            p.wr<uint8_t>(t.glowAddr + Off::m_bEligibleForScreenHighlight, t.oldEligible);
            tracked_.erase(tracked_.begin() + (ptrdiff_t)i);
        }
    }

    void dropAll() {
        Proc* p = proc_;
        if (p && p->hw) {
            for (Tracked& t : tracked_) {
                p->wr<int32_t>(t.glowAddr + Off::m_iGlowType, t.oldType);
                p->wr<uint32_t>(t.glowAddr + Off::m_glowColorOverride, t.oldColor);
                p->wr<uint8_t>(t.glowAddr + Off::m_bEligibleForScreenHighlight, t.oldEligible);
            }
        }
        tracked_.clear();
    }

private:
    struct Tracked {
        uintptr_t pawn = 0;
        uintptr_t glowAddr = 0;
        int32_t   oldType = 0;
        uint32_t  oldColor = 0;
        uint8_t   oldEligible = 0;
        bool      activeThisFrame = true;
    };
    std::vector<Tracked> tracked_;
    Proc* proc_ = nullptr;

    static uint32_t packColor(const float c[4]) {
        uint8_t a = (uint8_t)(clampf(c[3], 0.f, 1.f) * 255.f);
        uint8_t r = (uint8_t)(clampf(c[0], 0.f, 1.f) * 255.f);
        uint8_t g = (uint8_t)(clampf(c[1], 0.f, 1.f) * 255.f);
        uint8_t b = (uint8_t)(clampf(c[2], 0.f, 1.f) * 255.f);
        return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }

    void remember(uintptr_t pawn, uintptr_t glowAddr, uint32_t color, int type) {
        Proc* p = proc_;
        if (!p) return;

        for (Tracked& t : tracked_) {
            if (t.pawn != pawn) continue;
            t.activeThisFrame = true;
            p->wr<int32_t>(glowAddr + Off::m_iGlowType, type);
            p->wr<uint32_t>(glowAddr + Off::m_glowColorOverride, color);
            p->wr<uint8_t>(glowAddr + Off::m_bEligibleForScreenHighlight, 1);
            return;
        }

        Tracked t;
        t.pawn       = pawn;
        t.glowAddr   = glowAddr;
        t.oldType    = p->rd<int32_t>(glowAddr + Off::m_iGlowType);
        t.oldColor   = p->rd<uint32_t>(glowAddr + Off::m_glowColorOverride);
        t.oldEligible= p->rd<uint8_t>(glowAddr + Off::m_bEligibleForScreenHighlight);
        t.activeThisFrame = true;

        p->wr<int32_t>(glowAddr + Off::m_iGlowType, type);
        p->wr<uint32_t>(glowAddr + Off::m_glowColorOverride, color);
        p->wr<uint8_t>(glowAddr + Off::m_bEligibleForScreenHighlight, 1);

        tracked_.push_back(t);
    }
};

// -----------------------------------------------------------------------------
// Misc - flash suppression. Writes m_flFlashBangTime into the past so the
// flashbang effect is skipped while the feature is on.
// -----------------------------------------------------------------------------
class Misc {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& f) {
        if (!cfg.noFlash) return;
        if (!f.haveLocal || !nav.proc().ensureWrite()) return;

        float flash = nav.proc().rd<float>(f.local.pawn + Off::m_flFlashBangTime);
        if (flash > 0.f) {
            nav.proc().wr<float>(f.local.pawn + Off::m_flFlashBangTime, 0.f);
            nav.proc().wr<float>(f.local.pawn + Off::m_flFlashDuration, 0.f);
        }
    }
};
