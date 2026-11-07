#include "Glow.h"

#include <cmath>

uint32_t Glow::packColor(const float c[4]) {
    uint8_t a = (uint8_t)(clampf(c[3], 0.f, 1.f) * 255.f);
    uint8_t r = (uint8_t)(clampf(c[0], 0.f, 1.f) * 255.f);
    uint8_t g = (uint8_t)(clampf(c[1], 0.f, 1.f) * 255.f);
    uint8_t b = (uint8_t)(clampf(c[2], 0.f, 1.f) * 255.f);
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void Glow::apply(Tracked& t, uint32_t color, int type) {
    if (!mem_) return;
    mem_->wr<int32_t>(t.glowAddr + g_off.m_iGlowType, type);
    mem_->wr<uint32_t>(t.glowAddr + g_off.m_glowColorOverride, color);
    mem_->wr<uint8_t>(t.glowAddr + g_off.m_bEligibleForScreenHighlight, 1);
}

void Glow::remember(IMemory& mem, uintptr_t pawn, uintptr_t glowAddr,
                    uint32_t color, int type) {
    for (Tracked& t : tracked_) {
        if (t.pawn != pawn) continue;
        t.touchedThisFrame = true;
        apply(t, color, type);
        return;
    }

    Tracked t;
    t.pawn        = pawn;
    t.glowAddr    = glowAddr;
    t.oldType     = mem.rd<int32_t>(glowAddr + g_off.m_iGlowType);
    t.oldColor    = mem.rd<uint32_t>(glowAddr + g_off.m_glowColorOverride);
    t.oldEligible = mem.rd<uint8_t>(glowAddr + g_off.m_bEligibleForScreenHighlight);
    t.touchedThisFrame = true;

    apply(t, color, type);
    tracked_.push_back(t);
}

void Glow::restoreAll() {
    if (mem_) {
        for (Tracked& t : tracked_) {
            mem_->wr<int32_t>(t.glowAddr + g_off.m_iGlowType, t.oldType);
            mem_->wr<uint32_t>(t.glowAddr + g_off.m_glowColorOverride, t.oldColor);
            mem_->wr<uint8_t>(t.glowAddr + g_off.m_bEligibleForScreenHighlight, t.oldEligible);
        }
    }
    tracked_.clear();
}

void Glow::tick(const Nav& nav, const Config& cfg, const Frame& frame) {
    IMemory& mem = nav.mem();
    mem_ = &mem;

    if (!cfg.glow) {
        restoreAll();
        return;
    }

    if (!mem.canWrite()) return;
    if (!frame.haveLocal || !frame.screen.valid) return;

    for (const Player& pl : frame.players) {
        if (pl.pawn == frame.local.pawn) continue;
        if (pl.team == frame.local.team) continue;
        if (!pl.alive()) continue;

        uintptr_t glowAddr = pl.pawn + g_off.m_Glow;
        uint32_t color = packColor(cfg.glowEnemy);
        remember(mem, pl.pawn, glowAddr, color, cfg.glowType);
    }

    // Anything not touched this frame is restored and forgotten: it died, went
    // dormant, or left the list.
    for (size_t i = 0; i < tracked_.size();) {
        Tracked& t = tracked_[i];
        if (t.touchedThisFrame) {
            t.touchedThisFrame = false;
            ++i;
            continue;
        }
        mem.wr<int32_t>(t.glowAddr + g_off.m_iGlowType, t.oldType);
        mem.wr<uint32_t>(t.glowAddr + g_off.m_glowColorOverride, t.oldColor);
        mem.wr<uint8_t>(t.glowAddr + g_off.m_bEligibleForScreenHighlight, t.oldEligible);
        tracked_.erase(tracked_.begin() + (ptrdiff_t)i);
    }
}
