// muei 無影 - synthesized input.
//
// Two routes for the same actions, chosen per call:
//
//   SendInput      - the normal path. Relative mouse motion, so the physical
//                    cursor never teleports and the OS sees a plausible hand.
//   driver         - when a kernel backend is attached, click and motion go
//                    through the driver's input command instead, which the game
//                    cannot distinguish from a real device because it arrives
//                    below the Win32 input stack.
//
// Every route falls back to SendInput if the driver rejects a packet, so a
// stale driver never turns into a silently dead aimbot.
#pragma once

#include <windows.h>

#include "../core/Config.h"

class ProcKernel;

namespace input {

void attachDriver(ProcKernel* kernel);
bool driverAvailable();

// Relative mouse movement in counts. dx > 0 moves right.
void moveRelative(long dx, long dy);

// Left button press/release. `useDriver` asks for the kernel route.
void leftButton(bool down, bool useDriver = false);

// Space press/release, used by bhop.
void space(bool down);

// Poll helper: is a virtual key physically down.
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
