#include "Input.h"

namespace input {

void moveRelative(long dx, long dy) {
    if (dx == 0 && dy == 0) return;

    INPUT in{};
    in.type       = INPUT_MOUSE;
    in.mi.dx      = dx;
    in.mi.dy      = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

void leftButton(bool down) {
    INPUT in{};
    in.type       = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(INPUT));
}

void space(bool down) {
    INPUT in{};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = VK_SPACE;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

} // namespace input
