// muei 無影 - synthesized input.
//
// SendInput only. Relative mouse motion for aim, so the physical cursor never
// teleports and the correction arrives through the same path a hand would take.
#pragma once

#include <windows.h>

#include "../core/Config.h"

namespace input {

// Relative mouse movement in counts. dx > 0 moves right.
void moveRelative(long dx, long dy);

void leftButton(bool down);
void space(bool down);

inline bool keyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

inline int aimKeyToVk(AimKey k) {
    switch (k) {
        case AimKey::RMB:      return VK_RBUTTON;
        case AimKey::LMB:      return VK_LBUTTON;
        case AimKey::Shift:    return VK_SHIFT;
        case AimKey::Alt:      return VK_MENU;
        case AimKey::XButton1: return VK_XBUTTON1;
    }
    return VK_RBUTTON;
}

} // namespace input
