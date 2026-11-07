#include "Triggerbot.h"
#include "Input.h"

#include <cmath>

void Triggerbot::release() {
    if (!firing_) return;
    if (GetTickCount64() - fireAt_ >= 35) {
        input::leftButton(false);
        firing_ = false;
    }
}

void Triggerbot::tick(const Nav& nav, const Config& cfg, const Frame& frame) {
    release();

    if (!cfg.triggerbot || !frame.haveLocal || !frame.local.alive()) {
        armed_ = false;
        return;
    }

    IMemory& mem = nav.mem();
    const uintptr_t localPawn = frame.local.pawn;

    uint32_t handle = mem.rd<uint32_t>(localPawn + g_off.m_iIDEntIndex);
    if (!handle) { armed_ = false; return; }

    uintptr_t ent = nav.entityFromHandle(handle);
    if (!ent || ent == localPawn) { armed_ = false; return; }

    int32_t health = mem.rd<int32_t>(ent + g_off.m_iHealth);
    uint8_t life   = mem.rd<uint8_t>(ent + g_off.m_lifeState);
    int     team   = (int)mem.rd<uint8_t>(ent + g_off.m_iTeamNum);

    if (health <= 0 || life != 0) { armed_ = false; return; }
    if (cfg.triggerTeamFlag && team == frame.local.team) { armed_ = false; return; }

    if (cfg.triggerHeadOnly) {
        // Only fire when the crosshair is within a few degrees of the head: the
        // crosshair handle says "something", this says "the head".
        Vec3 origin{}, head{};
        if (nav.readOrigin(ent, origin) && nav.bone(ent, Bone::HEAD, origin, head)) {
            QAngle want = vecToAngles(head - frame.local.eye);
            normalizeAngle(want);
            QAngle d = angleSub(want, frame.viewAngles);
            if (std::fabs(d.pitch) > 3.5f || std::fabs(d.yaw) > 3.5f) {
                armed_ = false;
                return;
            }
        }
    }

    if (!armed_) {
        armed_   = true;
        armedAt_ = GetTickCount64();
        return;
    }

    if (GetTickCount64() - armedAt_ < (uint64_t)cfg.triggerDelayMs) return;

    input::leftButton(true, cfg.triggerUseDriver);
    firing_  = true;
    fireAt_  = GetTickCount64();

    // Re-arm without re-waiting the delay so a held crosshair keeps firing.
    armedAt_ = GetTickCount64() - (uint64_t)cfg.triggerDelayMs;
}
