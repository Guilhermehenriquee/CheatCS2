#include "Input.h"

#include "../core/ProcKernel.h"
#include "../core/Log.h"

namespace input {
namespace {

ProcKernel* g_driver = nullptr;

// mouclass button flags the driver's input command expects.
constexpr USHORT MOUSE_LEFT_DOWN  = 0x0001;
constexpr USHORT MOUSE_LEFT_UP    = 0x0002;

} // namespace

void attachDriver(ProcKernel* kernel) {
    g_driver = kernel;
    logx::line("input: driver route %s", kernel ? "available" : "unavailable");
}

bool driverAvailable() { return g_driver != nullptr; }

void moveRelative(long dx, long dy) {
    if (dx == 0 && dy == 0) return;

    if (g_driver) {
        aimstar::MouseInputData m{};
        m.UnitId  = 0;
        m.Flags   = 0;              // 0 == relative move
        m.Buttons = 0;
        m.LastX   = dx;
        m.LastY   = dy;
        if (g_driver->sendMouse(m)) return;
        // Driver rejected it: fall through instead of dropping the correction.
    }

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

void leftButton(bool down, bool useDriver) {
    if (useDriver && g_driver) {
        aimstar::MouseInputData m{};
        m.UnitId  = 0;
        m.Flags   = down ? MOUSE_LEFT_DOWN : MOUSE_LEFT_UP;
        m.Buttons = 0;
        if (g_driver->sendMouse(m)) return;
    }

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(INPUT));
}

void space(bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_SPACE;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

} // namespace input
