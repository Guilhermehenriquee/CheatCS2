#include "Aimbot.h"
#include "Input.h"
#include "Resolver.h"

#include <cmath>

namespace {

// CS2 m_yaw is 0.022 degrees per count at sensitivity 1.0. One count therefore
// rotates the view by sensitivity * 0.022 degrees.
constexpr float kDegPerCountAtSens1 = 0.022f;

// A single correction above this many counts is a teleport, not a flick.
constexpr long kMaxStepPerTick = 480;

} // namespace

void Aimbot::releaseFire() {
    if (!firing_) return;
    input::leftButton(false);
    firing_ = false;
}

void Aimbot::tick(const Nav& nav, const Config& cfg, const Frame& frame) {
    if (firing_ && GetTickCount64() > fireUntil_) releaseFire();

    targetIndex_ = -1;

    if (!cfg.aimbot || !frame.haveLocal || !frame.local.alive()) {
        releaseFire();
        return;
    }
    if (!frame.screen.valid) {
        releaseFire();
        return;
    }
    if (!input::keyDown(input::aimKeyToVk(cfg.aimKey))) {
        releaseFire();
        return;
    }

    const QAngle current = frame.viewAngles;
    if (!current.finite()) return;

    const float halfHeight = (float)frame.screen.h * 0.5f;
    if (halfHeight < 1.f) return;

    int    bestIndex = -1;
    float  bestFov   = cfg.aimFov;

    for (size_t i = 0; i < frame.players.size(); ++i) {
        const Player& pl = frame.players[i];
        if (pl.pawn == frame.local.pawn) continue;
        if (pl.team == frame.local.team) continue;
        if (!pl.alive()) continue;
        if (pl.distTo(frame.local) > cfg.aimMaxDist) continue;
        if (cfg.aimVisibleOnly && !pl.spotted) continue;

        Vec3 point = resolver::aimPoint(nav, pl, frame, cfg, cfg.aimBone);

        Vec2 sp;
        if (!frame.screen.project(point, sp)) continue;

        float dx = sp.x - frame.screenCenter.x;
        float dy = sp.y - frame.screenCenter.y;
        float px = std::sqrt(dx * dx + dy * dy);

        // Pixel radius back to an angle through the same projection the draw
        // path uses.
        float fovDeg = std::atan2f(px, halfHeight) * RAD2DEG;
        if (fovDeg > bestFov) continue;

        bestFov   = fovDeg;
        bestIndex = (int)i;
    }

    if (bestIndex < 0) {
        releaseFire();
        return;
    }

    targetIndex_ = frame.players[bestIndex].index;

    const Player& target = frame.players[bestIndex];
    Vec3 point = resolver::aimPoint(nav, target, frame, cfg, cfg.aimBone);

    QAngle want = vecToAngles(point - frame.local.eye);
    normalizeAngle(want);

    QAngle delta = angleSub(want, current);
    if (!std::isfinite(delta.yaw) || !std::isfinite(delta.pitch)) return;

    const float smooth = cfg.aimSmooth < 1.f ? 1.f : cfg.aimSmooth;

    if (cfg.aimMode == AimMode::Write) {
        applyWrite(nav, current, delta, smooth);
    } else {
        applyMouse(delta, smooth, cfg, frame);
    }

    if (cfg.aimAutoFire && !firing_) {
        input::leftButton(true, cfg.aimUseDriver);
        firing_    = true;
        fireUntil_ = GetTickCount64() + 40;
    }
}

void Aimbot::applyMouse(const QAngle& delta, float smooth, const Config& cfg,
                        const Frame& frame) {
    float stepPitch = delta.pitch / smooth;
    float stepYaw   = delta.yaw / smooth;

    // Sensitivity is treated as the user's real in-game value, so the correction
    // lands the same distance regardless of their setup.
    float sens = cfg.aimSens > 0.01f ? cfg.aimSens : 1.f;
    float degPerCount = kDegPerCountAtSens1 * sens;

    // Scale by the render height so a 4K overlay and a 1080p one feel identical.
    float scale = 1.f;
    if (frame.screen.h > 0) {
        constexpr float kBaseHalfHeight = 540.f;
        scale = ((float)frame.screen.h * 0.5f) / kBaseHalfHeight;
        scale = clampf(scale, 0.5f, 2.0f);
    }

    long dx = (long)std::lround(stepYaw   / degPerCount * scale);
    long dy = (long)std::lround(stepPitch / degPerCount * scale);
    dx = (long)clampf((float)dx, -(float)kMaxStepPerTick, (float)kMaxStepPerTick);
    dy = (long)clampf((float)dy, -(float)kMaxStepPerTick, (float)kMaxStepPerTick);

    if (dx == 0 && dy == 0) return;
    input::moveRelative(dx, dy);
}

void Aimbot::applyWrite(const Nav& nav, const QAngle& current, const QAngle& delta,
                        float smooth) {
    if (!offKnown(g_off.dwViewAngles)) return;

    QAngle next = current;
    next.pitch += delta.pitch / smooth;
    next.yaw   += delta.yaw   / smooth;
    normalizeAngle(next);

    // The write handle is opened lazily; a read-only backend simply never lands.
    uintptr_t addr = nav.at(g_off.dwViewAngles);
    if (addr) nav.mem().wr<QAngle>(addr, next);
}
