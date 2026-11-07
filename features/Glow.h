// muei 無影 - glow.
//
// CGlowProperty is inline inside C_BaseModelEntity (m_Glow), not a pointer. The
// write path therefore targets pawn + m_Glow + field. Every modified value is
// remembered and restored when the entity stops glowing, so switching the
// feature off leaves the game exactly as it was found.
#pragma once

#include <cstdint>
#include <vector>

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Nav.h"

class Glow {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& frame);

    // Restore everything that is currently modified. Called on shutdown and
    // whenever the backend is dropped.
    void restoreAll();

private:
    struct Tracked {
        uintptr_t pawn       = 0;
        uintptr_t glowAddr   = 0;
        int32_t   oldType    = 0;
        uint32_t  oldColor   = 0;
        uint8_t   oldEligible= 0;
        bool      touchedThisFrame = false;
    };

    std::vector<Tracked> tracked_;
    IMemory*             mem_ = nullptr;

    static uint32_t packColor(const float c[4]);
    void apply(Tracked& t, uint32_t color, int type);
    void remember(IMemory& mem, uintptr_t pawn, uintptr_t glowAddr,
                  uint32_t color, int type);
};
