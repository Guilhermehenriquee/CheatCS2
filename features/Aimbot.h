// muei 無影 - aimbot.
//
// Target selection is angular, not pixel-based: the FOV the operator sets in the
// menu is a true cone, converted once per frame through the projected
// half-height. That keeps the behaviour identical at 1080p and 1440p.
//
// Two application paths:
//   Mouse  - converts the angle delta to counts through the in-game sensitivity
//            and emits relative motion. The OS sees a hand on the pad.
//   Write  - writes dwViewAngles directly. Snappier and does not fight the
//            game's own input accumulation, at the cost of being a memory write
//            the AC can see.
#pragma once

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Nav.h"

class Aimbot {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& frame);

    // Last chosen target's entity index, -1 when idle. Read by the menu so the
    // operator can see what the aimbot locked.
    int targetIndex() const { return targetIndex_; }

private:
    int      targetIndex_ = -1;
    bool     firing_      = false;
    uint64_t fireUntil_   = 0;

    void releaseFire();
    void applyMouse(const QAngle& delta, float smooth, const Config& cfg,
                    const Frame& frame);
    void applyWrite(const Nav& nav, const QAngle& current, const QAngle& delta,
                    float smooth);
};
