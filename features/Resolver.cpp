#include "Resolver.h"

#include <cmath>

namespace resolver {
namespace {

// How far above the feet the aim point can be, in units. Used to keep a lead
// from sliding off the player into thin air.
constexpr float kVolumeRadiusXY = 18.f;
constexpr float kVolumeMinZ     = -4.f;
constexpr float kVolumeMaxZ     = 76.f;

Vec3 clampToVolume(const Vec3& want, const Vec3& origin) {
    Vec3 local = want - origin;

    float r = std::sqrt(local.x * local.x + local.y * local.y);
    if (r > kVolumeRadiusXY && r > 1e-3f) {
        float s = kVolumeRadiusXY / r;
        local.x *= s;
        local.y *= s;
    }
    local.z = clampf(local.z, kVolumeMinZ, kVolumeMaxZ);
    return origin + local;
}

// Deterministic per-target jitter. Seeded off the entity index so it is stable
// across frames but differs between players; range is +-0.35 units, well under
// a head hitbox, so it never misses the shot it is aiming at.
Vec3 jitter(const Player& pl, const Vec3& at) {
    uint32_t seed = (uint32_t)(pl.index * 2654435761u);
    seed ^= seed >> 13;
    float jx = ((float)(seed & 0xFF) / 255.f - 0.5f) * 0.7f;
    seed = seed * 1664525u + 1013904223u;
    float jy = ((float)(seed & 0xFF) / 255.f - 0.5f) * 0.7f;
    return { at.x + jx, at.y + jy, at.z };
}

} // namespace

bool leading(const Config& cfg, const Player& pl) {
    if (!cfg.resolverOn) return false;
    if (cfg.resolverMode == ResolveMode::None) return false;
    if (pl.velocity.zero()) return false;
    return pl.velocity.len() > 1.f;
}

Vec3 aimPoint(const Nav& nav, const Player& pl, const Frame& frame,
              const Config& cfg, int bone) {
    Vec3 base{};
    bool haveBone = nav.bone(pl.pawn, bone, pl.origin, base);
    if (!haveBone) {
        // Degrade to the eye rather than dropping the target.
        base = pl.eye;
    }

    if (!cfg.resolverOn || cfg.resolverMode == ResolveMode::None) return base;

    // Flight time to the target from the local eye.
    Vec3 from = frame.haveLocal ? frame.local.eye : pl.eye;
    float dist = vdist(from, base);
    float speed = cfg.resolverBulletSpeed > 1.f ? cfg.resolverBulletSpeed : 780.f;

    float travel = dist / speed;
    float total  = travel + (cfg.resolverLatency > 0.f ? cfg.resolverLatency : 0.f);
    // Past ~0.75 s of lead the assumption is doing more harm than good.
    total = clampf(total, 0.f, 0.75f);

    Vec3 lead = base + pl.velocity * total;
    lead = clampToVolume(lead, pl.origin);

    if (cfg.resolverMode == ResolveMode::TravelJitter) {
        lead = jitter(pl, lead);
    }
    return lead;
}

} // namespace resolver
