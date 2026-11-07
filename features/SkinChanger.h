// muei 無影 - skin changer.
//
// Writes the fallback paint-kit block on the local player's own weapon entities.
// Nothing here touches another player's inventory and nothing is sent to the
// server: the value is what the client uses to render a finish it already owns
// the model for.
//
// The write path is only armed when every offset it needs is present in g_off.
// A missing offset puts a specific line in the menu and the log ("needs
// m_pAttributeManager") instead of writing a guessed address. That gate is the
// difference between a disabled cosmetic and a corrupted game state.
#pragma once

#include <string>

#include "../core/Config.h"
#include "../sdk/Frame.h"
#include "../sdk/Nav.h"

class SkinChanger {
public:
    void tick(const Nav& nav, const Config& cfg, const Frame& frame);

    // True when the offset set is complete enough to write safely.
    static bool available();

    // Which offsets are missing, for the menu line.
    static std::string missing();

    // How many weapons were written last tick.
    int applied() const { return applied_; }

private:
    int  applied_ = 0;
    bool warned_  = false;

    void applyToWeapon(IMemory& mem, uintptr_t weapon, const Config& cfg);
};
