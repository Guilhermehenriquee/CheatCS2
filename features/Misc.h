// muei 無影 - misc.
//
// Flash suppression: the effect is driven by a start timestamp and a duration on
// the pawn. Zeroing both skips the whiteout without touching the renderer.
#pragma once

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Nav.h"

class Misc {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& frame);

private:
    bool warnedWrite_ = false;
};
