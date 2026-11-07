#include "Bhop.h"
#include "Input.h"

void Bhop::release() {
    if (!pressed_) return;
    input::space(false);
    pressed_ = false;
}

void Bhop::tick(const Nav& nav, const Config& cfg, const Frame& frame) {
    if (!cfg.bhop || !frame.haveLocal || !frame.local.alive()) {
        release();
        return;
    }

    if (frame.local.onGround()) {
        if (!pressed_) {
            input::space(true);
            pressed_ = true;
        }
    } else {
        release();
    }

    if (!cfg.bhopAutoStrafe) return;

    // Strafe assist: nudge the mouse in the direction the view is already
    // turning so the velocity vector keeps re-projecting forward on landing.
    QAngle view = nav.viewAngles();
    if (!view.finite()) return;

    float d = angleNorm(view.yaw - lastYaw_);
    lastYaw_ = view.yaw;

    if (d > 180.f)  d -= 360.f;
    if (d < -180.f) d += 360.f;

    int step = 0;
    if (d > 0.6f)       step = 6;
    else if (d < -0.6f) step = -6;
    if (step) input::moveRelative(step, 0);
}
