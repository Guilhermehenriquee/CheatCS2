// muei 無影 - one scan per frame, shared by every feature.
//
// The overlay reads the world once per rendered frame and hands the snapshot to
// ESP, aimbot, triggerbot, resolver and glow. That keeps the read count flat no
// matter how many features are on, and it means every feature agrees about the
// same instant - which matters for aim: a target that ESP drew and the aimbot
// aimed at must be the same snapshot.
#pragma once

#include <vector>

#include "Entity.h"
#include "Math.h"

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

struct Frame {
    std::vector<Player> players;      // every live pawn, local included
    Player  local;
    bool    haveLocal = false;

    QAngle  viewAngles;
    ScreenCtx screen;
    Vec2    screenCenter{ 0.f, 0.f };

    // Filled by ESP only when the skeleton option is on; cleared each frame.
    std::vector<BoneSet> skeletons;

    // Convenience: index of the local player inside players, or -1.
    int localIndex() const {
        if (!haveLocal) return -1;
        for (size_t i = 0; i < players.size(); ++i) {
            if (players[i].pawn == local.pawn) return (int)i;
        }
        return -1;
    }
};
