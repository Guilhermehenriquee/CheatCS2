// muei 無影 - aim-point resolver.
//
// "Resolver" here means resolving *where to put the crosshair*, not undoing an
// animation state. Two real effects are compensated:
//
//   travel   - the bullet is not instant. At 780 u/s a target 1000 units away
//              is reached 1.28 s later, by which time a running player has moved
//              several units. Leading by velocity * flight time is the whole
//              fix.
//   latency  - the snapshot itself is one to three ticks old. A fixed offset
//              (config, default 12 ms) covers input + net + render.
//
// The result is clamped back onto the player's bounding volume, so a bad
// velocity read can never make the aimbot aim at empty space behind a wall.
#pragma once

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Math.h"
#include "../sdk/Nav.h"

namespace resolver {

// Resolve the world-space point the aimbot should target for `pl`.
// bone is the preferred bone index; falls back to the eye position.
Vec3 aimPoint(const Nav& nav, const Player& pl, const Frame& frame,
              const Config& cfg, int bone);

// True while the resolver is using a lead (i.e. it is actually doing work).
bool leading(const Config& cfg, const Player& pl);

} // namespace resolver
