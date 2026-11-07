// muei 無影 - entity model.
//
// A Player is a snapshot, not a live handle: Nav::readPlayer fills one per frame
// and every feature reads that copy. Nothing in the overlay re-reads a field
// mid-draw, which keeps the read count flat and makes the ESP, aimbot and
// triggerbot all agree on what the world looked like at the same instant.
#pragma once

#include <cstdint>
#include <cstring>

#include "Math.h"
#include "Offsets.h"

struct Player {
    uintptr_t pawn       = 0;
    uintptr_t node       = 0;
    uintptr_t controller = 0;

    Vec3    origin;                 // absolute world origin at the feet
    Vec3    eye;                    // origin + eye height
    Vec3    velocity;               // absolute velocity, zero when unknown
    QAngle  eyeAngles;
    QAngle  aimPunch;               // view punch, zero when the field is unknown

    int32_t  health    = 0;
    int32_t  maxHealth = 100;
    int32_t  team      = 0;
    uint8_t  lifeState = 1;
    uint32_t flags     = 0;
    int32_t  index     = -1;
    int32_t  shotsFired = 0;

    bool dormant = true;
    bool spotted = false;           // spotted by the local player this tick

    char     name[32] = { 0 };

    bool alive() const { return health > 0 && lifeState == 0; }
    bool onGround() const { return (flags & 1u) != 0; }
    bool enemyOf(const Player& local) const { return team != local.team; }
    float distTo(const Player& other) const { return vdist(origin, other.origin); }
};

// Skeleton segments, in pairs of bone indices. Kept in one place so ESP and the
// resolver cannot disagree about what a skeleton is.
struct BonePair { int a, b; };

inline const BonePair kSkeletonPairs[] = {
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
inline constexpr int kSkeletonPairCount =
    (int)(sizeof(kSkeletonPairs) / sizeof(kSkeletonPairs[0]));

// Per-player cached bone positions, filled on demand by ESP.
struct BoneSet {
    Vec3 pts[Bone::MAX_BONES];
    bool ok[Bone::MAX_BONES] = { false };
};
