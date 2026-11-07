#include "RCS.h"
#include "Input.h"

#include "../core/Log.h"

#include <cmath>

namespace {

// CS2 m_yaw: degrees of rotation per mouse count at sensitivity 1.0.
constexpr float kDegPerCountAtSens1 = 0.022f;

// A single compensating step above this is a snap, not a correction.
constexpr float kMaxStep = 400.f;

} // namespace

bool RCS::available() {
    return offKnown(g_off.m_pCameraServices) && offKnown(g_off.m_vecCsViewPunchAngle);
}

void RCS::tick(const Nav& nav, const Config& cfg, const Frame& frame) {
    (void)nav;

    if (!cfg.rcs) {
        lastPunch_ = QAngle{};
        active_ = false;
        return;
    }

    if (!frame.haveLocal || !frame.local.alive()) {
        lastPunch_ = QAngle{};
        active_ = false;
        return;
    }

    if (!available()) {
        if (!warned_) {
            warned_ = true;
            logx::line("RCS: m_pCameraServices/m_vecCsViewPunchAngle unknown - "
                       "supply them in offsets.cfg to enable recoil control");
        }
        return;
    }

    // Nothing to compensate before the first shot of a burst.
    if (frame.local.shotsFired <= 0) {
        lastPunch_ = QAngle{};
        active_ = false;
        return;
    }

    // m_vecCsViewPunchAngle is the offset already applied to the camera for this
    // tick, so it is used as-is. (The CS:GO era doubled m_aimPunchAngle because
    // that field stored half the applied value; this one does not.)
    QAngle punch = frame.local.aimPunch;

    QAngle delta;
    delta.pitch = punch.pitch - lastPunch_.pitch;
    delta.yaw   = punch.yaw   - lastPunch_.yaw;
    lastPunch_  = punch;

    if (!std::isfinite(delta.pitch) || !std::isfinite(delta.yaw)) return;
    if (std::fabs(delta.pitch) < 0.01f && std::fabs(delta.yaw) < 0.01f) {
        active_ = true;
        return;
    }

    float sens = cfg.aimSens > 0.01f ? cfg.aimSens : 1.f;
    float degPerCount = kDegPerCountAtSens1 * sens;

    // Cancel the punch: move opposite to how the view moved.
    float dx = -delta.yaw   / degPerCount * cfg.rcsScaleX;
    float dy = -delta.pitch / degPerCount * cfg.rcsScaleY;

    dx = clampf(dx, -kMaxStep, kMaxStep);
    dy = clampf(dy, -kMaxStep, kMaxStep);

    long ix = (long)std::lround(dx);
    long iy = (long)std::lround(dy);

    if (ix || iy) input::moveRelative(ix, iy);
    active_ = true;
}
