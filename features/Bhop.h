// muei 無影 - bunny hop.
//
// Jump is held down for the whole airborne phase, not pulsed on the landing
// tick. The engine's own auto-jump buffer then rejumps on the exact frame of
// contact; a landing-tick pulse drops roughly every third hop because it races
// the frame the flag flips.
#pragma once

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Nav.h"

class Bhop {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& frame);

private:
    bool  pressed_  = false;
    float lastYaw_  = 0.f;
    void  release();
};
