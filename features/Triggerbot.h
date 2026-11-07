// muei 無影 - triggerbot.
//
// The crosshair entity handle (m_iIDEntIndex) is written by the engine every
// tick and already accounts for the real trace, spread and wall occlusion. Using
// it means the trigger fires exactly when the game would have registered a hit,
// instead of guessing from screen geometry.
#pragma once

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Nav.h"

class Triggerbot {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& frame);

private:
    bool     armed_    = false;
    uint64_t armedAt_  = 0;
    bool     firing_   = false;
    uint64_t fireAt_   = 0;

    void release();
};
