// muei 無影 - recoil control.
//
// The engine adds a view punch to the camera while firing. Compensating means
// moving the mouse by the *negative* of the punch delta, converted from degrees
// to counts through the player's sensitivity.
//
// The punch is sampled from the local pawn each frame, so compensation follows
// the actual spray pattern of whatever weapon is in hand - no per-weapon tables.
//
// Requires m_pAimPunchServices + m_aimPunchAngle to be known. When they are not,
// the feature reports itself unavailable instead of writing to a guessed offset.
#pragma once

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Nav.h"

class RCS {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& frame);

    // True when the offsets the feature needs are present in g_off.
    static bool available();

private:
    QAngle lastPunch_{};
    bool   active_ = false;
    bool   warned_ = false;
};
